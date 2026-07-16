#include "VRInteractionManager.h"

#include "AvatarManager.h"
#include "QuestDialogueManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr float kMaximumCalendarTimeScale = 1000.0f;
constexpr float kMaximumPoseRadians = 6.28318530717958647692f;
constexpr float kMaximumCalibrationValue = 10000.0f;

struct ServerSettingsState
{
    std::int32_t PreviousPlayerDifficulty{};
    float PreviousGreetDistance{};
    float PreviousWorldEncounters{};
    std::int32_t ServerDifficulty{};
    bool GreetingsEnabled{};
    bool WorldEncountersEnabled{};
    bool Active{};
};

ServerSettingsState g_serverSettings{};
bool g_weatherOverrideActive{};

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

[[nodiscard]] bool IsFinite(const GameplayActionPayload& a_payload) noexcept
{
    return std::isfinite(a_payload.ScalarA) && std::isfinite(a_payload.ScalarB) &&
           std::isfinite(a_payload.ScalarC) && std::isfinite(a_payload.ScalarD);
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

[[nodiscard]] float DaysPassed(const std::int32_t a_year, const std::int32_t a_month, const float a_day, const float a_hour) noexcept
{
    float days = static_cast<float>(a_year) * 365.0f + a_day + a_hour / 24.0f;
    for (std::int32_t month = 0; month < a_month; ++month)
        days += static_cast<float>(RE::Calendar::DAYS_IN_MONTH[month]);
    return days;
}

[[nodiscard]] CommandStatus ApplyCalendar(const GameplayActionPayload& a_payload) noexcept
{
    if (!IsCalendarPayloadValid(a_payload))
        return CommandStatus::Malformed;

    auto* calendar = RE::Calendar::GetSingleton();
    if (!calendar || !calendar->gameYear || !calendar->gameMonth || !calendar->gameDay || !calendar->gameHour ||
        !calendar->gameDaysPassed || !calendar->timeScale)
        return CommandStatus::Inactive;

    if ((a_payload.ActionFlags & kPreserveCalendarDate) == 0) {
        calendar->gameYear->value = static_cast<float>(a_payload.ValueA);
        calendar->gameMonth->value = static_cast<float>(a_payload.ValueB);
        calendar->gameDay->value = a_payload.ScalarA;
    }
    calendar->gameHour->value = a_payload.ScalarB;
    calendar->gameDaysPassed->value = DaysPassed(
        static_cast<std::int32_t>(calendar->gameYear->value),
        static_cast<std::int32_t>(calendar->gameMonth->value),
        calendar->gameDay->value, a_payload.ScalarB);
    calendar->timeScale->value = a_payload.ScalarC;
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

    // SetWeather is the typed CommonLib weather path. Override and acceleration
    // match the existing client weather synchronizer's authoritative handoff.
    sky->SetWeather(weather, true, true);
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
        (a_payload.ActionFlags & ~kWorldEncountersEnabled) != 0)
        return CommandStatus::Malformed;

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* greetDistance = GreetDistanceSetting();
    auto* worldEncounters = WorldEncountersGlobal();
    if (!player || !greetDistance || !worldEncounters ||
        !std::isfinite(greetDistance->GetFloat()) || !std::isfinite(worldEncounters->value))
        return CommandStatus::Inactive;

    if (!g_serverSettings.Active) {
        g_serverSettings.PreviousPlayerDifficulty = player->GetGameStatsData().difficulty;
        g_serverSettings.PreviousGreetDistance = greetDistance->GetFloat();
        g_serverSettings.PreviousWorldEncounters = worldEncounters->value;
    }
    g_serverSettings.ServerDifficulty = a_payload.ValueA;
    g_serverSettings.GreetingsEnabled = a_payload.ValueB != 0;
    g_serverSettings.WorldEncountersEnabled =
        (a_payload.ActionFlags & kWorldEncountersEnabled) != 0;
    g_serverSettings.Active = true;
    player->GetGameStatsData().difficulty = g_serverSettings.ServerDifficulty;
    greetDistance->SetFloat(g_serverSettings.GreetingsEnabled ? g_serverSettings.PreviousGreetDistance : 0.0F);
    worldEncounters->value = g_serverSettings.WorldEncountersEnabled ? 1.0F : 0.0F;
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
    sky->ReleaseWeatherOverride();
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
    if (payload.LocalFormIdA == 0 || payload.LocalFormIdD != 0 ||
        std::abs(payload.ScalarA) > 1000000.0f || std::abs(payload.ScalarB) > 1000000.0f ||
        std::abs(payload.ScalarC) > 1000000.0f || payload.ScalarD != 0.0f ||
        (payload.ActionFlags & ~0x13u) != 0)
        return CommandStatus::Malformed;

    RE::NiPointer<RE::Actor> actor;
    const auto status = ResolveRemoteActor(a_command, actor);
    if (status != CommandStatus::Success)
        return status;

    auto* object = LookupLocalForm<RE::TESObjectREFR>(payload.LocalFormIdA);
    if (!object)
        return CommandStatus::MissingForm;

    if (payload.LocalFormIdB != 0 && !LookupLocalForm<RE::TESObjectCELL>(payload.LocalFormIdB))
        return CommandStatus::MissingCell;
    if (payload.LocalFormIdC != 0 && !LookupLocalForm<RE::TESWorldSpace>(payload.LocalFormIdC))
        return CommandStatus::MissingForm;

    const auto action = static_cast<GameplayAction>(payload.Action);
    const bool hasPosition = payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.ValueA != 0 ||
                             payload.ScalarC != 0.0f;
    switch (action) {
    case GameplayAction::HiggsGrab:
    case GameplayAction::HiggsPull:
        object->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, true);
        if (hasPosition)
            object->SetPosition(payload.ScalarA, payload.ScalarB, payload.ScalarC);
        return CommandStatus::Success;
    case GameplayAction::HiggsDrop:
        if (hasPosition)
            object->SetPosition(payload.ScalarA, payload.ScalarB, payload.ScalarC);
        object->SetMotionType(RE::hkpMotion::MotionType::kDynamic, true);
        return CommandStatus::Success;
    case GameplayAction::HiggsStash:
    case GameplayAction::HiggsConsume:
        // These events describe how HIGGS initiated an inventory change. The
        // canonical inventory delta owns durable world/inventory mutation;
        // disabling the reference here races that path and can persist a false
        // disabled state in the receiver's save.
        return CommandStatus::Success;
    default:
        return CommandStatus::Malformed;
    }
}

[[nodiscard]] CommandStatus RejectPlanckInteraction(const CommandRecord& a_command) noexcept
{
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    RE::NiPointer<RE::Actor> actor;
    const auto status = ResolveRemoteActor(a_command, actor);
    if (status != CommandStatus::Success)
        return status;
    return payload.LocalFormIdA != 0 ? CommandStatus::Unsupported : CommandStatus::Malformed;
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
                return ApplyCalendar(payload);
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

void VRInteractionManager::ProcessPeriodic() noexcept
{
    try {
        if (!g_serverSettings.Active)
            return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* greetDistance = GreetDistanceSetting();
        auto* worldEncounters = WorldEncountersGlobal();
        if (!player || !greetDistance || !worldEncounters)
            return;
        player->GetGameStatsData().difficulty = g_serverSettings.ServerDifficulty;
        greetDistance->SetFloat(g_serverSettings.GreetingsEnabled ? g_serverSettings.PreviousGreetDistance : 0.0F);
        worldEncounters->value = g_serverSettings.WorldEncountersEnabled ? 1.0F : 0.0F;
    } catch (...) {
    }
}

void VRInteractionManager::Reset() noexcept
{
    try {
        if (g_serverSettings.Active) {
            if (auto* player = RE::PlayerCharacter::GetSingleton())
                player->GetGameStatsData().difficulty = g_serverSettings.PreviousPlayerDifficulty;
            if (auto* greetDistance = GreetDistanceSetting())
                greetDistance->SetFloat(g_serverSettings.PreviousGreetDistance);
            if (auto* worldEncounters = WorldEncountersGlobal())
                worldEncounters->value = g_serverSettings.PreviousWorldEncounters;
        }
        g_serverSettings = {};
        if (g_weatherOverrideActive) {
            if (auto* sky = RE::Sky::GetSingleton())
                sky->ReleaseWeatherOverride();
        }
        g_weatherOverrideActive = false;
    } catch (...) {
        g_serverSettings = {};
        g_weatherOverrideActive = false;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter
