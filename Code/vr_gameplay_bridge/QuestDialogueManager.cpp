#include "QuestDialogueManager.h"

#include "AvatarManager.h"
#include "LocalGameplayCapture.h"

#include <vr_common/VRCanonicalEntity.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr std::int32_t kQuestStatusStarted = 1;
constexpr std::int32_t kQuestStatusStopped = 2;
constexpr std::uint32_t kMaximumQuestType = 11;
constexpr std::uint64_t kSetWaypointVrRva = 0x06C74D0;
constexpr std::uint64_t kRemoveWaypointVrRva = 0x06C7630;
constexpr std::uint64_t kMoveToVrRva = 0x09E90E0;
constexpr std::array<std::uint8_t, 16> kSetWaypointVrPrologue{
    0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24,
};
constexpr std::array<std::uint8_t, 16> kRemoveWaypointVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8D, 0x05, 0x1F, 0x20, 0x8C,
};
constexpr std::array<std::uint8_t, 16> kMoveToVrPrologue{
    0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
};

template <class T>
[[nodiscard]] T* LookupLocalForm(const std::uint32_t a_formId) noexcept
{
    if (a_formId == 0)
        return nullptr;

    auto* form = RE::TESForm::LookupByID<T>(a_formId);
    return form && form->GetFormID() == a_formId ? form : nullptr;
}

[[nodiscard]] bool HasFiniteScalars(const GameplayActionPayload& a_payload) noexcept
{
    return std::isfinite(a_payload.ScalarA) && std::isfinite(a_payload.ScalarB) &&
           std::isfinite(a_payload.ScalarC) && std::isfinite(a_payload.ScalarD);
}

[[nodiscard]] bool HasZeroTail(const GameplayActionPayload& a_payload) noexcept
{
    return a_payload.Reserved0 == 0 &&
           std::all_of(std::begin(a_payload.ReservedTail), std::end(a_payload.ReservedTail), [](const std::uint8_t a_value) {
               return a_value == 0;
           });
}

[[nodiscard]] bool HasNoTarget(const GameplayActionPayload& a_payload) noexcept
{
    return a_payload.TargetHandle.Value == 0 && a_payload.SecondaryHandle.Value == 0 && a_payload.TargetLocalFormId == 0;
}

[[nodiscard]] bool HasNoEntity(const BridgeIdentity& a_identity) noexcept
{
    return a_identity.EntityId == 0 && a_identity.EntityGeneration == 0;
}

[[nodiscard]] bool HasNoUnusedActionFields(const GameplayActionPayload& a_payload) noexcept
{
    return a_payload.LocalFormIdB == 0 && a_payload.LocalFormIdC == 0 && a_payload.LocalFormIdD == 0 &&
           a_payload.ValueA == 0 && a_payload.ValueB == 0 && a_payload.ScalarA == 0.0f &&
           a_payload.ScalarB == 0.0f && a_payload.ScalarC == 0.0f && a_payload.ScalarD == 0.0f &&
           a_payload.ActionFlags == 0;
}

[[nodiscard]] bool IsQuestStage(const std::int32_t a_stage) noexcept
{
    return a_stage >= 0 && a_stage <= std::numeric_limits<std::uint16_t>::max();
}

[[nodiscard]] CommandStatus ValidateBaseCommand(const CommandRecord& a_command) noexcept
{
    const auto& identity = a_command.Header.Identity;
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    const auto domain = static_cast<GameplayDomain>(payload.Domain);
    const auto action = static_cast<GameplayAction>(payload.Action);

    if (static_cast<CommandKind>(a_command.Header.Kind) != CommandKind::ApplyGameplayAction ||
        a_command.Header.PayloadSize != kFixedPayloadBytes || a_command.Header.Flags != 0 || identity.Reserved0 != 0 ||
        identity.SequenceId != 0 || identity.ActionId == 0 || !HasZeroTail(payload) || !HasFiniteScalars(payload) ||
        !IsActionInDomain(domain, action))
        return CommandStatus::Malformed;

    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ResolveQuest(const GameplayActionPayload& a_payload, RE::TESQuest*& ar_quest) noexcept
{
    ar_quest = LookupLocalForm<RE::TESQuest>(a_payload.TargetLocalFormId);
    return ar_quest ? CommandStatus::Success : CommandStatus::MissingForm;
}

[[nodiscard]] CommandStatus SetQuestStage(RE::TESQuest& a_quest, const std::uint16_t a_stage) noexcept
{
    // CommonLib does not expose TESQuest::SetStage. Dispatching the engine's
    // native Quest.SetCurrentStageID method preserves the stage scripts and
    // journal bookkeeping without relying on a desktop-only relocation.
    auto* skyrimVm = RE::SkyrimVM::GetSingleton();
    auto* vm = skyrimVm ? skyrimVm->impl.get() : nullptr;
    if (!vm)
        return CommandStatus::Inactive;

    auto* handles = vm->GetObjectHandlePolicy();
    if (!handles)
        return CommandStatus::Inactive;

    const auto handle = handles->GetHandleForObject(RE::FormType::Quest, &a_quest);
    if (handle == handles->EmptyHandle())
        return CommandStatus::EngineRejected;

    const auto suppression = LocalGameplayCapture::ArmQuestStageSuppression(a_quest.GetFormID(), a_stage);
    if (suppression == 0)
        return CommandStatus::EngineRejected;

    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
    if (vm->DispatchMethodCall(
            handle,
            RE::BSFixedString("Quest"),
            RE::BSFixedString("SetCurrentStageID"),
            RE::MakeFunctionArguments(static_cast<std::int32_t>(a_stage)),
            callback))
        return CommandStatus::Success;

    LocalGameplayCapture::CancelQuestSuppression(suppression);
    return CommandStatus::EngineRejected;
}

[[nodiscard]] CommandStatus ExecuteQuestState(const CommandRecord& a_command) noexcept
{
    const auto& identity = a_command.Header.Identity;
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    if (!HasNoEntity(identity) || payload.TargetHandle.Value != 0 || payload.SecondaryHandle.Value != 0 ||
        payload.TargetLocalFormId == 0 || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
        payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || !IsQuestStage(payload.ValueA) ||
        (payload.ValueB != kQuestStatusStarted && payload.ValueB != kQuestStatusStopped) ||
        payload.ScalarA != 0.0f || payload.ScalarB != 0.0f || payload.ScalarC != 0.0f ||
        payload.ScalarD != 0.0f || payload.ActionFlags > kMaximumQuestType)
        return CommandStatus::Malformed;

    RE::TESQuest* quest{};
    const auto questStatus = ResolveQuest(payload, quest);
    if (questStatus != CommandStatus::Success)
        return questStatus;

    if (payload.ValueB == kQuestStatusStopped) {
        const auto suppression = LocalGameplayCapture::ArmQuestStartStopSuppression(quest->GetFormID(), false);
        if (suppression == 0)
            return CommandStatus::EngineRejected;
        quest->Stop();
        return CommandStatus::Success;
    }

    const auto suppression = LocalGameplayCapture::ArmQuestStartStopSuppression(quest->GetFormID(), true);
    if (suppression == 0)
        return CommandStatus::EngineRejected;
    if (!quest->Start()) {
        LocalGameplayCapture::CancelQuestSuppression(suppression);
        return CommandStatus::EngineRejected;
    }
    return SetQuestStage(*quest, static_cast<std::uint16_t>(payload.ValueA));
}

[[nodiscard]] CommandStatus ExecuteQuestStage(const CommandRecord& a_command) noexcept
{
    const auto& identity = a_command.Header.Identity;
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    if (!HasNoEntity(identity) || payload.TargetHandle.Value != 0 || payload.SecondaryHandle.Value != 0 ||
        payload.TargetLocalFormId == 0 || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
        payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || !IsQuestStage(payload.ValueA) ||
        payload.ValueB != 0 || payload.ScalarA != 0.0f || payload.ScalarB != 0.0f ||
        payload.ScalarC != 0.0f || payload.ScalarD != 0.0f || payload.ActionFlags > kMaximumQuestType)
        return CommandStatus::Malformed;

    RE::TESQuest* quest{};
    const auto questStatus = ResolveQuest(payload, quest);
    return questStatus == CommandStatus::Success ?
               SetQuestStage(*quest, static_cast<std::uint16_t>(payload.ValueA)) :
               questStatus;
}

[[nodiscard]] CommandStatus ResolveRemoteActor(const CommandRecord& a_command, RE::NiPointer<RE::Actor>& ar_actor) noexcept
{
    const auto& identity = a_command.Header.Identity;
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    if (payload.TargetHandle.Value == 0 || payload.SecondaryHandle.Value != 0 || !CanonicalEntity::IsValid(
            identity.EntityId,
            identity.EntityGeneration))
        return CommandStatus::Malformed;

    return AvatarManager::Get().ResolveGameplayActor(a_command, ar_actor);
}

[[nodiscard]] CommandStatus ExecuteDialogue(const CommandRecord& a_command) noexcept
{
    const auto& identity = a_command.Header.Identity;
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    switch (static_cast<GameplayAction>(payload.Action)) {
    case GameplayAction::Dialogue:
        // Dialogue text is delivered exclusively through GameplayTextManager.
        // The fixed action record is only its validated companion and has no
        // local game mutation of its own.
        return HasNoEntity(identity) && HasNoTarget(payload) && payload.LocalFormIdA == 0 && HasNoUnusedActionFields(payload) ?
                   CommandStatus::Success :
                   CommandStatus::Malformed;
    case GameplayAction::Subtitle:
    {
        if (payload.TargetLocalFormId != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0f || payload.ScalarB != 0.0f || payload.ScalarC != 0.0f ||
            payload.ScalarD != 0.0f || payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        if (payload.LocalFormIdA != 0 && !LookupLocalForm<RE::TESTopic>(payload.LocalFormIdA))
            return CommandStatus::MissingForm;

        RE::NiPointer<RE::Actor> actor;
        const auto actorStatus = ResolveRemoteActor(a_command, actor);
        // Subtitle text is delivered exclusively through GameplayTextManager.
        // Retain identity and topic validation for this fixed companion action,
        // but do not attempt to synthesize a subtitle without its text payload.
        return actorStatus == CommandStatus::Success ? CommandStatus::Success : actorStatus;
    }
    case GameplayAction::Package:
    {
        if (payload.TargetLocalFormId != 0 || payload.LocalFormIdA == 0 || payload.LocalFormIdB != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0f || payload.ScalarB != 0.0f || payload.ScalarC != 0.0f ||
            payload.ScalarD != 0.0f || payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        auto* package = LookupLocalForm<RE::TESPackage>(payload.LocalFormIdA);
        if (!package)
            return CommandStatus::MissingForm;

        RE::NiPointer<RE::Actor> actor;
        const auto actorStatus = ResolveRemoteActor(a_command, actor);
        if (actorStatus != CommandStatus::Success)
            return actorStatus;

        // This is the typed CommonLib VR virtual used by the original client's
        // Actor::SetPackage wrapper. It assigns this package; EvaluatePackage
        // alone would only replay the actor's existing package.
        actor->PutCreatedPackage(package, false, true, true);
        return CommandStatus::Success;
    }
    default:
        return CommandStatus::Malformed;
    }
}

[[nodiscard]] CommandStatus ExecuteWaypoint(const CommandRecord& a_command) noexcept
{
    const auto& identity = a_command.Header.Identity;
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    if (!HasNoEntity(identity) || !HasNoTarget(payload))
        return CommandStatus::Malformed;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player)
        return CommandStatus::Inactive;

    switch (static_cast<GameplayAction>(payload.Action)) {
    case GameplayAction::SetWaypoint:
        if (payload.LocalFormIdA == 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarD != 0.0f || payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        if (auto* worldspace = LookupLocalForm<RE::TESWorldSpace>(payload.LocalFormIdA)) {
            RE::NiPoint3 position{payload.ScalarA, payload.ScalarB, payload.ScalarC};
            using SetWaypoint = void(RE::PlayerCharacter*, RE::NiPoint3*, RE::TESWorldSpace*);
            // The generated VR row for desktop ID 40535 points at an
            // unrelated routine. RVA 0x6C74D0 is the verified VR equivalent:
            // its body creates/reuses the custom marker, writes the supplied
            // NiPoint3, consumes the TESWorldSpace argument, and stores the
            // handle at VR PlayerCharacter offset 0x1020.
            static REL::Relocation<SetWaypoint> setWaypoint{REL::Offset(kSetWaypointVrRva)};
            if (setWaypoint.offset() != kSetWaypointVrRva ||
                std::memcmp(reinterpret_cast<const void*>(setWaypoint.address()),
                            kSetWaypointVrPrologue.data(), kSetWaypointVrPrologue.size()) != 0)
                return CommandStatus::Unsupported;
            setWaypoint(player, std::addressof(position), worldspace);
            const auto* runtime = player->GetVRInfoRuntimeData();
            const auto marker = runtime ? runtime->playerMapMarker.get() : nullptr;
            return marker && marker->GetWorldspace() == worldspace && marker->GetPosition() == position ?
                       CommandStatus::Success : CommandStatus::EngineRejected;
        }
        else
            return CommandStatus::MissingForm;
    case GameplayAction::RemoveWaypoint:
        if (payload.LocalFormIdA != 0 || !HasNoUnusedActionFields(payload))
            return CommandStatus::Malformed;
        {
            using RemoveWaypoint = void(RE::PlayerCharacter*);
            // RVA 0x6C7630 releases the handle at VR offset 0x1020, restores
            // the invalid handle sentinel, and updates the marker path. The
            // generated desktop ID 40536 row is not this function in VR.
            static REL::Relocation<RemoveWaypoint> removeWaypoint{REL::Offset(kRemoveWaypointVrRva)};
            if (removeWaypoint.offset() != kRemoveWaypointVrRva ||
                std::memcmp(reinterpret_cast<const void*>(removeWaypoint.address()),
                            kRemoveWaypointVrPrologue.data(), kRemoveWaypointVrPrologue.size()) != 0)
                return CommandStatus::Unsupported;
            removeWaypoint(player);
            const auto* runtime = player->GetVRInfoRuntimeData();
            return runtime && !runtime->playerMapMarker.get() ?
                       CommandStatus::Success : CommandStatus::EngineRejected;
        }
    default:
        return CommandStatus::Malformed;
    }
}

[[nodiscard]] CommandStatus ResolveTeleportLocation(
    const GameplayActionPayload& a_payload,
    RE::TESObjectCELL*& ar_cell,
    RE::TESWorldSpace*& ar_worldspace) noexcept
{
    ar_cell = LookupLocalForm<RE::TESObjectCELL>(a_payload.LocalFormIdA);
    ar_worldspace = a_payload.LocalFormIdB != 0 ? LookupLocalForm<RE::TESWorldSpace>(a_payload.LocalFormIdB) : nullptr;
    if (!ar_cell || (a_payload.LocalFormIdB != 0 && !ar_worldspace))
        return CommandStatus::MissingCell;

    return ar_cell->IsInteriorCell() ?
               (ar_worldspace == nullptr ? CommandStatus::Success : CommandStatus::Malformed) :
               (ar_worldspace != nullptr && ar_cell->GetRuntimeData().worldSpace == ar_worldspace ?
                    CommandStatus::Success :
                    CommandStatus::Malformed);
}

[[nodiscard]] CommandStatus ExecuteTeleport(const CommandRecord& a_command) noexcept
{
    const auto& identity = a_command.Header.Identity;
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    if (!HasNoEntity(identity) || payload.TargetHandle.Value != 0 || payload.SecondaryHandle.Value != 0 ||
        payload.LocalFormIdA == 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
        payload.ValueA != 0 || payload.ValueB != 0 || payload.ScalarD != 0.0f || payload.ActionFlags != 0)
        return CommandStatus::Malformed;

    RE::TESObjectREFR* target = payload.TargetLocalFormId != 0 ?
                                     LookupLocalForm<RE::TESObjectREFR>(payload.TargetLocalFormId) :
                                     RE::PlayerCharacter::GetSingleton();
    if (!target)
        return CommandStatus::MissingForm;

    RE::TESObjectCELL* cell{};
    RE::TESWorldSpace* worldspace{};
    const auto locationStatus = ResolveTeleportLocation(payload, cell, worldspace);
    if (locationStatus != CommandStatus::Success)
        return locationStatus;

    // This is the pinned CommonLib internal move-to contract used by the VR
    // avatar spatial-transfer lane. Its VR address database maps the audited
    // SE ID 56227; do not replace it with a desktop hook or direct cell write.
    using MoveTo = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*,
                        const RE::NiPoint3&, const RE::NiPoint3&);
    static REL::Relocation<MoveTo> moveTo{RELOCATION_ID(56227, 56626)};
    if (moveTo.offset() != kMoveToVrRva ||
        std::memcmp(reinterpret_cast<const void*>(moveTo.address()),
                    kMoveToVrPrologue.data(), kMoveToVrPrologue.size()) != 0)
        return CommandStatus::Unsupported;
    moveTo(
        target,
        RE::ObjectRefHandle{},
        cell,
        worldspace,
        RE::NiPoint3{payload.ScalarA, payload.ScalarB, payload.ScalarC},
        target->GetAngle());
    return target->GetParentCell() == cell && target->GetWorldspace() == worldspace ?
               CommandStatus::Success :
               CommandStatus::EngineRejected;
}
} // namespace

CommandStatus QuestDialogueManager::Execute(const CommandRecord& a_command) noexcept
{
    try {
        const auto baseStatus = ValidateBaseCommand(a_command);
        if (baseStatus != CommandStatus::Success)
            return baseStatus;

        const auto& payload = a_command.Payload.ApplyGameplayAction;
        switch (static_cast<GameplayDomain>(payload.Domain)) {
        case GameplayDomain::Quest:
            return static_cast<GameplayAction>(payload.Action) == GameplayAction::SetQuestState ?
                       ExecuteQuestState(a_command) :
                       ExecuteQuestStage(a_command);
        case GameplayDomain::Dialogue:
            return ExecuteDialogue(a_command);
        case GameplayDomain::Party:
            return ExecuteWaypoint(a_command);
        case GameplayDomain::WorldState:
            return static_cast<GameplayAction>(payload.Action) == GameplayAction::Teleport ?
                       ExecuteTeleport(a_command) :
                       CommandStatus::Malformed;
        default:
            return CommandStatus::Malformed;
        }
    } catch (...) {
        return CommandStatus::EngineRejected;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter
