#include "VRInteractionManager.h"

#include "AvatarManager.h"
#include "CalendarHooks.h"
#include "HiggsSpatialReplayPolicy.h"
#include "QuestDialogueManager.h"
#include "WeatherNativeAccess.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <string_view>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr float kMaximumCalendarTimeScale = 1000.0f;
constexpr float kMaximumPoseRadians = 6.28318530717958647692f;
constexpr float kMaximumCalibrationValue = 10000.0f;
constexpr std::size_t kMaximumPendingHiggsSpatialReplays = 32;
namespace HiggsPolicy = SkyrimTogetherVR::HiggsSpatialReplayPolicy;

struct ServerSettingsState
{
    std::int32_t PreviousPlayerDifficulty{};
    float PreviousGreetDistance{};
    float PreviousWorldEncounters{};
    float PreviousKillMoveFrequency{};
    std::int32_t ServerDifficulty{};
    bool GreetingsEnabled{};
    bool WorldEncountersEnabled{};
    bool PvpEnabled{};
    bool Active{};
};

ServerSettingsState g_serverSettings{};
std::atomic<bool> g_pvpEnabledSnapshot{};
bool g_weatherOverrideActive{};

struct HiggsSpatialReplay
{
    RE::NiPointer<RE::Actor> Actor{};
    RE::NiPointer<RE::TESObjectREFR> Object{};
    RE::NiPointer<RE::NiAVObject> ActorRoot{};
    HiggsPolicy::Transaction Transaction{};
    bool BindingActive{};
};

std::array<HiggsSpatialReplay, kMaximumPendingHiggsSpatialReplays> g_higgsSpatialReplays{};

[[nodiscard]] RE::Setting* GreetDistanceSetting() noexcept
{
    auto* collection = RE::GameSettingCollection::GetSingleton();
    auto* setting = collection ? collection->GetSetting("fAIMinGreetingDistance") : nullptr;
    return setting && setting->GetType() == RE::Setting::Type::kFloat ? setting : nullptr;
}

[[nodiscard]] RE::TESGlobal* WorldEncountersGlobal() noexcept
{
    return RE::TESForm::LookupByID<RE::TESGlobal>(0x000B8EC1);
}

[[nodiscard]] RE::TESGlobal* KillMoveFrequencyGlobal() noexcept
{
    // PlayerService suppresses vanilla kill moves for every connected desktop
    // session. This global is lifecycle state, not part of the death-system
    // configuration that controls respawn behavior.
    return RE::TESForm::LookupByID<RE::TESGlobal>(0x00100F19);
}

[[nodiscard]] bool IsFinite(const GameplayActionPayload& a_payload) noexcept
{
    return std::isfinite(a_payload.ScalarA) && std::isfinite(a_payload.ScalarB) &&
           std::isfinite(a_payload.ScalarC) && std::isfinite(a_payload.ScalarD);
}

[[nodiscard]] HiggsPolicy::Transform ToHiggsPolicyTransform(const RE::NiTransform& acTransform) noexcept
{
    HiggsPolicy::Transform result{};
    result.Translate = {acTransform.translate.x, acTransform.translate.y, acTransform.translate.z};
    for (std::size_t row = 0; row < result.Rotate.Rows.size(); ++row)
        result.Rotate.Rows[row] = {acTransform.rotate.entry[row][0], acTransform.rotate.entry[row][1],
                                   acTransform.rotate.entry[row][2]};
    result.Scale = acTransform.scale;
    return result;
}

[[nodiscard]] RE::NiTransform ToNiTransform(const HiggsPolicy::Transform& acTransform) noexcept
{
    RE::NiTransform result{};
    result.translate = {acTransform.Translate.X, acTransform.Translate.Y, acTransform.Translate.Z};
    for (std::size_t row = 0; row < acTransform.Rotate.Rows.size(); ++row) {
        result.rotate.entry[row][0] = acTransform.Rotate.Rows[row].X;
        result.rotate.entry[row][1] = acTransform.Rotate.Rows[row].Y;
        result.rotate.entry[row][2] = acTransform.Rotate.Rows[row].Z;
    }
    result.scale = acTransform.Scale;
    return result;
}

[[nodiscard]] bool IsSafeTransform(const RE::NiTransform& acTransform) noexcept
{
    return HiggsPolicy::IsSafeTransform(ToHiggsPolicyTransform(acTransform));
}

void ClearHiggsSpatialReplay(const RE::TESObjectREFR& ac_object) noexcept
{
    for (auto& replay : g_higgsSpatialReplays) {
        if ((replay.Transaction.Active || replay.BindingActive) && replay.Object.get() == std::addressof(ac_object)) {
            HiggsPolicy::ClearForDrop(replay.Transaction);
            TP_UNUSED(replay.Object->SetMotionType(RE::hkpMotion::MotionType::kDynamic, true));
            replay = {};
        }
    }
}

void ClearHiggsSpatialReplaysForActor(const RE::Actor& ac_actor) noexcept
{
    for (auto& replay : g_higgsSpatialReplays) {
        if ((replay.Transaction.Active || replay.BindingActive) && replay.Actor.get() == std::addressof(ac_actor))
        {
            if (replay.Object)
                TP_UNUSED(replay.Object->SetMotionType(RE::hkpMotion::MotionType::kDynamic, true));
            replay = {};
        }
    }
}

[[nodiscard]] HiggsSpatialReplay* FindHiggsSpatialReplay(const RE::TESObjectREFR& ac_object,
                                                           const std::uint32_t a_sequence,
                                                           const bool a_isLeft) noexcept
{
    const auto existing = std::find_if(
        g_higgsSpatialReplays.begin(), g_higgsSpatialReplays.end(),
        [&ac_object, a_sequence, a_isLeft](const HiggsSpatialReplay& ac_replay) noexcept {
            return ac_replay.Transaction.Active && ac_replay.Object.get() == std::addressof(ac_object) &&
                   ac_replay.Transaction.Sequence == a_sequence && ac_replay.Transaction.IsLeft == a_isLeft;
        });
    return existing != g_higgsSpatialReplays.end() ? std::addressof(*existing) : nullptr;
}

[[nodiscard]] CommandStatus ApplyCompletedHiggsSpatialReplay(HiggsSpatialReplay& ar_replay) noexcept
{
    const auto clear = [&ar_replay](const CommandStatus a_status) noexcept { ar_replay = {}; return a_status; };
    if (!ar_replay.Actor || !ar_replay.Object || !HiggsPolicy::IsSafeTransform(ar_replay.Transaction.Relative) ||
        ar_replay.Actor->IsInRagdollState() || !AvatarManager::Get().IsManagedRemotePlayerActor(ar_replay.Actor.get()))
        return clear(CommandStatus::Inactive);

    RE::NiPointer<RE::NiAVObject> actorRoot{ar_replay.Actor->Get3D()};
    RE::NiPointer<RE::NiAVObject> objectRoot{ar_replay.Object->Get3D()};
    const auto* const objectRootOwner = objectRoot ? objectRoot->GetUserData() : nullptr;
    if (!actorRoot || actorRoot.get() != ar_replay.ActorRoot.get() || !objectRoot ||
        (objectRootOwner != nullptr && objectRootOwner != ar_replay.Object.get()))
        return clear(CommandStatus::Inactive);

    RE::NiPointer<RE::NiAVObject> hand;
    hand.reset(actorRoot->GetObjectByName(
        RE::BSFixedString(ar_replay.Transaction.IsLeft ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]")));
    if (!hand || !IsSafeTransform(hand->world))
        return clear(CommandStatus::Inactive);

    RE::NiPointer<RE::NiAVObject> grabbedNode = objectRoot;
    if (ar_replay.Transaction.NodeNameLength != 0) {
        grabbedNode.reset(objectRoot->GetObjectByName(RE::BSFixedString(ar_replay.Transaction.NodeName.data())));
        if (!grabbedNode)
            return clear(CommandStatus::Inactive);
    }
    if (!IsSafeTransform(grabbedNode->world) || !IsSafeTransform(objectRoot->world))
        return clear(CommandStatus::EngineRejected);

    const auto desiredGrabbedNodeWorld = HiggsPolicy::ComposeHandRelative(
        ToHiggsPolicyTransform(hand->world), ar_replay.Transaction.Relative);
    const auto desiredObjectRootWorld = HiggsPolicy::SolveObjectRootWorld(
        ToHiggsPolicyTransform(grabbedNode->world), ToHiggsPolicyTransform(objectRoot->world),
        desiredGrabbedNodeWorld);
    auto rootWorld = ToNiTransform(desiredObjectRootWorld);
    if (!IsSafeTransform(rootWorld) ||
        !ar_replay.Object->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, true))
        return clear(CommandStatus::EngineRejected);

    RE::NiPoint3 angles{};
    // False means the Euler representation is non-unique at gimbal lock; the
    // function still supplies a valid canonical angle triplet.
    TP_UNUSED(rootWorld.rotate.ToEulerAnglesXYZ(angles));
    if (!std::isfinite(angles.x) || !std::isfinite(angles.y) || !std::isfinite(angles.z))
        return clear(CommandStatus::EngineRejected);

    // HIGGS reports the hand-to-grabbed-node transform. Move the reference
    // root so the named node reaches that transform; never rewrite an
    // internal mesh node, which would deform the model without moving Havok.
    ar_replay.Object->SetPosition(rootWorld.translate);
    ar_replay.Object->SetAngle(angles);
    ar_replay.Transaction.Active = false;
    ar_replay.BindingActive = true;
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus BeginHiggsSpatialReplay(const RE::NiPointer<RE::Actor>& ac_actor,
                                                      RE::TESObjectREFR& ar_object,
                                                      const GameplayActionPayload& ac_payload) noexcept
{
    if (!ac_actor || !HiggsPolicy::IsSpatialBegin(ac_payload.ActionFlags) || ac_payload.ValueA == 0)
        return CommandStatus::Malformed;
    const auto sequence = static_cast<std::uint32_t>(ac_payload.ValueA);
    const bool isLeft = (ac_payload.ActionFlags & HiggsPolicy::kLeftHand) != 0;
    auto available = std::find_if(g_higgsSpatialReplays.begin(), g_higgsSpatialReplays.end(),
                                  [&ar_object, &ac_actor, isLeft](const HiggsSpatialReplay& replay) noexcept {
                                      return replay.BindingActive && replay.Object.get() == std::addressof(ar_object) &&
                                             replay.Actor.get() == ac_actor.get() && replay.Transaction.IsLeft == isLeft;
                                  });
    if (available == g_higgsSpatialReplays.end())
        available = std::find_if(g_higgsSpatialReplays.begin(), g_higgsSpatialReplays.end(),
                                  [](const HiggsSpatialReplay& ac_replay) noexcept {
                                      return !ac_replay.Transaction.Active && !ac_replay.BindingActive;
                                  });
    if (available == g_higgsSpatialReplays.end()) {
        available = g_higgsSpatialReplays.begin();
        if (available->Object)
            TP_UNUSED(available->Object->SetMotionType(RE::hkpMotion::MotionType::kDynamic, true));
    }
    *available = {};
    available->Actor = ac_actor;
    available->Object = std::addressof(ar_object);
    available->ActorRoot.reset(ac_actor->Get3D());
    if (!available->ActorRoot || !AvatarManager::Get().IsManagedRemotePlayerActor(ac_actor.get()))
    {
        *available = {};
        return CommandStatus::Inactive;
    }
    if (ac_payload.ValueB < 0 || ac_payload.ValueB > static_cast<std::int32_t>(HiggsPolicy::kMaximumNodeBytes))
        return CommandStatus::Malformed;
    HiggsPolicy::Begin(available->Transaction, sequence, isLeft, static_cast<std::uint8_t>(ac_payload.ValueB));
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus AppendHiggsSpatialNode(RE::TESObjectREFR& ar_object,
                                                     const GameplayActionPayload& ac_payload) noexcept
{
    if (!HiggsPolicy::IsSpatialNode(ac_payload.ActionFlags) || ac_payload.ValueA == 0 ||
        ac_payload.ScalarA != 0.0F || ac_payload.ScalarB != 0.0F || ac_payload.ScalarC != 0.0F ||
        ac_payload.ScalarD != 0.0F)
        return CommandStatus::Malformed;
    auto* replay = FindHiggsSpatialReplay(ar_object, static_cast<std::uint32_t>(ac_payload.ValueA),
                                          (ac_payload.ActionFlags & HiggsPolicy::kLeftHand) != 0);
    if (!replay)
        return CommandStatus::StaleEntity;
    std::array<char, HiggsPolicy::kNodeBytesPerChunk> bytes{};
    std::memcpy(bytes.data(), &ac_payload.LocalFormIdB, sizeof(ac_payload.LocalFormIdB));
    std::memcpy(bytes.data() + sizeof(ac_payload.LocalFormIdB), &ac_payload.LocalFormIdC, sizeof(ac_payload.LocalFormIdC));
    std::memcpy(bytes.data() + sizeof(ac_payload.LocalFormIdB) + sizeof(ac_payload.LocalFormIdC), &ac_payload.LocalFormIdD, sizeof(ac_payload.LocalFormIdD));
    std::memcpy(bytes.data() + sizeof(ac_payload.LocalFormIdB) + sizeof(ac_payload.LocalFormIdC) + sizeof(ac_payload.LocalFormIdD), &ac_payload.ValueB, sizeof(ac_payload.ValueB));
    return HiggsPolicy::AppendNode(replay->Transaction, static_cast<std::uint32_t>(ac_payload.ValueA),
                                   (ac_payload.ActionFlags & HiggsPolicy::kLeftHand) != 0,
                                   HiggsPolicy::ChunkIndex(ac_payload.ActionFlags), bytes) ?
               CommandStatus::Success : CommandStatus::StaleEntity;
}

[[nodiscard]] CommandStatus AppendHiggsSpatialChunk(RE::TESObjectREFR& ar_object,
                                                      const GameplayActionPayload& ac_payload) noexcept
{
    if (!HiggsPolicy::IsSpatialChunk(ac_payload.ActionFlags) || ac_payload.ValueA == 0)
        return CommandStatus::Malformed;
    const auto index = HiggsPolicy::ChunkIndex(ac_payload.ActionFlags);
    auto* replay = FindHiggsSpatialReplay(ar_object, static_cast<std::uint32_t>(ac_payload.ValueA),
                                          (ac_payload.ActionFlags & HiggsPolicy::kLeftHand) != 0);
    if (!replay)
        return CommandStatus::StaleEntity;
    const HiggsPolicy::Chunk chunk{{ac_payload.ScalarA, ac_payload.ScalarB, ac_payload.ScalarC, ac_payload.ScalarD}};
    const auto append = HiggsPolicy::Append(replay->Transaction, static_cast<std::uint32_t>(ac_payload.ValueA),
                                            (ac_payload.ActionFlags & HiggsPolicy::kLeftHand) != 0, index, chunk);
    if (append == HiggsPolicy::AppendResult::Rejected)
        return CommandStatus::StaleEntity;
    return append == HiggsPolicy::AppendResult::Complete ? ApplyCompletedHiggsSpatialReplay(*replay) :
                                                            CommandStatus::Success;
}

[[nodiscard]] bool HasOnlyZeroUnusedFields(const GameplayActionPayload& a_payload) noexcept
{
    return a_payload.LocalFormIdB == 0 && a_payload.LocalFormIdC == 0 && a_payload.LocalFormIdD == 0 &&
           a_payload.ValueA == 0 && a_payload.ValueB == 0 && a_payload.ActionFlags == 0;
}

template <class T>
[[nodiscard]] T* LookupLocalForm(const std::uint32_t a_formId) noexcept
{
    if (a_formId == 0)
        return nullptr;

    auto* form = RE::TESForm::LookupByID<T>(a_formId);
    return form && form->GetFormID() == a_formId ? form : nullptr;
}

[[nodiscard]] bool IsExpectedRemoteActor(
    const GameplayActionPayload& a_payload,
    const RE::NiPointer<RE::Actor>& a_actor) noexcept
{
    if (!a_actor || a_payload.TargetLocalFormId == 0)
        return a_actor != nullptr;

    auto* reference = LookupLocalForm<RE::TESObjectREFR>(a_payload.TargetLocalFormId);
    return reference == a_actor.get();
}

[[nodiscard]] CommandStatus ResolveRemoteActor(
    const CommandRecord& a_command,
    RE::NiPointer<RE::Actor>& ar_actor) noexcept
{
    const auto status = AvatarManager::Get().ResolveGameplayActor(a_command, ar_actor);
    if (status != CommandStatus::Success)
        return status;
    return IsExpectedRemoteActor(a_command.Payload.ApplyGameplayAction, ar_actor) ?
               CommandStatus::Success :
               CommandStatus::InvalidHandle;
}

[[nodiscard]] bool IsZeroTarget(const GameplayActionPayload& a_payload) noexcept
{
    return a_payload.TargetHandle.Value == 0 && a_payload.TargetLocalFormId == 0;
}

[[nodiscard]] bool IsCalendarPayloadValid(const GameplayActionPayload& a_payload) noexcept
{
    const bool preserveDate = (a_payload.ActionFlags & kPreserveCalendarDate) != 0;
    if (!IsZeroTarget(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.LocalFormIdB != 0 ||
        a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 ||
        (a_payload.ActionFlags & ~kPreserveCalendarDate) != 0 || a_payload.ScalarD != 0.0f ||
        a_payload.ScalarB < 0.0f || a_payload.ScalarB >= 24.0f ||
        a_payload.ScalarC < 0.0f || a_payload.ScalarC > kMaximumCalendarTimeScale)
        return false;

    if (preserveDate)
        return a_payload.ValueA == 0 && a_payload.ValueB == 0 && a_payload.ScalarA == 0.0F;

    if (a_payload.ValueA < 0 || a_payload.ValueA > 999 || a_payload.ValueB < 0 ||
        a_payload.ValueB >= static_cast<std::int32_t>(RE::Calendar::Months::kTotal) ||
        a_payload.ScalarA < 1.0f || std::floor(a_payload.ScalarA) != a_payload.ScalarA)
        return false;

    const auto month = static_cast<std::size_t>(a_payload.ValueB);
    return a_payload.ScalarA <= static_cast<float>(RE::Calendar::DAYS_IN_MONTH[month]);
}

[[nodiscard]] CommandStatus ApplyCalendar(const CommandRecord& a_command) noexcept
{
    const auto& a_payload = a_command.Payload.ApplyGameplayAction;
    if (!IsCalendarPayloadValid(a_payload))
        return CommandStatus::Malformed;

    auto* calendar = RE::Calendar::GetSingleton();
    if (!calendar || !calendar->gameYear || !calendar->gameMonth || !calendar->gameDay || !calendar->gameHour ||
        !calendar->gameDaysPassed || !calendar->timeScale)
        return CommandStatus::Inactive;

    const auto currentGameDaysPassed = calendar->gameDaysPassed->value;
    if (!std::isfinite(currentGameDaysPassed) || currentGameDaysPassed < 0.0F)
        return CommandStatus::EngineRejected;

    // The game derives GameDaysPassed from rawDaysPassed and hour. A server
    // calendar snapshot must not reconstruct a new elapsed-day count from its
    // displayed date, because that changes save/quest timing history.
    const auto rawDaysPassed = std::floor(currentGameDaysPassed);
    const auto expectedGameDaysPassed = rawDaysPassed + a_payload.ScalarB / 24.0F;
    const auto previousYear = calendar->gameYear->value;
    const auto previousMonth = calendar->gameMonth->value;
    const auto previousDay = calendar->gameDay->value;
    const auto previousHour = calendar->gameHour->value;
    const auto previousTimeScale = calendar->timeScale->value;
    const auto previousRawDaysPassed = calendar->rawDaysPassed;

    const auto restorePrevious = [&calendar, previousYear, previousMonth, previousDay, previousHour,
                                  currentGameDaysPassed, previousTimeScale, previousRawDaysPassed]() noexcept {
        calendar->gameYear->value = previousYear;
        calendar->gameMonth->value = previousMonth;
        calendar->gameDay->value = previousDay;
        calendar->gameHour->value = previousHour;
        calendar->gameDaysPassed->value = currentGameDaysPassed;
        calendar->timeScale->value = previousTimeScale;
        calendar->rawDaysPassed = previousRawDaysPassed;
    };

    if ((a_payload.ActionFlags & kPreserveCalendarDate) == 0) {
        calendar->gameYear->value = static_cast<float>(a_payload.ValueA);
        calendar->gameMonth->value = static_cast<float>(a_payload.ValueB);
        calendar->gameDay->value = a_payload.ScalarA;
    }
    calendar->gameHour->value = a_payload.ScalarB;
    calendar->timeScale->value = a_payload.ScalarC;
    calendar->rawDaysPassed = rawDaysPassed;
    calendar->gameDaysPassed->value = expectedGameDaysPassed;

    if (!CalendarHooks::IsCalendarSnapshotInvariant(
            calendar->gameDaysPassed->value, calendar->rawDaysPassed, calendar->gameHour->value,
            calendar->timeScale->value)) {
        restorePrevious();
        return CommandStatus::EngineRejected;
    }

    const auto& identity = a_command.Header.Identity;
    if (!CalendarHooks::ActivateAuthoritativeTick(
            identity.ServerInstanceNonce, identity.ConnectionGeneration, a_payload.ScalarC)) {
        restorePrevious();
        return CommandStatus::StaleSession;
    }

    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ApplyWeather(const GameplayActionPayload& a_payload) noexcept
{
    if (!IsZeroTarget(a_payload) || a_payload.LocalFormIdA == 0 || !HasOnlyZeroUnusedFields(a_payload) ||
        a_payload.ScalarA != 0.0f || a_payload.ScalarB != 0.0f || a_payload.ScalarC != 0.0f || a_payload.ScalarD != 0.0f)
        return CommandStatus::Malformed;

    auto* weather = LookupLocalForm<RE::TESWeather>(a_payload.LocalFormIdA);
    if (!weather)
        return CommandStatus::MissingForm;

    auto* sky = RE::Sky::GetSingleton();
    if (!sky)
        return CommandStatus::Inactive;

    if (!WeatherNativeAccess::ForceWeather(*sky, *weather))
        return CommandStatus::EngineRejected;

    g_weatherOverrideActive = true;
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ApplyServerSettings(const GameplayActionPayload& a_payload) noexcept
{
    if (!IsZeroTarget(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.LocalFormIdB != 0 ||
        a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 || a_payload.ValueA < 0 ||
        a_payload.ValueA > 5 || (a_payload.ValueB != 0 && a_payload.ValueB != 1) ||
        a_payload.ScalarA != 0.0F || a_payload.ScalarB != 0.0F || a_payload.ScalarC != 0.0F ||
        a_payload.ScalarD != 0.0F ||
        (a_payload.ActionFlags & ~(kWorldEncountersEnabled | kPvpEnabled)) != 0)
        return CommandStatus::Malformed;

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* greetDistance = GreetDistanceSetting();
    auto* worldEncounters = WorldEncountersGlobal();
    auto* killMoveFrequency = KillMoveFrequencyGlobal();
    if (!player || !greetDistance || !worldEncounters || !killMoveFrequency ||
        !std::isfinite(greetDistance->GetFloat()) || !std::isfinite(worldEncounters->value) ||
        !std::isfinite(killMoveFrequency->value))
        return CommandStatus::Inactive;

    if (!g_serverSettings.Active) {
        g_serverSettings.PreviousPlayerDifficulty = player->GetGameStatsData().difficulty;
        g_serverSettings.PreviousGreetDistance = greetDistance->GetFloat();
        g_serverSettings.PreviousWorldEncounters = worldEncounters->value;
        g_serverSettings.PreviousKillMoveFrequency = killMoveFrequency->value;
    }
    g_serverSettings.ServerDifficulty = a_payload.ValueA;
    g_serverSettings.GreetingsEnabled = a_payload.ValueB != 0;
    g_serverSettings.WorldEncountersEnabled =
        (a_payload.ActionFlags & kWorldEncountersEnabled) != 0;
    g_serverSettings.PvpEnabled = (a_payload.ActionFlags & kPvpEnabled) != 0;
    g_serverSettings.Active = true;
    player->GetGameStatsData().difficulty = g_serverSettings.ServerDifficulty;
    greetDistance->SetFloat(g_serverSettings.GreetingsEnabled ? g_serverSettings.PreviousGreetDistance : 0.0F);
    worldEncounters->value = g_serverSettings.WorldEncountersEnabled ? 1.0F : 0.0F;
    killMoveFrequency->value = 0.0F;
    g_pvpEnabledSnapshot.store(g_serverSettings.PvpEnabled, std::memory_order_release);
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ReleaseWeather(const GameplayActionPayload& a_payload) noexcept
{
    if (!IsZeroTarget(a_payload) || a_payload.LocalFormIdA != 0 || !HasOnlyZeroUnusedFields(a_payload) ||
        a_payload.ScalarA != 0.0F || a_payload.ScalarB != 0.0F || a_payload.ScalarC != 0.0F ||
        a_payload.ScalarD != 0.0F)
        return CommandStatus::Malformed;

    auto* sky = RE::Sky::GetSingleton();
    if (!sky)
        return CommandStatus::Inactive;
    if (!WeatherNativeAccess::ReleaseWeatherOverride(*sky))
        return CommandStatus::EngineRejected;

    g_weatherOverrideActive = false;
    return CommandStatus::Success;
}

[[nodiscard]] bool IsPosePayloadValid(const GameplayActionPayload& a_payload, const float a_limit) noexcept
{
    return a_payload.LocalFormIdA == 0 && HasOnlyZeroUnusedFields(a_payload) &&
           std::abs(a_payload.ScalarA) <= a_limit && std::abs(a_payload.ScalarB) <= a_limit &&
           std::abs(a_payload.ScalarC) <= a_limit && std::abs(a_payload.ScalarD) <= a_limit;
}

[[nodiscard]] bool ApplyGraphFloats(
    RE::Actor& a_actor,
    const std::array<std::string_view, 4>& a_names,
    const GameplayActionPayload& a_payload) noexcept
{
    if (a_actor.IsInRagdollState())
        return true;

    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if (!a_actor.GetAnimationGraphManager(manager) || !manager)
        return false;

    const std::array<float, 4> values{a_payload.ScalarA, a_payload.ScalarB, a_payload.ScalarC, a_payload.ScalarD};
    std::array<float, 4> previous{};
    for (std::size_t index = 0; index < a_names.size(); ++index) {
        if (!a_actor.GetGraphVariableFloat(RE::BSFixedString(a_names[index].data()), previous[index]))
            return false;
    }

    const auto managerUnchanged = [&]() noexcept {
        RE::BSTSmartPointer<RE::BSAnimationGraphManager> current;
        return a_actor.GetAnimationGraphManager(current) && current && current.get() == manager.get();
    };
    std::size_t written{};
    for (; written < a_names.size(); ++written) {
        if (!managerUnchanged() || !a_actor.SetGraphVariableFloat(RE::BSFixedString(a_names[written].data()), values[written])) {
            if (managerUnchanged()) {
                for (std::size_t rollback = 0; rollback < written; ++rollback)
                    a_actor.SetGraphVariableFloat(RE::BSFixedString(a_names[rollback].data()), previous[rollback]);
            }
            return false;
        }
    }
    return managerUnchanged();
}

[[nodiscard]] CommandStatus ApplyPoseChunk(const CommandRecord& a_command) noexcept
{
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    if (!IsPosePayloadValid(payload, kMaximumPoseRadians))
        return CommandStatus::Malformed;

    RE::NiPointer<RE::Actor> actor;
    const auto status = ResolveRemoteActor(a_command, actor);
    if (status != CommandStatus::Success)
        return status;

    constexpr std::array<std::string_view, 4> kPoseGraphVariables{
        "Pitch", "PitchOffset", "1stPRot", "1stPRotDamped"};
    return ApplyGraphFloats(*actor, kPoseGraphVariables, payload) ? CommandStatus::Success : CommandStatus::EngineRejected;
}

[[nodiscard]] CommandStatus ApplyCalibration(const CommandRecord& a_command) noexcept
{
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    if (!IsPosePayloadValid(payload, kMaximumCalibrationValue))
        return CommandStatus::Malformed;

    RE::NiPointer<RE::Actor> actor;
    const auto status = ResolveRemoteActor(a_command, actor);
    if (status != CommandStatus::Success)
        return status;

    constexpr std::array<std::string_view, 4> kCalibrationGraphVariables{
        "TurnDelta", "Direction", "SpeedSampled", "SpeedDamped"};
    return ApplyGraphFloats(*actor, kCalibrationGraphVariables, payload) ? CommandStatus::Success : CommandStatus::EngineRejected;
}

[[nodiscard]] CommandStatus ApplyHiggsInteraction(const CommandRecord& a_command) noexcept
{
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    const bool spatialNodeRecord = (payload.ActionFlags & HiggsPolicy::kSpatialNode) != 0;
    const bool spatialRebase = (payload.ActionFlags & HiggsPolicy::kSpatialRebase) != 0;
    if ((!spatialRebase && payload.LocalFormIdA == 0) || (!spatialNodeRecord && !spatialRebase && payload.LocalFormIdD != 0) ||
        std::abs(payload.ScalarA) > HiggsPolicy::kMaximumTransformMagnitude ||
        std::abs(payload.ScalarB) > HiggsPolicy::kMaximumTransformMagnitude ||
        std::abs(payload.ScalarC) > HiggsPolicy::kMaximumTransformMagnitude ||
        std::abs(payload.ScalarD) > HiggsPolicy::kMaximumTransformMagnitude ||
        !HiggsPolicy::HasOnlyKnownFlags(payload.ActionFlags))
        return CommandStatus::Malformed;

    RE::NiPointer<RE::Actor> actor;
    const auto status = ResolveRemoteActor(a_command, actor);
    if (status != CommandStatus::Success)
        return status;

    if (spatialRebase) {
        if (payload.ActionFlags != HiggsPolicy::kSpatialRebase || payload.LocalFormIdA != 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F)
            return CommandStatus::Malformed;
        ClearHiggsSpatialReplaysForActor(*actor);
        return CommandStatus::Success;
    }

    auto* object = LookupLocalForm<RE::TESObjectREFR>(payload.LocalFormIdA);
    if (!object)
        return CommandStatus::MissingForm;
    // PLANCK exclusively owns actor physics. HIGGS may only replay ordinary
    // object references, even if an invalid or malicious relay packet
    // resolves successfully to an actor locally.
    if (object->As<RE::Actor>())
        return CommandStatus::Unsupported;

    if (!spatialNodeRecord && payload.LocalFormIdB != 0 && !LookupLocalForm<RE::TESObjectCELL>(payload.LocalFormIdB))
        return CommandStatus::MissingCell;
    if (!spatialNodeRecord && payload.LocalFormIdC != 0 && !LookupLocalForm<RE::TESWorldSpace>(payload.LocalFormIdC))
        return CommandStatus::MissingForm;

    const auto action = static_cast<GameplayAction>(payload.Action);
    const bool spatialBegin = (payload.ActionFlags & HiggsPolicy::kSpatialBegin) != 0;
    const bool spatialChunk = (payload.ActionFlags & HiggsPolicy::kSpatialChunk) != 0;
    const bool spatialNode = (payload.ActionFlags & HiggsPolicy::kSpatialNode) != 0;
    if (spatialBegin || spatialChunk || spatialNode) {
        if (static_cast<int>(spatialBegin) + static_cast<int>(spatialChunk) + static_cast<int>(spatialNode) != 1)
            return CommandStatus::Malformed;
        if (spatialBegin) {
            if (action != GameplayAction::HiggsGrab && action != GameplayAction::HiggsPull)
                return CommandStatus::Malformed;
            return BeginHiggsSpatialReplay(actor, *object, payload);
        }
        if (spatialNode)
            return AppendHiggsSpatialNode(*object, payload);
        if (action != GameplayAction::HiggsPull || (payload.ActionFlags & HiggsPolicy::kSpatialBegin) != 0)
            return CommandStatus::Malformed;
        return AppendHiggsSpatialChunk(*object, payload);
    }

    if (payload.ScalarD != 0.0F || payload.ValueB != 0)
        return CommandStatus::Malformed;
    const bool hasPosition = payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.ValueA != 0 ||
                             payload.ScalarC != 0.0f;
    switch (action) {
    case GameplayAction::HiggsGrab:
    case GameplayAction::HiggsPull:
        if (!object->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, true))
            return CommandStatus::EngineRejected;
        if (hasPosition)
            object->SetPosition(payload.ScalarA, payload.ScalarB, payload.ScalarC);
        return CommandStatus::Success;
    case GameplayAction::HiggsDrop:
        if (hasPosition)
            object->SetPosition(payload.ScalarA, payload.ScalarB, payload.ScalarC);
        ClearHiggsSpatialReplay(*object);
        return object->SetMotionType(RE::hkpMotion::MotionType::kDynamic, true) ?
                   CommandStatus::Success :
                   CommandStatus::EngineRejected;
    case GameplayAction::HiggsStash:
    case GameplayAction::HiggsConsume:
        // These events describe how HIGGS initiated an inventory change. The
        // canonical inventory delta owns durable world/inventory mutation;
        // disabling the reference here races that path and can persist a false
        // disabled state in the receiver's save.
        ClearHiggsSpatialReplay(*object);
        return CommandStatus::Success;
    default:
        return CommandStatus::Malformed;
    }
}

[[nodiscard]] CommandStatus RejectPlanckInteraction(const CommandRecord& a_command) noexcept
{
    // PLANCK interface002 replay is intentionally outside this CommonLib
    // command path.  It crosses only the bounded POD bridge, so no gameplay
    // command can accidentally turn a remote physical observation into a
    // direct RE/Havok or HIGGS hand operation.
    TP_UNUSED(a_command);
    return CommandStatus::Unsupported;
}
} // namespace

CommandStatus VRInteractionManager::Execute(const CommandRecord& a_command) noexcept
{
    try {
        if (static_cast<CommandKind>(a_command.Header.Kind) != CommandKind::ApplyGameplayAction)
            return CommandStatus::Malformed;

        const auto& payload = a_command.Payload.ApplyGameplayAction;
        const auto domain = static_cast<GameplayDomain>(payload.Domain);
        const auto action = static_cast<GameplayAction>(payload.Action);
        if (a_command.Header.Identity.SequenceId != 0 || a_command.Header.Identity.ActionId == 0 ||
            payload.Reserved0 != 0 ||
            !std::all_of(std::begin(payload.ReservedTail), std::end(payload.ReservedTail), [](std::uint8_t a_value) { return a_value == 0; }) ||
            !IsFinite(payload) || !IsActionInDomain(domain, action))
            return CommandStatus::Malformed;
        if ((payload.TargetHandle.Value == 0 &&
             (a_command.Header.Identity.EntityId != 0 || a_command.Header.Identity.EntityGeneration != 0)) ||
            (payload.TargetHandle.Value != 0 &&
             (a_command.Header.Identity.EntityId == 0 || a_command.Header.Identity.EntityGeneration == 0)))
            return CommandStatus::Malformed;

        switch (domain) {
        case GameplayDomain::WorldState:
            switch (action) {
            case GameplayAction::SetCalendar:
                return ApplyCalendar(a_command);
            case GameplayAction::SetWeather:
                return ApplyWeather(payload);
            case GameplayAction::ApplyServerSettings:
                return ApplyServerSettings(payload);
            case GameplayAction::ReleaseWeather:
                return ReleaseWeather(payload);
            case GameplayAction::Teleport:
                return QuestDialogueManager::Execute(a_command);
            default:
                return CommandStatus::Malformed;
            }
        case GameplayDomain::VrBodyPose:
            switch (action) {
            case GameplayAction::VrPoseChunk:
                return ApplyPoseChunk(a_command);
            case GameplayAction::VrCalibration:
                return ApplyCalibration(a_command);
            default:
                return CommandStatus::Malformed;
            }
        case GameplayDomain::Higgs:
            return ApplyHiggsInteraction(a_command);
        case GameplayDomain::Planck:
            return RejectPlanckInteraction(a_command);
        default:
            return CommandStatus::Malformed;
        }
    } catch (...) {
        return CommandStatus::EngineRejected;
    }
}

bool VRInteractionManager::IsPvpEnabled() noexcept
{
    return g_pvpEnabledSnapshot.load(std::memory_order_acquire);
}

void VRInteractionManager::ProcessPeriodic() noexcept
{
    try {
        for (auto& replay : g_higgsSpatialReplays) {
            if (!replay.BindingActive)
                continue;
            replay.Transaction.Active = true;
            const auto status = ApplyCompletedHiggsSpatialReplay(replay);
            if (status != CommandStatus::Success)
                replay = {};
        }
        if (!g_serverSettings.Active)
            return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* greetDistance = GreetDistanceSetting();
        auto* worldEncounters = WorldEncountersGlobal();
        auto* killMoveFrequency = KillMoveFrequencyGlobal();
        if (!player || !greetDistance || !worldEncounters || !killMoveFrequency ||
            !std::isfinite(greetDistance->GetFloat()) || !std::isfinite(worldEncounters->value) ||
            !std::isfinite(killMoveFrequency->value))
            return;
        player->GetGameStatsData().difficulty = g_serverSettings.ServerDifficulty;
        greetDistance->SetFloat(g_serverSettings.GreetingsEnabled ? g_serverSettings.PreviousGreetDistance : 0.0F);
        worldEncounters->value = g_serverSettings.WorldEncountersEnabled ? 1.0F : 0.0F;
        killMoveFrequency->value = 0.0F;
    } catch (...) {
    }
}

void VRInteractionManager::Reset() noexcept
{
    CalendarHooks::ResetAuthoritativeTick();
    g_pvpEnabledSnapshot.store(false, std::memory_order_release);
    for (auto& replay : g_higgsSpatialReplays) {
        if ((replay.Transaction.Active || replay.BindingActive) && replay.Object)
            TP_UNUSED(replay.Object->SetMotionType(RE::hkpMotion::MotionType::kDynamic, true));
    }
    g_higgsSpatialReplays = {};
    try {
        if (g_serverSettings.Active) {
            if (auto* player = RE::PlayerCharacter::GetSingleton())
                player->GetGameStatsData().difficulty = g_serverSettings.PreviousPlayerDifficulty;
            if (auto* greetDistance = GreetDistanceSetting())
                greetDistance->SetFloat(g_serverSettings.PreviousGreetDistance);
            if (auto* worldEncounters = WorldEncountersGlobal())
                worldEncounters->value = g_serverSettings.PreviousWorldEncounters;
            if (auto* killMoveFrequency = KillMoveFrequencyGlobal())
                killMoveFrequency->value = g_serverSettings.PreviousKillMoveFrequency;
        }
        g_serverSettings = {};
    } catch (...) {
        g_serverSettings = {};
    }

    if (!g_weatherOverrideActive)
        return;

    try {
        if (auto* sky = RE::Sky::GetSingleton(); sky && WeatherNativeAccess::ReleaseWeatherOverride(*sky))
            g_weatherOverrideActive = false;
    } catch (...) {
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter
