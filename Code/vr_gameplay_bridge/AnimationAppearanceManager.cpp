#include "AnimationAppearanceManager.h"
#include "AvatarManager.h"
#include "LocalGameplayCapture.h"

#include <vr_common/VRCanonicalEntity.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr std::int32_t kMaximumItemCount = 10'000;
constexpr std::int32_t kMaximumInventoryTransactionValue = 1'000'000;
constexpr std::size_t kMaximumStagedEquipmentTransactions = 512;
constexpr std::size_t kMaximumStagedAppearanceTransactions = 512;
constexpr std::size_t kMaximumStagedInventoryTransactions = 512;
constexpr std::size_t kHeadPartCount =
    static_cast<std::size_t>(RE::BGSHeadPart::HeadPartType::kTotal);
constexpr std::uint32_t kRightHandEquipSlotFormId = 0x00013F42;
constexpr std::uint32_t kLeftHandEquipSlotFormId = 0x00013F43;
constexpr std::uintptr_t kCreateTintTextureVrRva = 0x0CFCFB0;
constexpr std::uintptr_t kCreateTintsVrRva = 0x04002F0;
constexpr std::uintptr_t kTextureComponentCtorVrRva = 0x01B8350;
// NiRenderedTexture extends the 0x40-byte NiTexture base with its renderer
// buffer as the first derived member. This is the layout used by the original
// Skyrim Together FaceGen path as well as Skyrim VR 1.4.15.
constexpr std::size_t kRenderedTextureBufferOffset = sizeof(RE::NiTexture);

// The wire format carries an ID, never a game string or a native pointer.
// Keep this deliberately small until each additional graph event has VR
// runtime semantics evidence.
enum class FixedAnimationEvent : std::uint32_t
{
    IdleForceDefaultState = 1,
    IdleReturnToDefault = 2,
    ReturnDefaultState = 3,
    ReturnToDefault = 4,
    ForceFurnitureExit = 5,
    IdleStop = 6,
    IdleStopInstant = 7,
    GetUpBegin = 8,
    JumpUp = 9,
    JumpDown = 10,
    JumpLand = 11,
    SneakStart = 12,
    SneakStop = 13,
    SprintStart = 14,
    SprintStop = 15,
    Ragdoll = 16,
    GetUpEnd = 17,
    ChairEnter = 18,
    ChairExit = 19,
    HorseEnter = 20,
    HorseExit = 21,
    WeaponDraw = 22,
    WeaponSheathe = 23,
};

[[nodiscard]] bool HasFiniteScalars(const GameplayActionPayload& a_payload) noexcept
{
    return std::isfinite(a_payload.ScalarA) && std::isfinite(a_payload.ScalarB) &&
           std::isfinite(a_payload.ScalarC) && std::isfinite(a_payload.ScalarD);
}

[[nodiscard]] bool HasNoScalars(const GameplayActionPayload& a_payload) noexcept
{
    return a_payload.ScalarA == 0.0F && a_payload.ScalarB == 0.0F &&
           a_payload.ScalarC == 0.0F && a_payload.ScalarD == 0.0F;
}

[[nodiscard]] bool HasNoUnusedForms(const GameplayActionPayload& a_payload) noexcept
{
    return a_payload.LocalFormIdB == 0 && a_payload.LocalFormIdC == 0 &&
           a_payload.LocalFormIdD == 0;
}

[[nodiscard]] bool HasValidAppearanceFlags(const GameplayActionPayload& a_payload) noexcept
{
    return (a_payload.ActionFlags & ~kAppearanceDeferredRefresh) == 0;
}

[[nodiscard]] CommandStatus FinishAppearanceMutation(
    RE::Actor& a_actor,
    const GameplayActionPayload& a_payload) noexcept
{
    if (!HasValidAppearanceFlags(a_payload))
        return CommandStatus::Malformed;
    if ((a_payload.ActionFlags & kAppearanceDeferredRefresh) == 0)
        a_actor.Update3DModel();
    return CommandStatus::Success;
}

[[nodiscard]] const char* EventName(const FixedAnimationEvent a_event) noexcept
{
    switch (a_event) {
    case FixedAnimationEvent::IdleForceDefaultState:
        return "IdleForceDefaultState";
    case FixedAnimationEvent::IdleReturnToDefault:
        return "IdleReturnToDefault";
    case FixedAnimationEvent::ReturnDefaultState:
        return "ReturnDefaultState";
    case FixedAnimationEvent::ReturnToDefault:
        return "ReturnToDefault";
    case FixedAnimationEvent::ForceFurnitureExit:
        return "ForceFurnExit";
    case FixedAnimationEvent::IdleStop:
        return "IdleStop";
    case FixedAnimationEvent::IdleStopInstant:
        return "IdleStopInstant";
    case FixedAnimationEvent::GetUpBegin:
        return "GetUpBegin";
    case FixedAnimationEvent::JumpUp:
        return "JumpUp";
    case FixedAnimationEvent::JumpDown:
        return "JumpDown";
    case FixedAnimationEvent::JumpLand:
        return "JumpLand";
    case FixedAnimationEvent::SneakStart:
        return "SneakStart";
    case FixedAnimationEvent::SneakStop:
        return "SneakStop";
    case FixedAnimationEvent::SprintStart:
        return "SprintStart";
    case FixedAnimationEvent::SprintStop:
        return "SprintStop";
    case FixedAnimationEvent::Ragdoll:
        return "Ragdoll";
    case FixedAnimationEvent::GetUpEnd:
        return "GetUpEnd";
    case FixedAnimationEvent::ChairEnter:
        return "ChairEnter";
    case FixedAnimationEvent::ChairExit:
        return "ChairExit";
    case FixedAnimationEvent::HorseEnter:
        return "HorseEnter";
    case FixedAnimationEvent::HorseExit:
        return "HorseExit";
    case FixedAnimationEvent::WeaponDraw:
        return "weaponDraw";
    case FixedAnimationEvent::WeaponSheathe:
        return "weaponSheathe";
    default:
        return nullptr;
    }
}

[[nodiscard]] CommandStatus ValidateEnvelope(const CommandRecord& a_command) noexcept
{
    if (static_cast<CommandKind>(a_command.Header.Kind) != CommandKind::ApplyGameplayAction ||
        a_command.Header.PayloadSize != kFixedPayloadBytes || a_command.Header.Flags != 0 ||
        a_command.Header.Identity.Reserved0 != 0 || a_command.Header.Identity.ActionId == 0)
        return CommandStatus::Malformed;

    const auto& payload = a_command.Payload.ApplyGameplayAction;
    const auto domain = static_cast<GameplayDomain>(payload.Domain);
    const auto action = static_cast<GameplayAction>(payload.Action);
    const bool canonicalEntity = CanonicalEntity::IsValid(
        a_command.Header.Identity.EntityId, a_command.Header.Identity.EntityGeneration);
    const bool zeroEntity = a_command.Header.Identity.EntityId == 0 &&
                            a_command.Header.Identity.EntityGeneration == 0;
    const bool remoteActor = payload.TargetHandle.Value >= kFirstRemoteAvatarHandle && canonicalEntity;
    const bool localPlayer = payload.TargetHandle.Value == kLocalPlayerHandle.Value && canonicalEntity;
    const bool localNativeInventory = payload.TargetHandle.Value == 0 && payload.TargetLocalFormId != 0 &&
                                      canonicalEntity && domain == GameplayDomain::Inventory &&
                                      IsInventoryTransactionAction(action);
    const bool worldInventory = payload.TargetHandle.Value == 0 && payload.TargetLocalFormId != 0 &&
                                zeroEntity && domain == GameplayDomain::Inventory &&
                                IsInventoryTransactionAction(action);
    const bool actorTarget = payload.TargetHandle.Value != 0 && payload.TargetLocalFormId == 0;
    const bool legacyInventoryTarget = domain == GameplayDomain::Inventory &&
                                       ((payload.TargetHandle.Value != 0) != (payload.TargetLocalFormId != 0));
    const bool inventoryTransaction = domain == GameplayDomain::Inventory && IsInventoryTransactionAction(action);
    if (!IsActionInDomain(domain, action) ||
        (inventoryTransaction ? !(remoteActor || localPlayer || localNativeInventory || worldInventory) :
                                (!actorTarget && !legacyInventoryTarget)) ||
        payload.Reserved0 != 0 ||
        payload.SecondaryHandle.Value != 0 ||
        !std::all_of(std::begin(payload.ReservedTail), std::end(payload.ReservedTail), [](std::uint8_t a_value) { return a_value == 0; }) ||
        !HasFiniteScalars(payload))
        return CommandStatus::Malformed;

    switch (domain) {
    case GameplayDomain::Animation:
    case GameplayDomain::Appearance:
    case GameplayDomain::Equipment:
    case GameplayDomain::Inventory:
        return CommandStatus::Success;
    default:
        return CommandStatus::Unsupported;
    }
}

[[nodiscard]] CommandStatus ApplyAnimation(
    RE::Actor& a_actor,
    const GameplayActionPayload& a_payload) noexcept
{
    if (static_cast<GameplayAction>(a_payload.Action) == GameplayAction::DrawWeapon) {
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 ||
            (a_payload.ValueA != 0 && a_payload.ValueA != 1) || a_payload.ValueB != 0 ||
            !HasNoScalars(a_payload))
            return CommandStatus::Malformed;

        const auto eventName = a_payload.ValueA != 0 ? "weaponDraw" : "weaponSheathe";
        return a_actor.NotifyAnimationGraph(RE::BSFixedString{eventName}) ?
                   CommandStatus::Success : CommandStatus::EngineRejected;
    }
    if (static_cast<GameplayAction>(a_payload.Action) != GameplayAction::AnimationEvent ||
        !HasNoUnusedForms(a_payload) || a_payload.ValueA != 0 || a_payload.ValueB != 0 ||
        !HasNoScalars(a_payload))
        return CommandStatus::Malformed;

    const auto* eventName = EventName(static_cast<FixedAnimationEvent>(a_payload.LocalFormIdA));
    if (!eventName)
        return CommandStatus::Unsupported;

    return a_actor.NotifyAnimationGraph(RE::BSFixedString{eventName}) ?
               CommandStatus::Success :
               CommandStatus::EngineRejected;
}

[[nodiscard]] CommandStatus ApplyAppearanceImmediate(
    RE::Actor& a_actor,
    const GameplayActionPayload& a_payload) noexcept
{
    auto* npc = a_actor.GetActorBase();
    if (!npc || !npc->IsDynamicForm())
        return CommandStatus::EngineRejected;
    const auto action = static_cast<GameplayAction>(a_payload.Action);
    switch (action) {
    case GameplayAction::SetRace:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA == 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload) || !HasValidAppearanceFlags(a_payload))
            return CommandStatus::Malformed;
        if (!RE::TESForm::LookupByID<RE::TESRace>(a_payload.LocalFormIdA))
            return CommandStatus::MissingForm;
        npc->race = RE::TESForm::LookupByID<RE::TESRace>(a_payload.LocalFormIdA);
        npc->originalRace = nullptr;
        return FinishAppearanceMutation(a_actor, a_payload);
    case GameplayAction::SetSex:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA > 1 || a_payload.ValueB != 0 || !HasNoScalars(a_payload) ||
            !HasValidAppearanceFlags(a_payload))
            return CommandStatus::Malformed;
        npc->actorData.actorBaseFlags.set(a_payload.ValueA != 0, RE::ACTOR_BASE_DATA::Flag::kFemale);
        return FinishAppearanceMutation(a_actor, a_payload);
    case GameplayAction::SetWeight:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || a_payload.ScalarA < 0.0F || a_payload.ScalarA > 100.0F ||
            a_payload.ScalarB != 0.0F || a_payload.ScalarC != 0.0F || a_payload.ScalarD != 0.0F ||
            !HasValidAppearanceFlags(a_payload))
            return CommandStatus::Malformed;
        npc->weight = a_payload.ScalarA;
        return FinishAppearanceMutation(a_actor, a_payload);
    case GameplayAction::SetName:
        // CommandRecord is fixed-size and has no UTF-8/UTF-16 value field.
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return CommandStatus::Malformed;
        return CommandStatus::Unsupported;
    case GameplayAction::SetHeadPart:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA == 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA >= static_cast<std::int32_t>(RE::BGSHeadPart::HeadPartType::kTotal) ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload) || !HasValidAppearanceFlags(a_payload))
            return CommandStatus::Malformed;
        if (!RE::TESForm::LookupByID<RE::BGSHeadPart>(a_payload.LocalFormIdA))
            return CommandStatus::MissingForm;
        npc->ChangeHeadPart(RE::TESForm::LookupByID<RE::BGSHeadPart>(a_payload.LocalFormIdA));
        return FinishAppearanceMutation(a_actor, a_payload);
    case GameplayAction::SetTint:
        if (a_payload.LocalFormIdA != 0 ||
            a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA >= static_cast<std::int32_t>(RE::TintMask::Type::kTotal) ||
            a_payload.ValueB != 0 || a_payload.ScalarA < 0.0F || a_payload.ScalarA > 1.0F ||
            a_payload.ScalarB != 0.0F || a_payload.ScalarC != 0.0F || a_payload.ScalarD != 0.0F ||
            !HasValidAppearanceFlags(a_payload))
            return CommandStatus::Malformed;
        if (a_payload.ValueA != static_cast<std::int32_t>(RE::TintMask::Type::kSkinTone))
            return CommandStatus::Unsupported;
        npc->bodyTintColor.red = static_cast<std::uint8_t>((a_payload.LocalFormIdB >> 16) & 0xffu);
        npc->bodyTintColor.green = static_cast<std::uint8_t>((a_payload.LocalFormIdB >> 8) & 0xffu);
        npc->bodyTintColor.blue = static_cast<std::uint8_t>(a_payload.LocalFormIdB & 0xffu);
        return FinishAppearanceMutation(a_actor, a_payload);
    case GameplayAction::SetHairColor:
    {
        if (!HasNoUnusedForms(a_payload) || a_payload.ValueA != 0 || a_payload.ValueB != 0 ||
            !HasNoScalars(a_payload) || !HasValidAppearanceFlags(a_payload))
            return CommandStatus::Malformed;
        auto* color = a_payload.LocalFormIdA != 0 ?
            RE::TESForm::LookupByID<RE::BGSColorForm>(a_payload.LocalFormIdA) : nullptr;
        if (a_payload.LocalFormIdA != 0 && !color)
            return CommandStatus::MissingForm;
        npc->SetHairColor(color);
        return FinishAppearanceMutation(a_actor, a_payload);
    }
    case GameplayAction::SetFaceTexture:
    {
        if (!HasNoUnusedForms(a_payload) || a_payload.ValueA != 0 || a_payload.ValueB != 0 ||
            !HasNoScalars(a_payload) || !HasValidAppearanceFlags(a_payload))
            return CommandStatus::Malformed;
        auto* texture = a_payload.LocalFormIdA != 0 ?
            RE::TESForm::LookupByID<RE::BGSTextureSet>(a_payload.LocalFormIdA) : nullptr;
        if (a_payload.LocalFormIdA != 0 && !texture)
            return CommandStatus::MissingForm;
        npc->SetFaceTexture(texture);
        return FinishAppearanceMutation(a_actor, a_payload);
    }
    case GameplayAction::SetFaceMorph:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA >= static_cast<std::int32_t>(kFaceMorphCount) || a_payload.ValueB != 0 ||
            std::abs(a_payload.ScalarA) > kMaximumFaceMorphMagnitude || a_payload.ScalarB != 0.0F ||
            a_payload.ScalarC != 0.0F || a_payload.ScalarD != 0.0F || !HasValidAppearanceFlags(a_payload))
            return CommandStatus::Malformed;
        if (!npc->faceData)
            npc->faceData = RE::calloc<RE::TESNPC::FaceData>(1);
        if (!npc->faceData)
            return CommandStatus::EngineRejected;
        npc->faceData->morphs[a_payload.ValueA] = a_payload.ScalarA;
        return FinishAppearanceMutation(a_actor, a_payload);
    case GameplayAction::SetFacePart:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA >= static_cast<std::int32_t>(kFacePartCount) ||
            (a_payload.ValueB != kFacePartDefault &&
             (a_payload.ValueB < 0 || a_payload.ValueB > kMaximumFacePartPreset)) ||
            !HasNoScalars(a_payload) || !HasValidAppearanceFlags(a_payload))
            return CommandStatus::Malformed;
        if (!npc->faceData)
            npc->faceData = RE::calloc<RE::TESNPC::FaceData>(1);
        if (!npc->faceData)
            return CommandStatus::EngineRejected;
        npc->faceData->parts[a_payload.ValueA] = a_payload.ValueB;
        return FinishAppearanceMutation(a_actor, a_payload);
    case GameplayAction::CommitAppearance:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload) || a_payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        a_actor.Update3DModel();
        return CommandStatus::Success;
    case GameplayAction::ResetFaceData:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload) || !HasValidAppearanceFlags(a_payload))
            return CommandStatus::Malformed;
        if (npc->faceData) {
            std::fill(std::begin(npc->faceData->morphs), std::end(npc->faceData->morphs), 0.0F);
            std::fill(std::begin(npc->faceData->parts), std::end(npc->faceData->parts), kFacePartDefault);
        }
        return FinishAppearanceMutation(a_actor, a_payload);
    default:
        return CommandStatus::Malformed;
    }

    return CommandStatus::Malformed;
}

struct StagedAppearanceTransaction
{
    BridgeIdentity Identity{};
    std::uint64_t LastActionId{};
    std::uint64_t Digest{};
    std::uint32_t Sequence{};
    RE::TESRace* Race{};
    RE::BGSColorForm* HairColor{};
    RE::BGSTextureSet* FaceTexture{};
    std::array<RE::BGSHeadPart*, kHeadPartCount> HeadParts{};
    std::bitset<kHeadPartCount> HeadPartPresent{};
    std::array<float, kFaceMorphCount> FaceMorphs{};
    std::array<std::int32_t, kFacePartCount> FaceParts{};
    std::bitset<kFaceMorphCount> FaceMorphPresent{};
    std::bitset<kFacePartCount> FacePartPresent{};
    struct Tint
    {
        std::string TexturePath{};
        std::uint32_t Color{};
        float Alpha{};
        std::uint8_t Type{};
        bool Present{};
        bool PathExpected{};
        bool PathSpecified{};
    };
    std::array<Tint, kMaximumAppearanceTints> Tints{};
    std::string Name{};
    float Weight{};
    std::int32_t Sex{};
    std::uint16_t Level{1};
    bool Essential{};
    CommandStatus Failure{CommandStatus::Success};
    bool HasRace{};
    bool HasSex{};
    bool HasWeight{};
    bool HairColorSpecified{};
    bool FaceTextureSpecified{};
    bool HeadPartsReset{};
    bool FaceDataSpecified{};
    bool HasFaceData{};
    bool TintsReset{};
    bool NameSpecified{};
    std::uint8_t ExpectedHeadPartCount{};
    std::uint8_t ExpectedTintCount{};
};

std::unordered_map<std::uint64_t, StagedAppearanceTransaction> s_stagedAppearances;
struct AppliedAppearance
{
    std::uint32_t Sequence{};
    std::uint64_t Digest{};
};
std::unordered_map<std::uint64_t, AppliedAppearance> s_appliedAppearances;
std::unordered_map<std::uint64_t, AppliedAppearance> s_partialAppearances;
// The engine owns the currently installed array when a dynamic NPC is deleted.
// Track only arrays allocated by this manager so subsequent swaps never free a
// possibly shared template array returned by CreateDuplicateForm.
struct ManagedHeadPartBuffer
{
    std::uint64_t Target{};
    RE::BGSHeadPart** Buffer{};
    RE::BGSHeadPart** OriginalBuffer{};
    std::int8_t OriginalCount{};
};
std::array<ManagedHeadPartBuffer, kMaximumStagedAppearanceTransactions> s_managedHeadPartBuffers{};

[[nodiscard]] ManagedHeadPartBuffer* FindManagedHeadPartBuffer(const std::uint64_t a_target) noexcept
{
    const auto found = std::find_if(
        s_managedHeadPartBuffers.begin(), s_managedHeadPartBuffers.end(),
        [a_target](const ManagedHeadPartBuffer& a_entry) { return a_entry.Target == a_target; });
    return found != s_managedHeadPartBuffers.end() ? std::addressof(*found) : nullptr;
}

[[nodiscard]] ManagedHeadPartBuffer* ReserveManagedHeadPartBuffer(const std::uint64_t a_target) noexcept
{
    if (auto* existing = FindManagedHeadPartBuffer(a_target))
        return existing;
    const auto empty = std::find_if(
        s_managedHeadPartBuffers.begin(), s_managedHeadPartBuffers.end(),
        [](const ManagedHeadPartBuffer& a_entry) { return a_entry.Target == 0; });
    return empty != s_managedHeadPartBuffers.end() ? std::addressof(*empty) : nullptr;
}

[[nodiscard]] bool SameAppearanceIdentity(
    const BridgeIdentity& a_lhs, const BridgeIdentity& a_rhs) noexcept
{
    return a_lhs.ServerInstanceNonce == a_rhs.ServerInstanceNonce &&
           a_lhs.ConnectionGeneration == a_rhs.ConnectionGeneration &&
           a_lhs.LifecycleEpoch == a_rhs.LifecycleEpoch && a_lhs.EntityId == a_rhs.EntityId &&
           a_lhs.EntityGeneration == a_rhs.EntityGeneration;
}

[[nodiscard]] bool IsValidUtf8(const std::string_view a_text) noexcept
{
    for (std::size_t index = 0; index < a_text.size();) {
        const auto lead = static_cast<std::uint8_t>(a_text[index]);
        if (lead == 0)
            return false;
        if (lead < 0x80) {
            ++index;
            continue;
        }
        std::size_t continuationCount{};
        std::uint32_t codePoint{};
        if (lead >= 0xC2 && lead <= 0xDF) { continuationCount = 1; codePoint = lead & 0x1Fu; }
        else if (lead >= 0xE0 && lead <= 0xEF) { continuationCount = 2; codePoint = lead & 0x0Fu; }
        else if (lead >= 0xF0 && lead <= 0xF4) { continuationCount = 3; codePoint = lead & 0x07u; }
        else return false;
        if (index + continuationCount >= a_text.size())
            return false;
        for (std::size_t continuation = 1; continuation <= continuationCount; ++continuation) {
            const auto byte = static_cast<std::uint8_t>(a_text[index + continuation]);
            if ((byte & 0xC0u) != 0x80u)
                return false;
            codePoint = (codePoint << 6) | (byte & 0x3Fu);
        }
        if ((continuationCount == 2 && codePoint < 0x800) ||
            (continuationCount == 3 && codePoint < 0x10000) || codePoint > 0x10FFFF ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF))
            return false;
        index += continuationCount + 1;
    }
    return true;
}

[[nodiscard]] bool IsSafeTexturePath(const std::string_view a_path) noexcept
{
    if (a_path.empty() || a_path.size() > kMaximumAppearanceTexturePathBytes ||
        !IsValidUtf8(a_path) || a_path.front() == '/' || a_path.front() == '\\' ||
        a_path.find(':') != std::string_view::npos)
        return false;
    std::size_t segmentStart{};
    while (segmentStart <= a_path.size()) {
        const auto segmentEnd = a_path.find_first_of("/\\", segmentStart);
        const auto segment = a_path.substr(
            segmentStart, segmentEnd == std::string_view::npos ? std::string_view::npos : segmentEnd - segmentStart);
        if (segment.empty() || segment == "." || segment == "..")
            return false;
        if (segmentEnd == std::string_view::npos)
            break;
        segmentStart = segmentEnd + 1;
    }
    return true;
}

[[nodiscard]] bool IsExecutableGameAddress(const std::uintptr_t a_address) noexcept
{
    MEMORY_BASIC_INFORMATION memory{};
    const auto module = GetModuleHandleW(nullptr);
    if (!module || VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || memory.AllocationBase != module)
        return false;
    const auto protection = memory.Protect & 0xffu;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] std::uintptr_t ResolveLockedVrFunction(const std::uintptr_t a_rva) noexcept
{
    const auto& module = REL::Module::get();
    if (!REL::Module::IsVR() || module.version() != REL::Version{1, 4, 15, 0} ||
        module.base() > std::numeric_limits<std::uintptr_t>::max() - a_rva)
        return 0;
    const auto address = module.base() + a_rva;
    return IsExecutableGameAddress(address) ? address : 0;
}

[[nodiscard]] bool CanComposeFaceTints()
{
    return ResolveLockedVrFunction(kCreateTintTextureVrRva) != 0 &&
           ResolveLockedVrFunction(kCreateTintsVrRva) != 0 &&
           ResolveLockedVrFunction(kTextureComponentCtorVrRva) != 0 &&
           RE::BSGraphics::Renderer::GetSingleton() != nullptr;
}

[[nodiscard]] CommandStatus ComposeFaceTints(
    RE::Actor& a_actor, const StagedAppearanceTransaction& a_staged)
{
    if (!CanComposeFaceTints())
        return CommandStatus::Unsupported;
    auto* faceObject = a_actor.GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace);
    auto* geometry = faceObject ? faceObject->AsGeometry() : nullptr;
    auto* shader = geometry ? geometry->lightingShaderProp_cast() : nullptr;
    if (!shader || !shader->material ||
        shader->material->GetFeature() != RE::BSShaderMaterial::Feature::kFaceGen)
        return CommandStatus::Inactive;

    using CreateTexture = RE::NiTexture*(RE::BSFixedString&);
    using CreateTints = void(const RE::BSTArray<RE::TintMask*>&, RE::NiTexture*);
    using TextureCtor = RE::TESTexture*(RE::TESTexture*);
    const auto createTexture = reinterpret_cast<CreateTexture*>(
        ResolveLockedVrFunction(kCreateTintTextureVrRva));
    const auto createTints = reinterpret_cast<CreateTints*>(
        ResolveLockedVrFunction(kCreateTintsVrRva));
    const auto textureCtor = reinterpret_cast<TextureCtor*>(
        ResolveLockedVrFunction(kTextureComponentCtorVrRva));
    if (!createTexture || !createTints || !textureCtor)
        return CommandStatus::Unsupported;

    RE::BSTArray<RE::TintMask*> masks;
    std::vector<RE::TintMask*> ownedMasks;
    std::vector<RE::TESTexture*> ownedTextures;
    const auto cleanup = [&]() noexcept {
        for (auto* texture : ownedTextures) {
            if (texture) {
                texture->~TESTexture();
                RE::free(texture);
            }
        }
        for (auto* mask : ownedMasks)
            RE::free(mask);
    };

    try {
        ownedMasks.reserve(a_staged.ExpectedTintCount);
        ownedTextures.reserve(a_staged.ExpectedTintCount);
        masks.reserve(a_staged.ExpectedTintCount);
        for (std::size_t index = 0; index < a_staged.ExpectedTintCount; ++index) {
            const auto& source = a_staged.Tints[index];
            auto* mask = RE::calloc<RE::TintMask>(1);
            if (!mask) {
                cleanup();
                return CommandStatus::EngineRejected;
            }
            ownedMasks.push_back(mask);
            mask->color = RE::Color(source.Color);
            mask->alpha = source.Alpha;
            mask->type = static_cast<RE::TintMask::Type>(source.Type);
            if (!source.TexturePath.empty()) {
                auto* texture = RE::calloc<RE::TESTexture>(1);
                if (!texture || !textureCtor(texture)) {
                    RE::free(texture);
                    cleanup();
                    return CommandStatus::EngineRejected;
                }
                ownedTextures.push_back(texture);
                texture->InitializeDataComponent();
                texture->textureName = source.TexturePath.c_str();
                mask->texture = texture;
            }
            masks.push_back(mask);
        }

        RE::BSFixedString textureName{""};
        auto* rendered = createTexture(textureName);
        if (!rendered) {
            cleanup();
            return CommandStatus::EngineRejected;
        }
        RE::NiPointer<RE::NiTexture> renderedOwner{rendered};
        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        auto* rendererData = renderer ? renderer->CreateRenderTexture(512, 512) : nullptr;
        if (!rendererData) {
            cleanup();
            return CommandStatus::EngineRejected;
        }
        static_assert(sizeof(RE::NiTexture) == 0x40);
        // CreateTexture returns the NiRenderedTexture subtype used by the
        // original client. Its renderer buffer is the first derived member.
        *reinterpret_cast<RE::NiTexture::RendererData**>(
            reinterpret_cast<std::byte*>(rendered) + kRenderedTextureBufferOffset) = rendererData;
        createTints(masks, rendered);

        auto* material = static_cast<RE::BSLightingShaderMaterialFacegen*>(shader->material);
        auto& tintTexture = *reinterpret_cast<RE::NiPointer<RE::NiTexture>*>(
            std::addressof(material->tintTexture));
        tintTexture = renderedOwner;
        cleanup();
        return CommandStatus::Success;
    } catch (...) {
        cleanup();
        return CommandStatus::EngineRejected;
    }
}

[[nodiscard]] CommandStatus StageAppearance(
    const CommandRecord& a_command, const GameplayActionPayload& a_payload)
{
    const auto action = static_cast<GameplayAction>(a_payload.Action);
    const auto target = a_payload.TargetHandle.Value;
    const auto actionId = a_command.Header.Identity.ActionId;
    const auto knownFlags = kAppearanceDeferredRefresh |
        (action == GameplayAction::SetTint ? kAppearanceTintHasTexturePath : 0u);
    if (target == 0 || (a_payload.ActionFlags & kAppearanceDeferredRefresh) == 0 ||
        (a_payload.ActionFlags & ~knownFlags) != 0 || action == GameplayAction::CommitAppearance)
        return CommandStatus::Malformed;

    auto found = s_stagedAppearances.find(target);
    if (action == GameplayAction::BeginAppearance) {
        if (found == s_stagedAppearances.end() &&
            s_stagedAppearances.size() >= kMaximumStagedAppearanceTransactions)
            return CommandStatus::QueueOverflow;
        StagedAppearanceTransaction transaction{};
        transaction.Identity = a_command.Header.Identity;
        transaction.LastActionId = actionId;
        found = s_stagedAppearances.insert_or_assign(target, transaction).first;
    } else {
        if (found == s_stagedAppearances.end() ||
            !SameAppearanceIdentity(found->second.Identity, a_command.Header.Identity) ||
            found->second.LastActionId == std::numeric_limits<std::uint64_t>::max() ||
            actionId != found->second.LastActionId + 1)
            return CommandStatus::StaleEntity;
        found->second.LastActionId = actionId;
    }

    auto& staged = found->second;
    const auto fail = [&staged](const CommandStatus a_status) noexcept {
        if (staged.Failure == CommandStatus::Success)
            staged.Failure = a_status;
        return a_status;
    };
    if (staged.Failure != CommandStatus::Success)
        return staged.Failure;

    switch (action) {
    case GameplayAction::BeginAppearance:
        if (a_payload.LocalFormIdA == 0 || a_payload.LocalFormIdD != 2 ||
            a_payload.ValueA < 0 || a_payload.ValueA > static_cast<std::int32_t>(kHeadPartCount) ||
            a_payload.ValueB < 0 || a_payload.ValueB > static_cast<std::int32_t>(kMaximumAppearanceTints) ||
            !HasNoScalars(a_payload))
            return fail(CommandStatus::Malformed);
        staged.Sequence = a_payload.LocalFormIdA;
        staged.Digest = (static_cast<std::uint64_t>(a_payload.LocalFormIdC) << 32) |
                        a_payload.LocalFormIdB;
        if (staged.Digest == 0)
            return fail(CommandStatus::Malformed);
        if (const auto applied = s_appliedAppearances.find(target); applied != s_appliedAppearances.end()) {
            if (staged.Sequence == applied->second.Sequence && staged.Digest != applied->second.Digest)
                return fail(CommandStatus::Malformed);
            if (staged.Sequence != applied->second.Sequence &&
                static_cast<std::int32_t>(staged.Sequence - applied->second.Sequence) <= 0)
                return fail(CommandStatus::StaleEntity);
        }
        if (const auto partial = s_partialAppearances.find(target); partial != s_partialAppearances.end()) {
            if (staged.Sequence == partial->second.Sequence && staged.Digest != partial->second.Digest)
                return fail(CommandStatus::Malformed);
            if (staged.Sequence != partial->second.Sequence &&
                static_cast<std::int32_t>(staged.Sequence - partial->second.Sequence) <= 0)
                return fail(CommandStatus::StaleEntity);
        }
        staged.ExpectedHeadPartCount = static_cast<std::uint8_t>(a_payload.ValueA);
        staged.ExpectedTintCount = static_cast<std::uint8_t>(a_payload.ValueB);
        return CommandStatus::Success;
    case GameplayAction::SetRace:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA == 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return fail(CommandStatus::Malformed);
        staged.Race = RE::TESForm::LookupByID<RE::TESRace>(a_payload.LocalFormIdA);
        if (!staged.Race)
            return fail(CommandStatus::MissingForm);
        staged.HasRace = true;
        return CommandStatus::Success;
    case GameplayAction::SetSex:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA > 1 || a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return fail(CommandStatus::Malformed);
        staged.Sex = a_payload.ValueA;
        staged.HasSex = true;
        return CommandStatus::Success;
    case GameplayAction::SetWeight:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || a_payload.ScalarA < 0.0F || a_payload.ScalarA > 100.0F ||
            a_payload.ScalarB != 0.0F || a_payload.ScalarC != 0.0F || a_payload.ScalarD != 0.0F)
            return fail(CommandStatus::Malformed);
        staged.Weight = a_payload.ScalarA;
        staged.HasWeight = true;
        return CommandStatus::Success;
    case GameplayAction::SetHairColor:
        if (!HasNoUnusedForms(a_payload) || a_payload.ValueA != 0 || a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return fail(CommandStatus::Malformed);
        staged.HairColor = a_payload.LocalFormIdA != 0 ?
            RE::TESForm::LookupByID<RE::BGSColorForm>(a_payload.LocalFormIdA) : nullptr;
        if (a_payload.LocalFormIdA != 0 && !staged.HairColor)
            return fail(CommandStatus::MissingForm);
        staged.HairColorSpecified = true;
        return CommandStatus::Success;
    case GameplayAction::SetFaceTexture:
        if (!HasNoUnusedForms(a_payload) || a_payload.ValueA != 0 || a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return fail(CommandStatus::Malformed);
        staged.FaceTexture = a_payload.LocalFormIdA != 0 ?
            RE::TESForm::LookupByID<RE::BGSTextureSet>(a_payload.LocalFormIdA) : nullptr;
        if (a_payload.LocalFormIdA != 0 && !staged.FaceTexture)
            return fail(CommandStatus::MissingForm);
        staged.FaceTextureSpecified = true;
        return CommandStatus::Success;
    case GameplayAction::ResetHeadParts:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return fail(CommandStatus::Malformed);
        staged.HeadParts = {};
        staged.HeadPartPresent.reset();
        staged.HeadPartsReset = true;
        return CommandStatus::Success;
    case GameplayAction::SetHeadPart:
    {
        if (!staged.HeadPartsReset || !HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA == 0 ||
            a_payload.ValueA < 0 || a_payload.ValueA >= static_cast<std::int32_t>(kHeadPartCount) ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return fail(CommandStatus::Malformed);
        auto* headPart = RE::TESForm::LookupByID<RE::BGSHeadPart>(a_payload.LocalFormIdA);
        if (!headPart)
            return fail(CommandStatus::MissingForm);
        const auto slot = static_cast<std::size_t>(a_payload.ValueA);
        if (headPart->type != static_cast<RE::BGSHeadPart::HeadPartType>(slot))
            return fail(CommandStatus::Malformed);
        staged.HeadParts[slot] = headPart;
        staged.HeadPartPresent.set(slot);
        return CommandStatus::Success;
    }
    case GameplayAction::ResetFaceData:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return fail(CommandStatus::Malformed);
        staged.FaceMorphs = {};
        staged.FaceParts = {};
        staged.FaceMorphPresent.reset();
        staged.FacePartPresent.reset();
        staged.FaceDataSpecified = true;
        staged.HasFaceData = false;
        return CommandStatus::Success;
    case GameplayAction::SetFaceMorph:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA >= static_cast<std::int32_t>(kFaceMorphCount) || a_payload.ValueB != 0 ||
            std::abs(a_payload.ScalarA) > kMaximumFaceMorphMagnitude || a_payload.ScalarB != 0.0F ||
            a_payload.ScalarC != 0.0F || a_payload.ScalarD != 0.0F)
            return fail(CommandStatus::Malformed);
        staged.FaceMorphs[static_cast<std::size_t>(a_payload.ValueA)] = a_payload.ScalarA;
        staged.FaceMorphPresent.set(static_cast<std::size_t>(a_payload.ValueA));
        staged.FaceDataSpecified = true;
        staged.HasFaceData = true;
        return CommandStatus::Success;
    case GameplayAction::SetFacePart:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA >= static_cast<std::int32_t>(kFacePartCount) ||
            (a_payload.ValueB != kFacePartDefault &&
             (a_payload.ValueB < 0 || a_payload.ValueB > kMaximumFacePartPreset)) || !HasNoScalars(a_payload))
            return fail(CommandStatus::Malformed);
        staged.FaceParts[static_cast<std::size_t>(a_payload.ValueA)] = a_payload.ValueB;
        staged.FacePartPresent.set(static_cast<std::size_t>(a_payload.ValueA));
        staged.FaceDataSpecified = true;
        staged.HasFaceData = true;
        return CommandStatus::Success;
    case GameplayAction::SetTint:
        if (!staged.TintsReset || a_payload.LocalFormIdA != 0 || a_payload.LocalFormIdC != 0 ||
            a_payload.LocalFormIdD != 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA >= staged.ExpectedTintCount || a_payload.ValueB < 0 ||
            a_payload.ValueB >= static_cast<std::int32_t>(RE::TintMask::Type::kTotal) ||
            a_payload.ScalarA < 0.0F || a_payload.ScalarA > 1.0F ||
            a_payload.ScalarB != 0.0F || a_payload.ScalarC != 0.0F || a_payload.ScalarD != 0.0F)
            return fail(CommandStatus::Malformed);
        {
            auto& tint = staged.Tints[static_cast<std::size_t>(a_payload.ValueA)];
            if (tint.Present)
                return fail(CommandStatus::Malformed);
            tint.Color = a_payload.LocalFormIdB;
            tint.Alpha = a_payload.ScalarA;
            tint.Type = static_cast<std::uint8_t>(a_payload.ValueB);
            tint.Present = true;
            tint.PathExpected = (a_payload.ActionFlags & kAppearanceTintHasTexturePath) != 0;
            tint.PathSpecified = !tint.PathExpected;
        }
        return CommandStatus::Success;
    case GameplayAction::ResetTints:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return fail(CommandStatus::Malformed);
        staged.Tints = {};
        staged.TintsReset = true;
        return CommandStatus::Success;
    case GameplayAction::ClearHeadPart:
        return fail(CommandStatus::Unsupported);
    default:
        return fail(CommandStatus::Malformed);
    }
}

[[nodiscard]] CommandStatus CommitStagedAppearance(
    const CommandRecord& a_command, RE::Actor& a_actor, const GameplayActionPayload& a_payload)
{
    if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA <= 0 ||
        a_payload.ValueA > std::numeric_limits<std::uint16_t>::max() ||
        (a_payload.ValueB != 0 && a_payload.ValueB != 1) || !HasNoScalars(a_payload) ||
        a_payload.ActionFlags != 0)
        return CommandStatus::Malformed;

    const auto target = a_payload.TargetHandle.Value;
    const auto found = s_stagedAppearances.find(target);
    if (found == s_stagedAppearances.end() ||
        !SameAppearanceIdentity(found->second.Identity, a_command.Header.Identity) ||
        found->second.LastActionId == std::numeric_limits<std::uint64_t>::max() ||
        a_command.Header.Identity.ActionId != found->second.LastActionId + 1)
        return CommandStatus::StaleEntity;

    auto staged = found->second;
    s_stagedAppearances.erase(found);
    staged.Level = static_cast<std::uint16_t>(a_payload.ValueA);
    staged.Essential = a_payload.ValueB != 0;
    if (staged.Failure != CommandStatus::Success)
        return staged.Failure;
    if (staged.Sequence == 0 || staged.Digest == 0 || !staged.HasRace || !staged.HasSex ||
        !staged.HasWeight || !staged.HairColorSpecified || !staged.NameSpecified ||
        !staged.FaceTextureSpecified || !staged.HeadPartsReset || !staged.FaceDataSpecified ||
        !staged.TintsReset || staged.HeadPartPresent.count() != staged.ExpectedHeadPartCount ||
        (staged.HasFaceData &&
         (staged.FaceMorphPresent.count() != kFaceMorphCount ||
          staged.FacePartPresent.count() != kFacePartCount)))
        return CommandStatus::Malformed;
    for (std::size_t index = 0; index < staged.ExpectedTintCount; ++index)
        if (!staged.Tints[index].Present || !staged.Tints[index].PathSpecified)
            return CommandStatus::Malformed;
    if (const auto applied = s_appliedAppearances.find(target);
        applied != s_appliedAppearances.end() && applied->second.Sequence == staged.Sequence &&
        applied->second.Digest == staged.Digest)
        return CommandStatus::Success;

    auto* npc = a_actor.GetActorBase();
    if (!npc || !npc->IsDynamicForm())
        return CommandStatus::EngineRejected;
    if (staged.ExpectedTintCount != 0 && !CanComposeFaceTints())
        return CommandStatus::Unsupported;
    if (const auto partial = s_partialAppearances.find(target);
        partial != s_partialAppearances.end() && partial->second.Sequence == staged.Sequence &&
        partial->second.Digest == staged.Digest) {
        const auto tintStatus = staged.ExpectedTintCount != 0 ?
            ComposeFaceTints(a_actor, staged) : CommandStatus::Success;
        if (tintStatus == CommandStatus::Success) {
            s_partialAppearances.erase(partial);
            s_appliedAppearances[target] = {staged.Sequence, staged.Digest};
        }
        return tintStatus;
    }

    const auto headPartCount = staged.HeadPartPresent.count();
    auto** newHeadParts = headPartCount != 0 ? RE::calloc<RE::BGSHeadPart*>(headPartCount) : nullptr;
    if (headPartCount != 0 && !newHeadParts)
        return CommandStatus::EngineRejected;
    auto* managedHeadParts = FindManagedHeadPartBuffer(target);
    if (!managedHeadParts)
        managedHeadParts = ReserveManagedHeadPartBuffer(target);
    if (!managedHeadParts) {
        RE::free(newHeadParts);
        return CommandStatus::QueueOverflow;
    }
    if (managedHeadParts->Target == target && npc->headParts != managedHeadParts->Buffer) {
        RE::free(newHeadParts);
        return CommandStatus::EngineRejected;
    }
    RE::TESNPC::FaceData* newFaceData{};
    if (staged.HasFaceData && !npc->faceData) {
        newFaceData = RE::calloc<RE::TESNPC::FaceData>(1);
        if (!newFaceData) {
            RE::free(newHeadParts);
            return CommandStatus::EngineRejected;
        }
    }

    std::size_t headPartIndex{};
    for (std::size_t slot = 0; slot < staged.HeadParts.size(); ++slot)
        if (staged.HeadPartPresent.test(slot))
            newHeadParts[headPartIndex++] = staged.HeadParts[slot];

    npc->race = staged.Race;
    npc->originalRace = nullptr;
    npc->actorData.actorBaseFlags.set(staged.Sex != 0, RE::ACTOR_BASE_DATA::Flag::kFemale);
    npc->weight = staged.Weight;
    npc->SetActorBaseFlag(RE::ACTOR_BASE_DATA::Flag::kPCLevelMult, false, true);
    npc->actorData.level = staged.Level;
    npc->SetActorBaseFlag(RE::ACTOR_BASE_DATA::Flag::kEssential, staged.Essential, true);
    npc->SetHairColor(staged.HairColor);
    npc->SetFaceTexture(staged.FaceTexture);
    npc->SetFullName(staged.Name.c_str());

    auto** oldHeadParts = npc->headParts;
    const auto oldHeadPartCount = npc->numHeadParts;
    npc->headParts = newHeadParts;
    npc->numHeadParts = static_cast<std::int8_t>(headPartCount);
    RE::BGSHeadPart** retiredManagedHeadParts{};
    if (managedHeadParts->Target == target) {
        retiredManagedHeadParts = oldHeadParts;
        managedHeadParts->Buffer = newHeadParts;
    } else {
        *managedHeadParts = {target, newHeadParts, oldHeadParts, oldHeadPartCount};
    }

    if (staged.HasFaceData) {
        if (newFaceData)
            npc->faceData = newFaceData;
        std::copy(staged.FaceMorphs.begin(), staged.FaceMorphs.end(), std::begin(npc->faceData->morphs));
        std::copy(staged.FaceParts.begin(), staged.FaceParts.end(), std::begin(npc->faceData->parts));
    } else if (npc->faceData) {
        std::fill(std::begin(npc->faceData->morphs), std::end(npc->faceData->morphs), 0.0F);
        std::fill(std::begin(npc->faceData->parts), std::end(npc->faceData->parts), kFacePartDefault);
    }
    for (std::size_t index = 0; index < staged.ExpectedTintCount; ++index) {
        const auto& tint = staged.Tints[index];
        if (tint.Type != static_cast<std::uint8_t>(RE::TintMask::Type::kSkinTone))
            continue;
        npc->bodyTintColor.red = static_cast<std::uint8_t>((tint.Color >> 16) & 0xffu);
        npc->bodyTintColor.green = static_cast<std::uint8_t>((tint.Color >> 8) & 0xffu);
        npc->bodyTintColor.blue = static_cast<std::uint8_t>(tint.Color & 0xffu);
        break;
    }
    a_actor.Update3DModel();
    const auto tintStatus = staged.ExpectedTintCount != 0 ?
        ComposeFaceTints(a_actor, staged) : CommandStatus::Success;
    RE::free(retiredManagedHeadParts);
    if (tintStatus == CommandStatus::Success) {
        s_partialAppearances.erase(target);
        s_appliedAppearances[target] = {staged.Sequence, staged.Digest};
    } else {
        s_partialAppearances[target] = {staged.Sequence, staged.Digest};
    }
    return tintStatus;
}

[[nodiscard]] CommandStatus ApplyAppearance(
    const CommandRecord& a_command, RE::Actor& a_actor, const GameplayActionPayload& a_payload)
{
    const auto action = static_cast<GameplayAction>(a_payload.Action);
    if (action == GameplayAction::CommitAppearance)
        return CommitStagedAppearance(a_command, a_actor, a_payload);
    if ((a_payload.ActionFlags & kAppearanceDeferredRefresh) != 0)
        return StageAppearance(a_command, a_payload);
    if (action == GameplayAction::ClearHeadPart || action == GameplayAction::ResetHeadParts)
        return CommandStatus::Unsupported;
    return ApplyAppearanceImmediate(a_actor, a_payload);
}

struct StagedEquipmentEntry
{
    std::uint32_t FormId{};
    std::uint32_t Flags{};
    std::uint32_t Count{};
};

struct StagedEquipmentTransaction
{
    std::uint64_t TransactionId{};
    std::uint64_t ServerInstanceNonce{};
    std::uint64_t ConnectionGeneration{};
    std::uint64_t LifecycleEpoch{};
    std::uint32_t LeftSpell{};
    std::uint32_t RightSpell{};
    std::uint32_t Shout{};
    std::uint16_t ExpectedEntries{};
    std::vector<StagedEquipmentEntry> Entries{};
};

std::unordered_map<std::uint64_t, StagedEquipmentTransaction> s_stagedEquipment{};

[[nodiscard]] std::uint64_t TransactionId(const std::uint32_t aHigh, const std::uint32_t aLow) noexcept
{
    return (static_cast<std::uint64_t>(aHigh) << 32) | aLow;
}

[[nodiscard]] CommandStatus ApplyStagedEquipment(
    const CommandRecord& a_command, RE::Actor& a_actor, const GameplayActionPayload& a_payload)
{
    const auto action = static_cast<GameplayAction>(a_payload.Action);
    const auto actorKey = a_payload.TargetHandle.Value;
    if (actorKey == 0)
        return CommandStatus::Malformed;

    const auto noScalars = HasNoScalars(a_payload);
    if (action == GameplayAction::EquipmentSnapshotBegin) {
        const auto transactionId = TransactionId(a_payload.LocalFormIdD, static_cast<std::uint32_t>(a_payload.ValueB));
        if (transactionId == 0 || a_payload.ValueA < 0 || a_payload.ValueA > 64 || !noScalars || a_payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        if (s_stagedEquipment.size() >= kMaximumStagedEquipmentTransactions &&
            s_stagedEquipment.find(actorKey) == s_stagedEquipment.end())
            return CommandStatus::EngineRejected;
        auto& staged = s_stagedEquipment[actorKey];
        staged = {transactionId,
                  a_command.Header.Identity.ServerInstanceNonce,
                  a_command.Header.Identity.ConnectionGeneration,
                  a_command.Header.Identity.LifecycleEpoch,
                  a_payload.LocalFormIdA,
                  a_payload.LocalFormIdB,
                  a_payload.LocalFormIdC,
                  static_cast<std::uint16_t>(a_payload.ValueA),
                  {}};
        staged.Entries.reserve(staged.ExpectedEntries);
        return CommandStatus::Success;
    }

    auto stagedIt = s_stagedEquipment.find(actorKey);
    if (stagedIt == s_stagedEquipment.end())
        return CommandStatus::Malformed;
    auto& staged = stagedIt->second;
    const auto eraseAnd = [&stagedIt](const CommandStatus aStatus) noexcept {
        s_stagedEquipment.erase(stagedIt);
        return aStatus;
    };
    if (staged.ServerInstanceNonce != a_command.Header.Identity.ServerInstanceNonce ||
        staged.ConnectionGeneration != a_command.Header.Identity.ConnectionGeneration ||
        staged.LifecycleEpoch != a_command.Header.Identity.LifecycleEpoch)
        return eraseAnd(CommandStatus::StaleSession);

    if (action == GameplayAction::EquipmentSnapshotItem) {
        const auto transactionId = TransactionId(a_payload.LocalFormIdB, a_payload.LocalFormIdC);
        const auto wornFlags = kEquipmentSnapshotWorn | kEquipmentSnapshotWornLeft;
        const auto classificationFlags = kEquipmentSnapshotWeapon | kEquipmentSnapshotAmmo;
        const auto knownFlags = wornFlags | classificationFlags;
        if (transactionId != staged.TransactionId || a_payload.LocalFormIdA == 0 || a_payload.LocalFormIdD != 0 ||
            a_payload.ValueA <= 0 || a_payload.ValueA > kMaximumItemCount || a_payload.ValueB != 0 ||
            !noScalars || (a_payload.ActionFlags & ~knownFlags) != 0 ||
            (a_payload.ActionFlags & wornFlags) == 0 ||
            (a_payload.ActionFlags & classificationFlags) == classificationFlags ||
            ((a_payload.ActionFlags & kEquipmentSnapshotAmmo) != 0 &&
             (a_payload.ActionFlags & kEquipmentSnapshotWornLeft) != 0) ||
            staged.Entries.size() >= staged.ExpectedEntries ||
            std::any_of(staged.Entries.begin(), staged.Entries.end(), [&a_payload](const StagedEquipmentEntry& aEntry) {
                return aEntry.FormId == a_payload.LocalFormIdA;
            }))
            return eraseAnd(CommandStatus::Malformed);
        staged.Entries.push_back({a_payload.LocalFormIdA, a_payload.ActionFlags, static_cast<std::uint32_t>(a_payload.ValueA)});
        return CommandStatus::Success;
    }

    if (action != GameplayAction::EquipmentSnapshotEnd ||
        TransactionId(a_payload.LocalFormIdA, a_payload.LocalFormIdB) != staged.TransactionId ||
        a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 || a_payload.ValueA != 0 ||
        a_payload.ValueB != 0 || !noScalars || a_payload.ActionFlags != 0 ||
        staged.Entries.size() != staged.ExpectedEntries)
        return eraseAnd(CommandStatus::Malformed);

    auto* manager = RE::ActorEquipManager::GetSingleton();
    if (!manager)
        return eraseAnd(CommandStatus::Inactive);
    const auto lookupSpell = [](const std::uint32_t aFormId) noexcept {
        return aFormId != 0 ? RE::TESForm::LookupByID<RE::SpellItem>(aFormId) : nullptr;
    };
    const auto leftSpell = lookupSpell(staged.LeftSpell);
    const auto rightSpell = lookupSpell(staged.RightSpell);
    auto* shout = staged.Shout != 0 ? RE::TESForm::LookupByID<RE::TESShout>(staged.Shout) : nullptr;
    if ((staged.LeftSpell != 0 && !leftSpell) || (staged.RightSpell != 0 && !rightSpell) ||
        (staged.Shout != 0 && !shout))
        return eraseAnd(CommandStatus::MissingForm);

    const auto& runtime = a_actor.GetActorRuntimeData();
    auto* currentLeftSpell = runtime.selectedSpells[RE::Actor::SlotTypes::kLeftHand] ?
        runtime.selectedSpells[RE::Actor::SlotTypes::kLeftHand]->As<RE::SpellItem>() : nullptr;
    auto* currentRightSpell = runtime.selectedSpells[RE::Actor::SlotTypes::kRightHand] ?
        runtime.selectedSpells[RE::Actor::SlotTypes::kRightHand]->As<RE::SpellItem>() : nullptr;
    auto* currentShout = runtime.selectedPower ? runtime.selectedPower->As<RE::TESShout>() : nullptr;
    const bool needsLeftUnequip = currentLeftSpell && currentLeftSpell != leftSpell;
    const bool needsRightUnequip = currentRightSpell && currentRightSpell != rightSpell;
    const bool needsShoutUnequip = currentShout && currentShout != shout;
    auto* skyrimVm = RE::SkyrimVM::GetSingleton();
    auto* vm = skyrimVm ? skyrimVm->impl.get() : nullptr;
    auto* handles = vm ? vm->GetObjectHandlePolicy() : nullptr;
    if (needsLeftUnequip || needsRightUnequip || needsShoutUnequip) {
        if (!handles)
            return eraseAnd(CommandStatus::Inactive);
        const auto validationHandle = handles->GetHandleForObject(a_actor.GetFormType(), &a_actor);
        if (validationHandle == handles->EmptyHandle())
            return eraseAnd(CommandStatus::EngineRejected);
    }

    const auto inventory = a_actor.GetInventory();
    std::sort(staged.Entries.begin(), staged.Entries.end(), [](const StagedEquipmentEntry& aLeft, const StagedEquipmentEntry& aRight) {
        return aLeft.FormId < aRight.FormId;
    });
    for (const auto& entry : staged.Entries) {
        auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(entry.FormId);
        if (!object)
            return eraseAnd(CommandStatus::MissingForm);
        const bool isWeapon = object->GetFormType() == RE::FormType::Weapon;
        const bool isAmmo = object->GetFormType() == RE::FormType::Ammo;
        if (isWeapon != ((entry.Flags & kEquipmentSnapshotWeapon) != 0) ||
            isAmmo != ((entry.Flags & kEquipmentSnapshotAmmo) != 0))
            return eraseAnd(CommandStatus::Malformed);
        const auto inventoryEntry = inventory.find(object);
        if (inventoryEntry == inventory.end() ||
            inventoryEntry->second.first < static_cast<std::int32_t>(entry.Count))
            return eraseAnd(CommandStatus::EngineRejected);
    }
    struct PreviousWornItem
    {
        RE::TESBoundObject* Object{};
        std::uint32_t Count{};
        bool Worn{};
        bool WornLeft{};
        bool Weapon{};
    };
    std::vector<PreviousWornItem> wornItems;
    for (const auto& [object, data] : inventory) {
        if (object && data.second && data.second->IsWorn() && data.first > 0)
            wornItems.push_back({object, static_cast<std::uint32_t>(data.first),
                                 data.second->IsWorn(false), data.second->IsWorn(true),
                                 object->GetFormType() == RE::FormType::Weapon});
    }
    std::sort(wornItems.begin(), wornItems.end(), [](const auto& aLeft, const auto& aRight) {
        return aLeft.Object->GetFormID() < aRight.Object->GetFormID();
    });

    const auto restorePreviousState = [&]() noexcept {
        try {
            const auto currentInventory = a_actor.GetInventory();
            for (const auto& [object, data] : currentInventory) {
                if (object && data.second && data.second->IsWorn())
                    manager->UnequipObject(&a_actor, object, nullptr, 1, nullptr, true, true, false, false);
            }
            for (const auto& previous : wornItems) {
                if (previous.Worn)
                    manager->EquipObject(
                        &a_actor, previous.Object, nullptr, previous.Count,
                        previous.Weapon ? RE::TESForm::LookupByID<RE::BGSEquipSlot>(kRightHandEquipSlotFormId) : nullptr,
                        true, true, false, false);
                if (previous.WornLeft && (previous.Weapon || !previous.Worn))
                    manager->EquipObject(
                        &a_actor, previous.Object, nullptr, previous.Count,
                        previous.Weapon ? RE::TESForm::LookupByID<RE::BGSEquipSlot>(kLeftHandEquipSlotFormId) : nullptr,
                        true, true, false, false);
            }
            if (currentLeftSpell)
                manager->EquipSpell(&a_actor, currentLeftSpell,
                                    RE::TESForm::LookupByID<RE::BGSEquipSlot>(kLeftHandEquipSlotFormId));
            if (currentRightSpell)
                manager->EquipSpell(&a_actor, currentRightSpell,
                                    RE::TESForm::LookupByID<RE::BGSEquipSlot>(kRightHandEquipSlotFormId));
            if (currentShout)
                manager->EquipShout(&a_actor, currentShout);
        } catch (...) {
        }
    };

    // All form, actor, manager, and count validation is complete before the
    // first engine mutation. The bounded staging entry is consumed only here.
    try {
        for (const auto& previous : wornItems)
            manager->UnequipObject(&a_actor, previous.Object, nullptr, 1, nullptr, true, true, false, false);
        for (const auto& entry : staged.Entries) {
            auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(entry.FormId);
            const auto handItem = (entry.Flags & kEquipmentSnapshotWeapon) != 0;
            const auto count = entry.Count;
            if ((entry.Flags & kEquipmentSnapshotWorn) != 0)
                manager->EquipObject(&a_actor, object, nullptr, count,
                                     handItem ? RE::TESForm::LookupByID<RE::BGSEquipSlot>(kRightHandEquipSlotFormId) : nullptr,
                                     true, true, false, false);
            if ((entry.Flags & kEquipmentSnapshotWornLeft) != 0 &&
                (handItem || (entry.Flags & kEquipmentSnapshotWorn) == 0))
                manager->EquipObject(&a_actor, object, nullptr, count,
                                     handItem ? RE::TESForm::LookupByID<RE::BGSEquipSlot>(kLeftHandEquipSlotFormId) : nullptr,
                                     true, true, false, false);
        }
        if (needsLeftUnequip || needsRightUnequip || needsShoutUnequip) {
            const auto handle = handles->GetHandleForObject(a_actor.GetFormType(), &a_actor);
            const auto unequipSpell = [vm, handle](RE::SpellItem* aSpell, const RE::MagicSystem::CastingSource aSource) noexcept {
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
                return vm->DispatchMethodCall(handle, RE::BSFixedString("Actor"), RE::BSFixedString("UnequipSpell"),
                                              RE::MakeFunctionArguments(
                                                  static_cast<RE::SpellItem*>(aSpell),
                                                  static_cast<std::int32_t>(aSource)),
                                              callback);
            };
            const auto unequipShout = [vm, handle](RE::TESShout* aShout) noexcept {
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
                return vm->DispatchMethodCall(handle, RE::BSFixedString("Actor"), RE::BSFixedString("UnequipShout"),
                                              RE::MakeFunctionArguments(static_cast<RE::TESShout*>(aShout)), callback);
            };
            if ((needsLeftUnequip && !unequipSpell(currentLeftSpell, RE::MagicSystem::CastingSource::kLeftHand)) ||
                (needsRightUnequip && !unequipSpell(currentRightSpell, RE::MagicSystem::CastingSource::kRightHand)) ||
                (needsShoutUnequip && !unequipShout(currentShout))) {
                restorePreviousState();
                return eraseAnd(CommandStatus::EngineRejected);
            }
        }
        if (leftSpell)
            manager->EquipSpell(&a_actor, leftSpell, RE::TESForm::LookupByID<RE::BGSEquipSlot>(kLeftHandEquipSlotFormId));
        if (rightSpell)
            manager->EquipSpell(&a_actor, rightSpell, RE::TESForm::LookupByID<RE::BGSEquipSlot>(kRightHandEquipSlotFormId));
        if (shout)
            manager->EquipShout(&a_actor, shout);
    } catch (...) {
        restorePreviousState();
        return eraseAnd(CommandStatus::EngineRejected);
    }
    s_stagedEquipment.erase(stagedIt);
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ApplyEquipment(
    const CommandRecord& a_command,
    RE::Actor& a_actor,
    const GameplayActionPayload& a_payload)
{
    const auto action = static_cast<GameplayAction>(a_payload.Action);
    if (action >= GameplayAction::EquipmentSnapshotBegin && action <= GameplayAction::EquipmentSnapshotEnd)
        return ApplyStagedEquipment(a_command, a_actor, a_payload);
    if (a_payload.LocalFormIdA == 0 || a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 ||
        a_payload.ValueA < 1 || a_payload.ValueA > kMaximumItemCount || a_payload.ValueB != 0 ||
        !HasNoScalars(a_payload) || (a_payload.ActionFlags & ~0x7u) != 0)
        return CommandStatus::Malformed;

    const auto* slot = a_payload.LocalFormIdB != 0 ?
                           RE::TESForm::LookupByID<RE::BGSEquipSlot>(a_payload.LocalFormIdB) : nullptr;
    if (a_payload.LocalFormIdB != 0 && !slot)
        return CommandStatus::MissingForm;

    if (action != GameplayAction::EquipForm && action != GameplayAction::UnequipForm)
        return CommandStatus::Malformed;
    if (auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(a_payload.LocalFormIdA)) {
        auto* manager = RE::ActorEquipManager::GetSingleton();
        if (!manager)
            return CommandStatus::Inactive;

        const auto count = static_cast<std::uint32_t>(a_payload.ValueA);
        if (action == GameplayAction::EquipForm) {
            manager->EquipObject(&a_actor, object, nullptr, count, slot, true, true, false, false);
            return CommandStatus::Success;
        }
        return manager->UnequipObject(&a_actor, object, nullptr, count, slot, true, true, false, false) ?
                   CommandStatus::Success :
                   CommandStatus::EngineRejected;
    }

    if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_payload.LocalFormIdA)) {
        if (action == GameplayAction::EquipForm) {
            auto* manager = RE::ActorEquipManager::GetSingleton();
            if (!manager)
                return CommandStatus::Inactive;
            manager->EquipSpell(&a_actor, spell, slot);
            return CommandStatus::Success;
        }

        std::int32_t source{};
        if (a_payload.LocalFormIdB == kLeftHandEquipSlotFormId)
            source = static_cast<std::int32_t>(RE::MagicSystem::CastingSource::kLeftHand);
        else if (a_payload.LocalFormIdB == kRightHandEquipSlotFormId)
            source = static_cast<std::int32_t>(RE::MagicSystem::CastingSource::kRightHand);
        else
            return CommandStatus::Malformed;

        auto* skyrimVm = RE::SkyrimVM::GetSingleton();
        auto* vm = skyrimVm ? skyrimVm->impl.get() : nullptr;
        auto* handles = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!handles)
            return CommandStatus::Inactive;
        const auto handle = handles->GetHandleForObject(a_actor.GetFormType(), &a_actor);
        if (handle == handles->EmptyHandle())
            return CommandStatus::EngineRejected;
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        return vm->DispatchMethodCall(
                   handle,
                   RE::BSFixedString("Actor"),
                   RE::BSFixedString("UnequipSpell"),
                   RE::MakeFunctionArguments(
                       static_cast<RE::SpellItem*>(spell),
                       static_cast<std::int32_t>(source)),
                   callback) ?
                   CommandStatus::Success : CommandStatus::EngineRejected;
    }
    if (auto* shout = RE::TESForm::LookupByID<RE::TESShout>(a_payload.LocalFormIdA)) {
        if (action == GameplayAction::EquipForm) {
            auto* manager = RE::ActorEquipManager::GetSingleton();
            if (!manager)
                return CommandStatus::Inactive;
            manager->EquipShout(&a_actor, shout);
            return CommandStatus::Success;
        }

        if (a_payload.LocalFormIdB != 0)
            return CommandStatus::Malformed;
        auto* skyrimVm = RE::SkyrimVM::GetSingleton();
        auto* vm = skyrimVm ? skyrimVm->impl.get() : nullptr;
        auto* handles = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!handles)
            return CommandStatus::Inactive;
        const auto handle = handles->GetHandleForObject(a_actor.GetFormType(), &a_actor);
        if (handle == handles->EmptyHandle())
            return CommandStatus::EngineRejected;
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        return vm->DispatchMethodCall(
                   handle,
                   RE::BSFixedString("Actor"),
                   RE::BSFixedString("UnequipShout"),
                   RE::MakeFunctionArguments(static_cast<RE::TESShout*>(shout)),
                   callback) ?
                   CommandStatus::Success : CommandStatus::EngineRejected;
    }
    return RE::TESForm::LookupByID(a_payload.LocalFormIdA) ? CommandStatus::Unsupported : CommandStatus::MissingForm;
}

[[nodiscard]] CommandStatus ApplyInventory(
    RE::TESObjectREFR& a_owner,
    const GameplayActionPayload& a_payload)
{
    if (static_cast<GameplayAction>(a_payload.Action) == GameplayAction::InventoryReset) {
        if (a_payload.LocalFormIdA != 0 || a_payload.LocalFormIdB != 0 || a_payload.LocalFormIdC != 0 ||
            a_payload.LocalFormIdD != 0 || a_payload.ValueA != 0 || a_payload.ValueB != 0 ||
            !HasNoScalars(a_payload) || a_payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        const auto inventory = a_owner.GetInventory();
        for (const auto& [object, data] : inventory) {
            const auto count = data.first;
            const auto& entry = data.second;
            if (object && count > 0 && (!entry || !entry->IsQuestObject()))
                a_owner.RemoveItem(object, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
        }
        return CommandStatus::Success;
    }

    if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA == 0 || a_payload.ValueA == 0 ||
        a_payload.ValueA < -kMaximumItemCount || a_payload.ValueA > kMaximumItemCount ||
        a_payload.ValueB != 0 || !HasNoScalars(a_payload) ||
        (a_payload.ActionFlags & ~(kInventoryQuestItem | kInventoryDrop | kInventorySnapshotEntry)) != 0 ||
        ((a_payload.ActionFlags & kInventoryDrop) != 0 && a_payload.ValueA >= 0) ||
        ((a_payload.ActionFlags & kInventorySnapshotEntry) != 0 && a_payload.ValueA <= 0))
        return CommandStatus::Malformed;

    auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(a_payload.LocalFormIdA);
    if (!object)
        return RE::TESForm::LookupByID(a_payload.LocalFormIdA) ? CommandStatus::Unsupported : CommandStatus::MissingForm;

    if (a_payload.ValueA > 0) {
        if ((a_payload.ActionFlags & (kInventorySnapshotEntry | kInventoryQuestItem)) ==
            (kInventorySnapshotEntry | kInventoryQuestItem)) {
            const auto inventory = a_owner.GetInventory();
            const auto existing = inventory.find(object);
            if (existing != inventory.end() && existing->second.first > 0)
                return CommandStatus::Success;
        }
        a_owner.AddObjectToContainer(object, nullptr, a_payload.ValueA, nullptr);
    } else {
        const auto drop = (a_payload.ActionFlags & kInventoryDrop) != 0;
        if (drop && !a_owner.As<RE::Actor>())
            return CommandStatus::Unsupported;
        a_owner.RemoveItem(object, -a_payload.ValueA,
                           drop ? RE::ITEM_REMOVE_REASON::kDropping : RE::ITEM_REMOVE_REASON::kRemove,
                           nullptr, nullptr);
    }
    return CommandStatus::Success;
}

struct StagedInventoryEffect
{
    std::uint32_t EffectFormId{};
    std::int32_t Area{};
    std::int32_t Duration{};
    float Magnitude{};
    float RawCost{};
};

struct StagedInventoryEntry
{
    std::uint32_t FormId{};
    std::uint32_t ItemFlags{};
    std::uint32_t FormType{};
    std::int32_t Count{};
    std::uint32_t EnchantmentFormId{};
    std::uint32_t PoisonFormId{};
    std::uint32_t SoulLevel{};
    std::uint32_t ExpectedEffects{};
    std::int32_t EnchantmentCharge{};
    std::int32_t PoisonCount{};
    float Charge{};
    float Health{};
    std::uint32_t ExtraFlags{};
    bool HasExtra{};
    std::vector<StagedInventoryEffect> Effects{};
};

struct InventoryTransactionKey
{
    std::uint64_t ServerInstanceNonce{};
    std::uint64_t ConnectionGeneration{};
    std::uint64_t LifecycleEpoch{};
    std::uint64_t EntityId{};
    std::uint32_t EntityGeneration{};
    std::uint64_t SequenceId{};
    std::uint64_t TargetHandle{};
    std::uint32_t TargetLocalFormId{};
    std::uint16_t Domain{};

    [[nodiscard]] bool operator==(const InventoryTransactionKey& a_rhs) const noexcept = default;
};

struct InventoryTransactionKeyHash
{
    [[nodiscard]] std::size_t operator()(const InventoryTransactionKey& a_key) const noexcept
    {
        std::size_t value = static_cast<std::size_t>(a_key.ServerInstanceNonce);
        const auto mix = [&value](const std::uint64_t a_component) noexcept {
            value ^= static_cast<std::size_t>(a_component) + 0x9E3779B9u + (value << 6u) + (value >> 2u);
        };
        mix(a_key.ConnectionGeneration);
        mix(a_key.LifecycleEpoch);
        mix(a_key.EntityId);
        mix(a_key.EntityGeneration);
        mix(a_key.SequenceId);
        mix(a_key.TargetHandle);
        mix(a_key.TargetLocalFormId);
        mix(a_key.Domain);
        return value;
    }
};

struct StagedInventoryTransaction
{
    InventoryTransactionKey Key{};
    std::uint64_t LastActionId{};
    std::uint16_t ExpectedItems{};
    std::uint16_t TotalEffects{};
    std::uint16_t EffectsRemaining{};
    bool Reset{};
    bool HasOpenItemExtra{};
    CommandStatus Failure{CommandStatus::Success};
    std::vector<StagedInventoryEntry> Entries{};
};

struct InventoryRemoval
{
    RE::TESBoundObject* Object{};
    RE::ExtraDataList* ExtraList{};
    std::int32_t Count{};
    bool Drop{};
};

// This plan exists only for the current game-thread application call. It
// reserves actual stacks across every negative entry before any removal runs.
struct InventoryAvailability
{
    RE::TESBoundObject* Object{};
    RE::ExtraDataList* ExtraList{};
    std::int32_t Remaining{};
};

struct PreparedInventoryEntry
{
    const StagedInventoryEntry* Staged{};
    RE::TESBoundObject* Object{};
    RE::EnchantmentItem* Enchantment{};
    RE::AlchemyItem* Poison{};
    RE::BSTArray<RE::Effect> DynamicEffects{};
    std::vector<InventoryRemoval> Removals{};
    std::unique_ptr<RE::ExtraDataList> AdditionExtra{};
    std::int32_t DropRemovalCount{};
    std::int32_t AdditionCount{};
    std::int64_t ExpectedFinalCount{};
};

struct CoalescedInventoryEntry
{
    StagedInventoryEntry Entry{};
    std::int64_t Count{};
    std::int64_t DropRemovalCount{};
};

class ScopedInventoryBaselineRefresh final
{
public:
    explicit ScopedInventoryBaselineRefresh(const std::uint32_t a_ownerFormId) noexcept : m_ownerFormId(a_ownerFormId) {}

    void MarkNativeMutation() noexcept { m_nativeMutationStarted = true; }

    ~ScopedInventoryBaselineRefresh() noexcept
    {
        if (m_nativeMutationStarted)
            LocalGameplayCapture::RefreshInventoryBaseline(m_ownerFormId);
    }

private:
    std::uint32_t m_ownerFormId{};
    bool m_nativeMutationStarted{};
};

std::unordered_map<InventoryTransactionKey, StagedInventoryTransaction, InventoryTransactionKeyHash>
    s_stagedInventory{};

[[nodiscard]] InventoryTransactionKey InventoryKey(const CommandRecord& a_command) noexcept
{
    const auto& identity = a_command.Header.Identity;
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    return {identity.ServerInstanceNonce, identity.ConnectionGeneration, identity.LifecycleEpoch,
            identity.EntityId, identity.EntityGeneration, identity.SequenceId, payload.TargetHandle.Value,
            payload.TargetLocalFormId, payload.Domain};
}

[[nodiscard]] bool SameInventoryTarget(
    const InventoryTransactionKey& a_left, const InventoryTransactionKey& a_right) noexcept
{
    return a_left.ServerInstanceNonce == a_right.ServerInstanceNonce &&
           a_left.ConnectionGeneration == a_right.ConnectionGeneration &&
           a_left.LifecycleEpoch == a_right.LifecycleEpoch &&
           a_left.TargetHandle == a_right.TargetHandle &&
           a_left.TargetLocalFormId == a_right.TargetLocalFormId && a_left.Domain == a_right.Domain;
}

[[nodiscard]] bool SameInventorySessionLifecycle(
    const InventoryTransactionKey& a_left, const InventoryTransactionKey& a_right) noexcept
{
    return a_left.ServerInstanceNonce == a_right.ServerInstanceNonce &&
           a_left.ConnectionGeneration == a_right.ConnectionGeneration &&
           a_left.LifecycleEpoch == a_right.LifecycleEpoch;
}

[[nodiscard]] bool IsInventoryKeyLess(
    const InventoryTransactionKey& a_left, const InventoryTransactionKey& a_right) noexcept
{
    if (a_left.ServerInstanceNonce != a_right.ServerInstanceNonce)
        return a_left.ServerInstanceNonce < a_right.ServerInstanceNonce;
    if (a_left.ConnectionGeneration != a_right.ConnectionGeneration)
        return a_left.ConnectionGeneration < a_right.ConnectionGeneration;
    if (a_left.LifecycleEpoch != a_right.LifecycleEpoch)
        return a_left.LifecycleEpoch < a_right.LifecycleEpoch;
    if (a_left.EntityId != a_right.EntityId)
        return a_left.EntityId < a_right.EntityId;
    if (a_left.EntityGeneration != a_right.EntityGeneration)
        return a_left.EntityGeneration < a_right.EntityGeneration;
    if (a_left.SequenceId != a_right.SequenceId)
        return a_left.SequenceId < a_right.SequenceId;
    if (a_left.TargetHandle != a_right.TargetHandle)
        return a_left.TargetHandle < a_right.TargetHandle;
    if (a_left.TargetLocalFormId != a_right.TargetLocalFormId)
        return a_left.TargetLocalFormId < a_right.TargetLocalFormId;
    return a_left.Domain < a_right.Domain;
}

[[nodiscard]] CommandStatus FormLookupStatus(const std::uint32_t a_formId) noexcept
{
    return RE::TESForm::LookupByID(a_formId) ? CommandStatus::Unsupported : CommandStatus::MissingForm;
}

[[nodiscard]] CommandStatus ValidateInventoryBaseForm(
    const std::uint32_t a_formId, const std::uint32_t a_flags, std::uint32_t& ar_formType) noexcept
{
    const auto classification = kInventoryTransactionWeapon | kInventoryTransactionAmmo;
    if ((a_flags & classification) == classification)
        return CommandStatus::Malformed;
    auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(a_formId);
    if (!object)
        return FormLookupStatus(a_formId);

    const auto formType = object->GetFormType();
    const bool weapon = formType == RE::FormType::Weapon;
    const bool ammo = formType == RE::FormType::Ammo;
    if (weapon != ((a_flags & kInventoryTransactionWeapon) != 0) ||
        ammo != ((a_flags & kInventoryTransactionAmmo) != 0))
        return CommandStatus::Malformed;

    ar_formType = static_cast<std::uint32_t>(formType);
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ValidateInventoryExtra(
    const GameplayActionPayload& a_payload, const StagedInventoryEntry& a_item,
    const StagedInventoryTransaction& a_transaction) noexcept
{
    const bool hasEnchantment = a_payload.LocalFormIdA != 0;
    const bool dynamicEnchantment = a_payload.LocalFormIdA == kInventoryTransactionDynamicEnchantmentFormId;
    const bool baseIsWeapon = a_item.FormType == static_cast<std::uint32_t>(RE::FormType::Weapon);
    const bool baseIsArmor = a_item.FormType == static_cast<std::uint32_t>(RE::FormType::Armor);
    if (a_payload.LocalFormIdC > 5 || a_payload.LocalFormIdD > kMaximumInventoryTransactionEffects ||
        a_payload.ValueA < 0 || a_payload.ValueA > std::numeric_limits<std::uint16_t>::max() ||
        a_payload.ValueB < 0 || a_payload.ValueB > kMaximumInventoryTransactionValue ||
        a_payload.ScalarA < 0.0F || a_payload.ScalarA > static_cast<float>(kMaximumInventoryTransactionValue) ||
        a_payload.ScalarB < 0.0F || a_payload.ScalarB > static_cast<float>(kMaximumInventoryTransactionValue) ||
        a_payload.ScalarC != 0.0F || a_payload.ScalarD != 0.0F ||
        (a_payload.ActionFlags & ~kInventoryTransactionExtraKnownFlags) != 0 ||
        a_transaction.TotalEffects > kMaximumInventoryTransactionEffects - a_payload.LocalFormIdD)
        return CommandStatus::Malformed;

    if (!hasEnchantment) {
        if (a_payload.ValueA != 0 || a_payload.LocalFormIdD != 0 || a_payload.ActionFlags != 0)
            return CommandStatus::Malformed;
    } else {
        if (dynamicEnchantment) {
            if (a_payload.LocalFormIdD == 0 || (!baseIsWeapon && !baseIsArmor))
                return CommandStatus::Malformed;
        } else {
            if (a_payload.LocalFormIdD != 0)
                return CommandStatus::Malformed;
            if (!RE::TESForm::LookupByID<RE::EnchantmentItem>(a_payload.LocalFormIdA))
                return FormLookupStatus(a_payload.LocalFormIdA);
        }
        if (((a_payload.ActionFlags & kInventoryTransactionEnchantIsWeapon) != 0) != baseIsWeapon)
            return CommandStatus::Malformed;
    }

    if ((a_payload.LocalFormIdB == 0) != (a_payload.ValueB == 0))
        return CommandStatus::Malformed;
    if (a_payload.LocalFormIdB != 0 && !RE::TESForm::LookupByID<RE::AlchemyItem>(a_payload.LocalFormIdB))
        return FormLookupStatus(a_payload.LocalFormIdB);
    return CommandStatus::Success;
}

[[nodiscard]] bool SameInventoryEffects(
    const std::vector<StagedInventoryEffect>& a_left,
    const std::vector<StagedInventoryEffect>& a_right) noexcept
{
    return a_left.size() == a_right.size() && std::equal(
        a_left.begin(), a_left.end(), a_right.begin(), [](const auto& a_lhs, const auto& a_rhs) {
            return a_lhs.EffectFormId == a_rhs.EffectFormId && a_lhs.Area == a_rhs.Area &&
                   a_lhs.Duration == a_rhs.Duration && a_lhs.Magnitude == a_rhs.Magnitude &&
                   a_lhs.RawCost == a_rhs.RawCost;
        });
}

[[nodiscard]] bool SameInventoryMetadata(
    const StagedInventoryEntry& a_left, const StagedInventoryEntry& a_right,
    const bool a_ignoreQuestItem = false) noexcept
{
    const auto ignoredItemFlags = kInventoryDrop |
        (a_ignoreQuestItem ? kInventoryTransactionQuestItem : 0u);
    return a_left.FormId == a_right.FormId &&
           (a_left.ItemFlags & ~ignoredItemFlags) == (a_right.ItemFlags & ~ignoredItemFlags) &&
           a_left.FormType == a_right.FormType &&
           a_left.EnchantmentFormId == a_right.EnchantmentFormId &&
           a_left.PoisonFormId == a_right.PoisonFormId && a_left.SoulLevel == a_right.SoulLevel &&
           a_left.ExpectedEffects == a_right.ExpectedEffects &&
           a_left.EnchantmentCharge == a_right.EnchantmentCharge &&
           a_left.PoisonCount == a_right.PoisonCount && a_left.Charge == a_right.Charge &&
           a_left.Health == a_right.Health && a_left.ExtraFlags == a_right.ExtraFlags &&
           a_left.HasExtra == a_right.HasExtra && SameInventoryEffects(a_left.Effects, a_right.Effects);
}

[[nodiscard]] bool CheckedAddInventoryCount(
    const std::int64_t a_left, const std::int64_t a_right, std::int64_t& ar_sum) noexcept
{
    if ((a_right > 0 && a_left > std::numeric_limits<std::int64_t>::max() - a_right) ||
        (a_right < 0 && a_left < std::numeric_limits<std::int64_t>::min() - a_right))
        return false;
    ar_sum = a_left + a_right;
    return true;
}

[[nodiscard]] CommandStatus CoalesceInventoryEntries(
    const StagedInventoryTransaction& a_transaction,
    std::vector<CoalescedInventoryEntry>& ar_entries)
{
    ar_entries.clear();
    ar_entries.reserve(a_transaction.Entries.size());
    for (const auto& entry : a_transaction.Entries) {
        auto found = std::find_if(
            ar_entries.begin(), ar_entries.end(), [&entry, &a_transaction](const CoalescedInventoryEntry& a_existing) {
                return SameInventoryMetadata(a_existing.Entry, entry, a_transaction.Reset);
            });
        if (found == ar_entries.end()) {
            CoalescedInventoryEntry coalesced{};
            coalesced.Entry = entry;
            coalesced.Entry.ItemFlags &= ~(kInventoryDrop |
                                           (a_transaction.Reset ? kInventoryTransactionQuestItem : 0u));
            coalesced.Count = entry.Count;
            coalesced.DropRemovalCount =
                entry.Count < 0 && (entry.ItemFlags & kInventoryDrop) != 0 ? -static_cast<std::int64_t>(entry.Count) : 0;
            ar_entries.push_back(std::move(coalesced));
            continue;
        }

        if (!CheckedAddInventoryCount(found->Count, entry.Count, found->Count))
            return CommandStatus::EngineRejected;
        if (entry.Count < 0 && (entry.ItemFlags & kInventoryDrop) != 0 &&
            !CheckedAddInventoryCount(
                found->DropRemovalCount, -static_cast<std::int64_t>(entry.Count), found->DropRemovalCount))
            return CommandStatus::EngineRejected;
    }

    for (auto it = ar_entries.begin(); it != ar_entries.end();) {
        if (it->Count < -kMaximumInventoryTransactionValue ||
            it->Count > kMaximumInventoryTransactionValue)
            return CommandStatus::EngineRejected;
        if (!a_transaction.Reset && it->Count == 0) {
            it = ar_entries.erase(it);
            continue;
        }
        if (a_transaction.Reset && it->Count <= 0)
            return CommandStatus::EngineRejected;
        it->Entry.Count = static_cast<std::int32_t>(it->Count);
        it->DropRemovalCount = it->Count < 0 ?
            std::min(it->DropRemovalCount, -it->Count) : 0;
        ++it;
    }
    return CommandStatus::Success;
}

[[nodiscard]] bool HasNoSemanticInventoryExtra(
    const StagedInventoryEntry& a_entry, const bool a_ignoreQuestItem = false) noexcept
{
    const auto metadataFlags = kInventoryTransactionWorn | kInventoryTransactionWornLeft |
        (a_ignoreQuestItem ? 0u : kInventoryTransactionQuestItem);
    return (a_entry.ItemFlags & metadataFlags) == 0 &&
           a_entry.EnchantmentFormId == 0 && a_entry.PoisonFormId == 0 && a_entry.SoulLevel == 0 &&
           a_entry.Charge == 0.0F && a_entry.Health == 0.0F && a_entry.ExtraFlags == 0 &&
           a_entry.Effects.empty();
}

[[nodiscard]] bool HasOnlyRepresentableInventoryMetadata(
    const RE::ExtraDataList& a_extra, const bool a_questItem) noexcept
{
    for (const auto& data : a_extra) {
        switch (data.GetType()) {
        case RE::ExtraDataType::kCount:
        case RE::ExtraDataType::kCharge:
        case RE::ExtraDataType::kHealth:
        case RE::ExtraDataType::kPoison:
        case RE::ExtraDataType::kWorn:
        case RE::ExtraDataType::kWornLeft:
        case RE::ExtraDataType::kEnchantment:
        case RE::ExtraDataType::kSoul:
        case RE::ExtraDataType::kUniqueID:
            break;
        case RE::ExtraDataType::kAliasInstanceArray:
            if (!a_questItem)
                return false;
            break;
        default:
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SameDynamicEnchantment(
    const RE::EnchantmentItem& a_enchantment, const PreparedInventoryEntry& a_entry) noexcept
{
    if (!a_enchantment.IsDynamicForm() || a_enchantment.effects.size() != a_entry.DynamicEffects.size())
        return false;
    for (std::size_t index = 0; index < a_entry.DynamicEffects.size(); ++index) {
        const auto* actual = a_enchantment.effects[index];
        const auto& expected = a_entry.DynamicEffects[index];
        if (!actual || actual->baseEffect != expected.baseEffect ||
            actual->effectItem.magnitude != expected.effectItem.magnitude ||
            actual->effectItem.area != expected.effectItem.area ||
            actual->effectItem.duration != expected.effectItem.duration || actual->cost != expected.cost)
            return false;
    }
    return true;
}

[[nodiscard]] bool MatchesInventoryMetadata(
    RE::ExtraDataList* const ap_extra, const PreparedInventoryEntry& a_entry,
    const bool a_ignoreQuestItem = false) noexcept
{
    const auto& item = *a_entry.Staged;
    if (!ap_extra)
        return HasNoSemanticInventoryExtra(item, a_ignoreQuestItem);
    const bool questItem = ap_extra->HasQuestObjectAlias();
    if ((!a_ignoreQuestItem && questItem != ((item.ItemFlags & kInventoryTransactionQuestItem) != 0)) ||
        !HasOnlyRepresentableInventoryMetadata(*ap_extra, questItem) ||
        ap_extra->HasType<RE::ExtraWorn>() != ((item.ItemFlags & kInventoryTransactionWorn) != 0) ||
        ap_extra->HasType<RE::ExtraWornLeft>() != ((item.ItemFlags & kInventoryTransactionWornLeft) != 0))
        return false;

    const auto* charge = ap_extra->GetByType<RE::ExtraCharge>();
    const auto* health = ap_extra->GetByType<RE::ExtraHealth>();
    const auto* soul = ap_extra->GetByType<RE::ExtraSoul>();
    const auto* poison = ap_extra->GetByType<RE::ExtraPoison>();
    const auto* enchantment = ap_extra->GetByType<RE::ExtraEnchantment>();
    if ((charge != nullptr) != (item.Charge != 0.0F) || (charge && charge->charge != item.Charge) ||
        (health != nullptr) != (item.Health != 0.0F) || (health && health->health != item.Health) ||
        (soul != nullptr) != (item.SoulLevel != 0) ||
        (soul && static_cast<std::uint32_t>(soul->soul.get()) != item.SoulLevel) ||
        (poison != nullptr) != (a_entry.Poison != nullptr) ||
        (poison && (poison->poison != a_entry.Poison || poison->count != static_cast<std::uint32_t>(item.PoisonCount))) ||
        (enchantment != nullptr) != (item.EnchantmentFormId != 0))
        return false;
    if (!enchantment)
        return true;
    if (enchantment->charge != static_cast<std::uint16_t>(item.EnchantmentCharge) ||
        enchantment->removeOnUnequip != ((item.ExtraFlags & kInventoryTransactionEnchantRemoveUnequip) != 0))
        return false;
    if (!enchantment->enchantment)
        return false;
    return item.EnchantmentFormId == kInventoryTransactionDynamicEnchantmentFormId ?
               SameDynamicEnchantment(*enchantment->enchantment, a_entry) :
               enchantment->enchantment == a_entry.Enchantment;
}

[[nodiscard]] CommandStatus CountInventoryMetadata(
    RE::TESObjectREFR& a_owner, const PreparedInventoryEntry& a_prepared,
    const bool a_ignoreQuestItem, const bool a_preservedQuestOnly, std::int64_t& ar_count)
{
    ar_count = 0;
    const auto inventory = a_owner.GetInventory();
    const auto found = inventory.find(a_prepared.Object);
    if (found == inventory.end())
        return CommandStatus::Success;
    if (found->second.first <= 0)
        return CommandStatus::EngineRejected;

    const auto* inventoryEntry = found->second.second.get();
    std::int64_t plainRemainder = found->second.first;
    const auto appendIfMatching = [&a_prepared, a_ignoreQuestItem, a_preservedQuestOnly,
                                   &ar_count](RE::ExtraDataList* const ap_extra, const std::int64_t a_count) {
        const bool questItem = ap_extra && ap_extra->HasQuestObjectAlias();
        if (a_count > 0 && (!a_preservedQuestOnly || questItem) &&
            MatchesInventoryMetadata(ap_extra, a_prepared, a_ignoreQuestItem))
            return CheckedAddInventoryCount(ar_count, a_count, ar_count);
        return true;
    };
    if (inventoryEntry && inventoryEntry->extraLists) {
        for (auto* extra : *inventoryEntry->extraLists) {
            if (!extra)
                return CommandStatus::EngineRejected;
            const auto* count = extra->GetByType<RE::ExtraCount>();
            const auto stackCount = count ? static_cast<std::int64_t>(count->count) : 1;
            if (stackCount <= 0 || stackCount > plainRemainder)
                return CommandStatus::EngineRejected;
            plainRemainder -= stackCount;
            if (!appendIfMatching(extra, stackCount))
                return CommandStatus::EngineRejected;
        }
    }
    if (plainRemainder < 0)
        return CommandStatus::EngineRejected;
    if (!appendIfMatching(nullptr, plainRemainder))
        return CommandStatus::EngineRejected;
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus VerifyResetInventory(
    RE::TESObjectREFR& a_owner, const std::vector<PreparedInventoryEntry>& a_prepared)
{
    const auto inventory = a_owner.GetInventory();
    for (const auto& [object, data] : inventory) {
        if (!object || data.first <= 0)
            return CommandStatus::EngineRejected;
        const auto* inventoryEntry = data.second.get();
        std::int64_t plainRemainder = data.first;
        const auto isExpected = [&a_prepared, object](RE::ExtraDataList* const ap_extra) {
            return std::any_of(a_prepared.begin(), a_prepared.end(), [object, ap_extra](const auto& a_entry) {
                return a_entry.Staged->Count > 0 && a_entry.Object == object &&
                       MatchesInventoryMetadata(ap_extra, a_entry, true);
            });
        };
        const auto verifyStack = [&isExpected](RE::ExtraDataList* const ap_extra) {
            const bool preservedQuest = ap_extra && ap_extra->HasQuestObjectAlias();
            return preservedQuest || isExpected(ap_extra);
        };
        if (inventoryEntry && inventoryEntry->extraLists) {
            for (auto* extra : *inventoryEntry->extraLists) {
                if (!extra)
                    return CommandStatus::EngineRejected;
                const auto* count = extra->GetByType<RE::ExtraCount>();
                const auto stackCount = count ? static_cast<std::int64_t>(count->count) : 1;
                if (stackCount <= 0 || stackCount > plainRemainder || !verifyStack(extra))
                    return CommandStatus::EngineRejected;
                plainRemainder -= stackCount;
            }
        }
        if (plainRemainder < 0 || (plainRemainder != 0 && !verifyStack(nullptr)))
            return CommandStatus::EngineRejected;
    }
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ResolveInventoryOwner(
    const CommandRecord& a_command, RE::TESObjectREFR*& arp_owner) noexcept
{
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    if (payload.TargetHandle.Value != 0) {
        RE::NiPointer<RE::Actor> actor;
        const auto status = AvatarManager::Get().ResolveGameplayActor(a_command, actor);
        if (status != CommandStatus::Success)
            return status;
        arp_owner = actor.get();
        return arp_owner ? CommandStatus::Success : CommandStatus::Inactive;
    }

    if (a_command.Header.Identity.EntityId != 0) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(payload.TargetLocalFormId);
        if (!actor)
            return FormLookupStatus(payload.TargetLocalFormId);
        arp_owner = actor;
        return CommandStatus::Success;
    }

    arp_owner = RE::TESForm::LookupByID<RE::TESObjectREFR>(payload.TargetLocalFormId);
    return arp_owner ? CommandStatus::Success : FormLookupStatus(payload.TargetLocalFormId);
}

[[nodiscard]] CommandStatus PrepareInventoryEntry(
    const CoalescedInventoryEntry& a_entry, PreparedInventoryEntry& ar_prepared)
{
    ar_prepared.Staged = &a_entry.Entry;
    ar_prepared.DropRemovalCount = static_cast<std::int32_t>(a_entry.DropRemovalCount);
    ar_prepared.Object = RE::TESForm::LookupByID<RE::TESBoundObject>(a_entry.Entry.FormId);
    if (!ar_prepared.Object)
        return FormLookupStatus(a_entry.Entry.FormId);
    if (a_entry.Entry.EnchantmentFormId != 0 &&
        a_entry.Entry.EnchantmentFormId != kInventoryTransactionDynamicEnchantmentFormId) {
        ar_prepared.Enchantment = RE::TESForm::LookupByID<RE::EnchantmentItem>(a_entry.Entry.EnchantmentFormId);
        if (!ar_prepared.Enchantment)
            return FormLookupStatus(a_entry.Entry.EnchantmentFormId);
    }
    if (a_entry.Entry.PoisonFormId != 0) {
        ar_prepared.Poison = RE::TESForm::LookupByID<RE::AlchemyItem>(a_entry.Entry.PoisonFormId);
        if (!ar_prepared.Poison)
            return FormLookupStatus(a_entry.Entry.PoisonFormId);
    }
    if (a_entry.Entry.EnchantmentFormId == kInventoryTransactionDynamicEnchantmentFormId) {
        ar_prepared.DynamicEffects.reserve(static_cast<std::uint32_t>(a_entry.Entry.Effects.size()));
        for (const auto& effect : a_entry.Entry.Effects) {
            auto* setting = RE::TESForm::LookupByID<RE::EffectSetting>(effect.EffectFormId);
            if (!setting)
                return FormLookupStatus(effect.EffectFormId);
            RE::Effect nativeEffect{};
            nativeEffect.baseEffect = setting;
            nativeEffect.effectItem.magnitude = effect.Magnitude;
            nativeEffect.effectItem.area = static_cast<std::uint32_t>(effect.Area);
            nativeEffect.effectItem.duration = static_cast<std::uint32_t>(effect.Duration);
            nativeEffect.cost = effect.RawCost;
            ar_prepared.DynamicEffects.push_back(std::move(nativeEffect));
        }
    }
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus BuildRemovalPlan(
    RE::TESObjectREFR& a_owner, PreparedInventoryEntry& ar_prepared,
    std::vector<InventoryAvailability>& ar_availability)
{
    const auto requested = -ar_prepared.Staged->Count;
    const auto inventory = a_owner.GetInventory();
    const auto found = inventory.find(ar_prepared.Object);
    if (found == inventory.end() || found->second.first <= 0)
        return CommandStatus::EngineRejected;

    std::int64_t reserved{};
    ar_prepared.Removals.clear();
    const auto appendIfMatching = [&](RE::ExtraDataList* const ap_extra, const std::int32_t a_count) {
        if (a_count <= 0 || !MatchesInventoryMetadata(ap_extra, ar_prepared))
            return;
        auto available = std::find_if(
            ar_availability.begin(), ar_availability.end(),
            [object = ar_prepared.Object, ap_extra](const InventoryAvailability& a_stack) {
                return a_stack.Object == object && a_stack.ExtraList == ap_extra;
            });
        if (available == ar_availability.end()) {
            ar_availability.push_back({ar_prepared.Object, ap_extra, a_count});
            available = std::prev(ar_availability.end());
        }
        if (available->Remaining <= 0)
            return;
        const auto remaining = requested - static_cast<std::int32_t>(reserved);
        const auto reservedCount = std::min({available->Remaining, a_count, remaining});
        if (reservedCount <= 0)
            return;
        available->Remaining -= reservedCount;
        reserved += reservedCount;
        const auto dropCount = std::min(reservedCount, ar_prepared.DropRemovalCount);
        const auto removeCount = reservedCount - dropCount;
        if (removeCount > 0)
            ar_prepared.Removals.push_back({ar_prepared.Object, ap_extra, removeCount, false});
        if (dropCount > 0) {
            ar_prepared.Removals.push_back({ar_prepared.Object, ap_extra, dropCount, true});
            ar_prepared.DropRemovalCount -= dropCount;
        }
    };

    std::int64_t plainRemainder = found->second.first;
    const auto* entry = found->second.second.get();
    if (entry && entry->extraLists) {
        for (auto* extra : *entry->extraLists) {
            if (!extra)
                return CommandStatus::EngineRejected;
            const auto* count = extra->GetByType<RE::ExtraCount>();
            const auto stackCount = count ? static_cast<std::int64_t>(count->count) : 1;
            if (stackCount <= 0 || stackCount > plainRemainder)
                return CommandStatus::EngineRejected;
            plainRemainder -= stackCount;
            appendIfMatching(extra, static_cast<std::int32_t>(stackCount));
        }
    }
    if (plainRemainder < 0)
        return CommandStatus::EngineRejected;
    appendIfMatching(nullptr, static_cast<std::int32_t>(plainRemainder));
    return reserved == requested ? CommandStatus::Success : CommandStatus::EngineRejected;
}

[[nodiscard]] CommandStatus BuildResetRemovalPlan(
    RE::TESObjectREFR& a_owner, std::vector<InventoryRemoval>& ar_removals)
{
    const auto inventory = a_owner.GetInventory();
    for (const auto& [object, data] : inventory) {
        if (!object || data.first <= 0)
            return CommandStatus::EngineRejected;
        std::int64_t plainRemainder = data.first;
        const auto* entry = data.second.get();
        if (entry && entry->extraLists) {
            for (auto* extra : *entry->extraLists) {
                if (!extra)
                    return CommandStatus::EngineRejected;
                const auto* count = extra->GetByType<RE::ExtraCount>();
                const auto stackCount = count ? static_cast<std::int64_t>(count->count) : 1;
                if (stackCount <= 0 || stackCount > plainRemainder)
                    return CommandStatus::EngineRejected;
                plainRemainder -= stackCount;
                if (!extra->HasQuestObjectAlias())
                    ar_removals.push_back({object, extra, static_cast<std::int32_t>(stackCount), false});
            }
        }
        if (plainRemainder < 0)
            return CommandStatus::EngineRejected;
        if (plainRemainder != 0)
            ar_removals.push_back({object, nullptr, static_cast<std::int32_t>(plainRemainder), false});
    }
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus BuildAdditionExtra(PreparedInventoryEntry& ar_prepared)
{
    const auto& item = *ar_prepared.Staged;
    const bool needsExtra = item.Charge != 0.0F || item.Health != 0.0F || item.SoulLevel != 0 ||
                            ar_prepared.Poison != nullptr || ar_prepared.Enchantment != nullptr ||
                            (item.ItemFlags & (kInventoryTransactionWorn | kInventoryTransactionWornLeft)) != 0;
    if (!needsExtra)
        return CommandStatus::Success;

    auto extra = std::make_unique<RE::ExtraDataList>();
    const auto add = [&extra](RE::BSExtraData* ap_data) noexcept {
        if (!ap_data || !extra->Add(ap_data)) {
            delete ap_data;
            return false;
        }
        return true;
    };
    auto* charge = item.Charge != 0.0F ? new RE::ExtraCharge{} : nullptr;
    if (charge)
        charge->charge = item.Charge;
    if ((charge && !add(charge)) ||
        (item.Health != 0.0F && !add(new RE::ExtraHealth{item.Health})) ||
        (item.SoulLevel != 0 && !add(new RE::ExtraSoul{static_cast<RE::SOUL_LEVEL>(item.SoulLevel)})) ||
        (ar_prepared.Poison && !add(new RE::ExtraPoison{ar_prepared.Poison, item.PoisonCount})) ||
        (ar_prepared.Enchantment && !add(new RE::ExtraEnchantment{
                                      ar_prepared.Enchantment, static_cast<std::uint16_t>(item.EnchantmentCharge),
                                      (item.ExtraFlags & kInventoryTransactionEnchantRemoveUnequip) != 0})) ||
        ((item.ItemFlags & kInventoryTransactionWorn) != 0 && !add(new RE::ExtraWorn{})) ||
        ((item.ItemFlags & kInventoryTransactionWornLeft) != 0 && !add(new RE::ExtraWornLeft{})))
        return CommandStatus::EngineRejected;
    ar_prepared.AdditionExtra = std::move(extra);
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ApplyCompleteInventoryTransaction(
    const CommandRecord& a_command, const StagedInventoryTransaction& a_transaction)
{
    RE::TESObjectREFR* owner{};
    const auto ownerStatus = ResolveInventoryOwner(a_command, owner);
    if (ownerStatus != CommandStatus::Success)
        return ownerStatus;

    std::vector<CoalescedInventoryEntry> coalesced;
    const auto coalesceStatus = CoalesceInventoryEntries(a_transaction, coalesced);
    if (coalesceStatus != CommandStatus::Success)
        return coalesceStatus;

    std::vector<PreparedInventoryEntry> prepared;
    prepared.reserve(coalesced.size());
    for (const auto& entry : coalesced) {
        PreparedInventoryEntry resolved{};
        const auto status = PrepareInventoryEntry(entry, resolved);
        if (status != CommandStatus::Success)
            return status;
        prepared.push_back(std::move(resolved));
    }

    for (auto& entry : prepared) {
        if (a_transaction.Reset) {
            std::int64_t preservedQuestCount{};
            const auto status = CountInventoryMetadata(*owner, entry, true, true, preservedQuestCount);
            if (status != CommandStatus::Success)
                return status;
            const auto expected = static_cast<std::int64_t>(entry.Staged->Count);
            const auto additionCount = expected > preservedQuestCount ? expected - preservedQuestCount : 0;
            entry.ExpectedFinalCount = std::max(expected, preservedQuestCount);
            if (additionCount > std::numeric_limits<std::int32_t>::max() ||
                entry.ExpectedFinalCount > std::numeric_limits<std::int32_t>::max())
                return CommandStatus::EngineRejected;
            entry.AdditionCount = static_cast<std::int32_t>(additionCount);
            continue;
        }

        std::int64_t initialCount{};
        const auto status = CountInventoryMetadata(*owner, entry, false, false, initialCount);
        if (status != CommandStatus::Success)
            return status;
        entry.AdditionCount = entry.Staged->Count > 0 ? entry.Staged->Count : 0;
        if (!CheckedAddInventoryCount(initialCount, entry.Staged->Count, entry.ExpectedFinalCount))
            return CommandStatus::EngineRejected;
        if (entry.ExpectedFinalCount < 0 ||
            entry.ExpectedFinalCount > std::numeric_limits<std::int32_t>::max())
            return CommandStatus::EngineRejected;
    }

    std::vector<InventoryRemoval> resetRemovals;
    if (a_transaction.Reset) {
        const auto status = BuildResetRemovalPlan(*owner, resetRemovals);
        if (status != CommandStatus::Success)
            return status;
    }
    std::vector<InventoryAvailability> removalAvailability;
    for (auto& entry : prepared) {
        if (entry.Staged->Count < 0) {
            const auto status = BuildRemovalPlan(*owner, entry, removalAvailability);
            if (status != CommandStatus::Success)
                return status;
        }
    }

    RE::BGSCreatedObjectManager* createdObjects{};
    for (auto& entry : prepared) {
        if (entry.AdditionCount <= 0 ||
            entry.Staged->EnchantmentFormId != kInventoryTransactionDynamicEnchantmentFormId)
            continue;
        if (!createdObjects)
            createdObjects = RE::BGSCreatedObjectManager::GetSingleton();
        if (!createdObjects)
            return CommandStatus::Inactive;
        const auto isWeapon = (entry.Staged->ExtraFlags & kInventoryTransactionEnchantIsWeapon) != 0;
        entry.Enchantment = isWeapon ? createdObjects->AddWeaponEnchantment(entry.DynamicEffects) :
                                      createdObjects->AddArmorEnchantment(entry.DynamicEffects);
        if (!entry.Enchantment)
            return CommandStatus::EngineRejected;
    }
    for (auto& entry : prepared) {
        if (entry.AdditionCount > 0) {
            const auto status = BuildAdditionExtra(entry);
            if (status != CommandStatus::Success)
                return status;
        }
    }

    LocalGameplayCapture::ScopedRemoteInventorySuppression suppress{};
    ScopedInventoryBaselineRefresh refresh{owner->GetFormID()};
    for (const auto& removal : resetRemovals) {
        refresh.MarkNativeMutation();
        owner->RemoveItem(removal.Object, removal.Count, RE::ITEM_REMOVE_REASON::kRemove, removal.ExtraList, nullptr);
    }
    for (auto& entry : prepared) {
        if (entry.AdditionCount > 0) {
            refresh.MarkNativeMutation();
            owner->AddObjectToContainer(
                entry.Object, entry.AdditionExtra.release(), entry.AdditionCount, nullptr);
            continue;
        }
        for (const auto& removal : entry.Removals) {
            refresh.MarkNativeMutation();
            owner->RemoveItem(removal.Object, removal.Count,
                              removal.Drop ? RE::ITEM_REMOVE_REASON::kDropping : RE::ITEM_REMOVE_REASON::kRemove,
                              removal.ExtraList, nullptr);
        }
    }
    for (const auto& entry : prepared) {
        std::int64_t finalCount{};
        const auto status = CountInventoryMetadata(*owner, entry, a_transaction.Reset, false, finalCount);
        if (status != CommandStatus::Success || finalCount != entry.ExpectedFinalCount)
            return CommandStatus::EngineRejected;
    }
    if (a_transaction.Reset && VerifyResetInventory(*owner, prepared) != CommandStatus::Success)
        return CommandStatus::EngineRejected;
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus StageInventoryTransaction(const CommandRecord& a_command)
{
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    const auto action = static_cast<GameplayAction>(payload.Action);
    const auto key = InventoryKey(a_command);
    if (a_command.Header.Identity.ActionId == 0 || a_command.Header.Identity.SequenceId != 0)
        return CommandStatus::Malformed;
    try {
    const auto beginLayoutValid = [&payload]() noexcept {
        return payload.LocalFormIdA == 0 && payload.LocalFormIdB == 0 && payload.LocalFormIdC == 0 &&
               payload.LocalFormIdD == 0 && payload.ValueA >= 0 &&
               payload.ValueA <= static_cast<std::int32_t>(kMaximumInventoryTransactionItems) &&
               payload.ValueB == 0 && HasNoScalars(payload) &&
               (payload.ActionFlags & ~kInventoryTransactionBeginKnownFlags) == 0;
    };
    if (action == GameplayAction::InventoryTransactionBegin) {
        const bool reset = (payload.ActionFlags & kInventoryTransactionReset) != 0;
        if (!beginLayoutValid() || (payload.ValueA == 0 && !reset))
            return CommandStatus::Malformed;
        for (auto it = s_stagedInventory.begin(); it != s_stagedInventory.end();) {
            if (!SameInventoryTarget(it->first, key)) {
                ++it;
                continue;
            }
            if (it->second.LastActionId >= a_command.Header.Identity.ActionId)
                return CommandStatus::StaleEntity;
            it = s_stagedInventory.erase(it);
        }
        if (s_stagedInventory.size() >= kMaximumStagedInventoryTransactions) {
            auto oldest = s_stagedInventory.end();
            for (auto candidate = s_stagedInventory.begin(); candidate != s_stagedInventory.end(); ++candidate) {
                if (!SameInventorySessionLifecycle(candidate->first, key) ||
                    SameInventoryTarget(candidate->first, key) ||
                    candidate->second.LastActionId >= a_command.Header.Identity.ActionId)
                    continue;
                if (oldest == s_stagedInventory.end() ||
                    candidate->second.LastActionId < oldest->second.LastActionId ||
                    (candidate->second.LastActionId == oldest->second.LastActionId &&
                     IsInventoryKeyLess(candidate->first, oldest->first)))
                    oldest = candidate;
            }
            if (oldest == s_stagedInventory.end())
                return CommandStatus::QueueOverflow;
            s_stagedInventory.erase(oldest);
        }
        StagedInventoryTransaction staged{};
        staged.Key = key;
        staged.LastActionId = a_command.Header.Identity.ActionId;
        staged.ExpectedItems = static_cast<std::uint16_t>(payload.ValueA);
        staged.Reset = reset;
        staged.Entries.reserve(staged.ExpectedItems);
        s_stagedInventory.emplace(key, std::move(staged));
        return CommandStatus::Success;
    }

    const auto found = s_stagedInventory.find(key);
    if (found == s_stagedInventory.end() || found->second.LastActionId == std::numeric_limits<std::uint64_t>::max() ||
        a_command.Header.Identity.ActionId != found->second.LastActionId + 1)
        return CommandStatus::StaleEntity;
    auto& staged = found->second;
    staged.LastActionId = a_command.Header.Identity.ActionId;
    const auto fail = [&staged](const CommandStatus a_status) noexcept {
        if (staged.Failure == CommandStatus::Success)
            staged.Failure = a_status;
        return a_status;
    };
    const auto eraseAnd = [&found](const CommandStatus a_status) noexcept {
        s_stagedInventory.erase(found);
        return a_status;
    };

    if (action == GameplayAction::InventoryTransactionEnd) {
        if (payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            !HasNoScalars(payload) || payload.ActionFlags != 0)
            return eraseAnd(CommandStatus::Malformed);
        if (staged.Failure != CommandStatus::Success)
            return eraseAnd(staged.Failure);
        if (staged.Entries.size() != staged.ExpectedItems || staged.EffectsRemaining != 0 ||
            (!staged.Entries.empty() && !staged.HasOpenItemExtra))
            return eraseAnd(CommandStatus::Malformed);
        try {
            return eraseAnd(ApplyCompleteInventoryTransaction(a_command, staged));
        } catch (...) {
            return eraseAnd(CommandStatus::EngineRejected);
        }
    }

    if (staged.Failure != CommandStatus::Success)
        return staged.Failure;
    if (action == GameplayAction::InventoryTransactionItem) {
        if (staged.Entries.size() >= staged.ExpectedItems ||
            (!staged.Entries.empty() && (!staged.HasOpenItemExtra || staged.EffectsRemaining != 0)) ||
            payload.LocalFormIdA == 0 || payload.LocalFormIdA == kInventoryTransactionDynamicEnchantmentFormId ||
            payload.LocalFormIdB != staged.ExpectedItems || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA == 0 || payload.ValueA < -kMaximumInventoryTransactionValue ||
            payload.ValueA > kMaximumInventoryTransactionValue ||
            (staged.Reset && payload.ValueA < 0) || payload.ValueB != static_cast<std::int32_t>(staged.Entries.size()) ||
            !HasNoScalars(payload) || (payload.ActionFlags & ~kInventoryTransactionItemWireKnownFlags) != 0 ||
            ((payload.ActionFlags & kInventoryDrop) != 0 && (payload.ValueA >= 0 || staged.Reset)))
            return fail(CommandStatus::Malformed);
        std::uint32_t formType{};
        const auto formStatus = ValidateInventoryBaseForm(payload.LocalFormIdA, payload.ActionFlags, formType);
        if (formStatus != CommandStatus::Success)
            return fail(formStatus);
        staged.Entries.push_back({payload.LocalFormIdA, payload.ActionFlags, formType, payload.ValueA});
        staged.HasOpenItemExtra = false;
        return CommandStatus::Success;
    }

    if (action == GameplayAction::InventoryTransactionItemExtra) {
        if (staged.Entries.empty() || staged.HasOpenItemExtra || staged.EffectsRemaining != 0 ||
            staged.Entries.size() - 1 >= staged.ExpectedItems)
            return fail(CommandStatus::Malformed);
        auto& item = staged.Entries.back();
        const auto extraStatus = ValidateInventoryExtra(payload, item, staged);
        if (extraStatus != CommandStatus::Success)
            return fail(extraStatus);
        item.EnchantmentFormId = payload.LocalFormIdA;
        item.PoisonFormId = payload.LocalFormIdB;
        item.SoulLevel = payload.LocalFormIdC;
        item.ExpectedEffects = payload.LocalFormIdD;
        item.EnchantmentCharge = payload.ValueA;
        item.PoisonCount = payload.ValueB;
        item.Charge = payload.ScalarA;
        item.Health = payload.ScalarB;
        item.ExtraFlags = payload.ActionFlags;
        item.HasExtra = true;
        item.Effects.reserve(item.ExpectedEffects);
        staged.TotalEffects = static_cast<std::uint16_t>(staged.TotalEffects + item.ExpectedEffects);
        staged.EffectsRemaining = static_cast<std::uint16_t>(item.ExpectedEffects);
        staged.HasOpenItemExtra = true;
        return CommandStatus::Success;
    }

    if (action != GameplayAction::InventoryTransactionItemEffect || staged.Entries.empty() ||
        !staged.HasOpenItemExtra || staged.EffectsRemaining == 0)
        return fail(CommandStatus::Malformed);
    auto& item = staged.Entries.back();
    if (payload.LocalFormIdA == 0 || payload.LocalFormIdB != staged.Entries.size() - 1 ||
        payload.LocalFormIdC != item.Effects.size() ||
        payload.LocalFormIdD != item.Effects.size() + staged.EffectsRemaining || payload.ValueA < 0 ||
        payload.ValueA > kMaximumInventoryTransactionValue || payload.ValueB < 0 ||
        payload.ValueB > kMaximumInventoryTransactionValue ||
        std::abs(payload.ScalarA) > static_cast<float>(kMaximumInventoryTransactionValue) ||
        payload.ScalarB < 0.0F || payload.ScalarB > static_cast<float>(kMaximumInventoryTransactionValue) ||
        payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
        return fail(CommandStatus::Malformed);
    if (!RE::TESForm::LookupByID<RE::EffectSetting>(payload.LocalFormIdA))
        return fail(FormLookupStatus(payload.LocalFormIdA));
    item.Effects.push_back({payload.LocalFormIdA, payload.ValueA, payload.ValueB, payload.ScalarA, payload.ScalarB});
    --staged.EffectsRemaining;
    return CommandStatus::Success;
    } catch (...) {
        s_stagedInventory.erase(key);
        return CommandStatus::EngineRejected;
    }
}
} // namespace

CommandStatus AnimationAppearanceManager::StageText(
    const CommandRecord& a_command, const std::string_view a_text) noexcept
{
    try {
        if (static_cast<CommandKind>(a_command.Header.Kind) != CommandKind::ApplyGameplayTextChunk ||
            a_command.Header.PayloadSize != kFixedPayloadBytes || a_command.Header.Flags != 0 ||
            a_command.Header.Identity.SequenceId != 0 || a_command.Header.Identity.ActionId == 0)
            return CommandStatus::Malformed;
        const auto& payload = a_command.Payload.ApplyGameplayTextChunk;
        const auto action = static_cast<GameplayAction>(payload.Action);
        if (payload.TargetHandle.Value < kFirstRemoteAvatarHandle ||
            payload.Domain != static_cast<std::uint16_t>(GameplayDomain::Appearance) ||
            payload.Reserved0 != kGameplayTextAppearanceDeferred ||
            (action != GameplayAction::SetName && action != GameplayAction::SetTint))
            return CommandStatus::Malformed;

        const auto found = s_stagedAppearances.find(payload.TargetHandle.Value);
        if (found == s_stagedAppearances.end() ||
            !SameAppearanceIdentity(found->second.Identity, a_command.Header.Identity) ||
            found->second.LastActionId == std::numeric_limits<std::uint64_t>::max() ||
            a_command.Header.Identity.ActionId != found->second.LastActionId + 1)
            return CommandStatus::StaleEntity;
        auto& staged = found->second;
        staged.LastActionId = a_command.Header.Identity.ActionId;
        const auto fail = [&staged](const CommandStatus a_status) noexcept {
            if (staged.Failure == CommandStatus::Success)
                staged.Failure = a_status;
            return a_status;
        };
        if (staged.Failure != CommandStatus::Success)
            return staged.Failure;

        if (action == GameplayAction::SetName) {
            if (payload.AuxiliaryLocalFormId != 0 || a_text.empty() ||
                a_text.size() > kMaximumAppearanceNameBytes || !IsValidUtf8(a_text) || staged.NameSpecified)
                return fail(CommandStatus::Malformed);
            staged.Name.assign(a_text);
            staged.NameSpecified = true;
            return CommandStatus::Success;
        }

        if (payload.AuxiliaryLocalFormId == 0 ||
            payload.AuxiliaryLocalFormId > staged.ExpectedTintCount || !IsSafeTexturePath(a_text))
            return fail(CommandStatus::Malformed);
        auto& tint = staged.Tints[payload.AuxiliaryLocalFormId - 1];
        if (!tint.Present || !tint.PathExpected || tint.PathSpecified)
            return fail(CommandStatus::Malformed);
        tint.TexturePath.assign(a_text);
        tint.PathSpecified = true;
        return CommandStatus::Success;
    } catch (...) {
        return CommandStatus::EngineRejected;
    }
}

CommandStatus AnimationAppearanceManager::Apply(const CommandRecord& a_command) noexcept
{
    try {
        const auto envelope = ValidateEnvelope(a_command);
        if (envelope != CommandStatus::Success)
            return envelope;

        const auto& payload = a_command.Payload.ApplyGameplayAction;
        switch (static_cast<GameplayDomain>(payload.Domain)) {
        case GameplayDomain::Animation:
        case GameplayDomain::Appearance:
        case GameplayDomain::Equipment:
        {
            RE::NiPointer<RE::Actor> actor;
            const auto resolved = AvatarManager::Get().ResolveGameplayActor(a_command, actor);
            if (resolved != CommandStatus::Success)
                return resolved;
            if (static_cast<GameplayDomain>(payload.Domain) == GameplayDomain::Animation)
                return ApplyAnimation(*actor, payload);
            if (static_cast<GameplayDomain>(payload.Domain) == GameplayDomain::Appearance)
                return ApplyAppearance(a_command, *actor, payload);
            return ApplyEquipment(a_command, *actor, payload);
        }
        case GameplayDomain::Inventory:
        {
            if (IsInventoryTransactionAction(static_cast<GameplayAction>(payload.Action)))
                return StageInventoryTransaction(a_command);
            if (payload.TargetHandle.Value != 0) {
                RE::NiPointer<RE::Actor> actor;
                const auto resolved = AvatarManager::Get().ResolveGameplayActor(a_command, actor);
                return resolved == CommandStatus::Success ? ApplyInventory(*actor, payload) : resolved;
            }
            auto* owner = RE::TESForm::LookupByID<RE::TESObjectREFR>(payload.TargetLocalFormId);
            return owner ? ApplyInventory(*owner, payload) : CommandStatus::MissingForm;
        }
        default:
            return CommandStatus::Unsupported;
        }
    } catch (...) {
        return CommandStatus::EngineRejected;
    }
}

void AnimationAppearanceManager::Reset() noexcept
{
    s_stagedAppearances.clear();
    s_appliedAppearances.clear();
    s_partialAppearances.clear();
    s_stagedEquipment.clear();
    s_stagedInventory.clear();
}

void AnimationAppearanceManager::ForgetTarget(const AdapterHandle a_target, RE::TESNPC* const a_npc) noexcept
{
    if (a_target.Value == 0)
        return;
    s_stagedAppearances.erase(a_target.Value);
    s_appliedAppearances.erase(a_target.Value);
    s_partialAppearances.erase(a_target.Value);
    s_stagedEquipment.erase(a_target.Value);
    for (auto it = s_stagedInventory.begin(); it != s_stagedInventory.end();) {
        if (it->first.TargetHandle == a_target.Value)
            it = s_stagedInventory.erase(it);
        else
            ++it;
    }
    if (auto* managed = FindManagedHeadPartBuffer(a_target.Value)) {
        if (a_npc && a_npc->headParts == managed->Buffer) {
            a_npc->headParts = managed->OriginalBuffer;
            a_npc->numHeadParts = managed->OriginalCount;
        }
        RE::free(managed->Buffer);
        *managed = {};
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter
