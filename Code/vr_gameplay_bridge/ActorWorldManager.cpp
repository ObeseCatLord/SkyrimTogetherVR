#include "ActorWorldManager.h"

#include "LocalGameplayCapture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr float kMaximumActorValue = 1000000.0f;
constexpr std::array<RE::FormID, 9> kCrimeFactionFormIds{
    0x28170, 0x267E3, 0x29DB0, 0x2816D, 0x2816E, 0x2816C, 0x2816B, 0x267EA, 0x2816F,
};
constexpr RE::FormID kRightHandEquipSlotFormId = 0x13F42;
constexpr RE::FormID kLeftHandEquipSlotFormId = 0x13F43;
constexpr auto kPostRespawnKnockdownDelay = std::chrono::milliseconds(1500);
constexpr auto kPostRespawnProtectionDuration = std::chrono::seconds(10);

enum class PostRespawnPhase : std::uint8_t
{
    None,
    WaitingForKnockdown,
    Protected,
};

PostRespawnPhase g_postRespawnPhase{PostRespawnPhase::None};
std::chrono::steady_clock::time_point g_postRespawnDeadline{};
bool g_postRespawnEnabledGodMode{};

[[nodiscard]] bool IsZero(const GameplayActionPayload& a_payload) noexcept
{
    return a_payload.LocalFormIdA == 0 && a_payload.LocalFormIdB == 0 &&
           a_payload.LocalFormIdC == 0 && a_payload.LocalFormIdD == 0 &&
           a_payload.ValueA == 0 && a_payload.ValueB == 0 &&
           a_payload.ScalarA == 0.0f && a_payload.ScalarB == 0.0f &&
           a_payload.ScalarC == 0.0f && a_payload.ScalarD == 0.0f &&
           a_payload.ActionFlags == 0;
}

[[nodiscard]] bool HasOnlyActorValueArguments(const GameplayActionPayload& a_payload) noexcept
{
    return a_payload.TargetLocalFormId == 0 && a_payload.LocalFormIdB == 0 &&
           a_payload.LocalFormIdC == 0 && a_payload.LocalFormIdD == 0 &&
           a_payload.ValueA == 0 && a_payload.ValueB == 0 &&
           a_payload.ScalarB == 0.0f && a_payload.ScalarC == 0.0f &&
           a_payload.ScalarD == 0.0f && a_payload.ActionFlags == 0;
}

[[nodiscard]] bool IsValidActorValue(const std::uint32_t a_value) noexcept
{
    return a_value < static_cast<std::uint32_t>(RE::ActorValue::kTotal);
}

[[nodiscard]] bool IsValidActorValueAmount(const float a_value) noexcept
{
    return std::isfinite(a_value) && a_value >= -kMaximumActorValue && a_value <= kMaximumActorValue;
}

[[nodiscard]] bool IsBoolean(const std::int32_t a_value) noexcept
{
    return a_value == 0 || a_value == 1;
}

[[nodiscard]] bool IsValidLockLevel(const std::int32_t a_level) noexcept
{
    return a_level >= static_cast<std::int32_t>(RE::LOCK_LEVEL::kVeryEasy) &&
           a_level <= static_cast<std::int32_t>(RE::LOCK_LEVEL::kRequiresKey);
}

[[nodiscard]] CommandStatus ResolveActor(const CommandRecord& a_command, RE::NiPointer<RE::Actor>& ar_actor) noexcept
{
    return AvatarManager::Get().ResolveGameplayActor(a_command, ar_actor);
}

[[nodiscard]] CommandStatus ResolveObject(
    const GameplayActionPayload& a_payload,
    RE::TESObjectREFR*& ar_object) noexcept
{
    ar_object = nullptr;
    if (a_payload.TargetLocalFormId == 0)
        return CommandStatus::Malformed;

    ar_object = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_payload.TargetLocalFormId);
    return ar_object ? CommandStatus::Success : CommandStatus::MissingForm;
}

[[nodiscard]] CommandStatus RespawnLocalPlayer(RE::Actor& a_actor) noexcept
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || &a_actor != player)
        return CommandStatus::Malformed;

    const auto& runtime = player->GetActorRuntimeData();
    auto* rightSpell = runtime.selectedSpells[0] ? runtime.selectedSpells[0]->As<RE::SpellItem>() : nullptr;
    auto* leftSpell = runtime.selectedSpells[1] ? runtime.selectedSpells[1]->As<RE::SpellItem>() : nullptr;
    auto* shout = runtime.selectedPower ? runtime.selectedPower->As<RE::TESShout>() : nullptr;
    auto* destination = player->GetParentCell();

    for (const auto formId : kCrimeFactionFormIds) {
        if (auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(formId))
            faction->PlayerPayCrimeGold(false, false);
    }

    player->Resurrect(false, true);
    if (destination)
        TP_UNUSED(player->CenterOnCell(destination));

    if (auto* equipManager = RE::ActorEquipManager::GetSingleton()) {
        const auto* rightSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(kRightHandEquipSlotFormId);
        const auto* leftSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(kLeftHandEquipSlotFormId);
        if (rightSpell && rightSlot)
            equipManager->EquipSpell(player, rightSpell, rightSlot);
        if (leftSpell && leftSlot)
            equipManager->EquipSpell(player, leftSpell, leftSlot);
        if (shout)
            equipManager->EquipShout(player, shout);
    }
    if (player->IsDead())
        return CommandStatus::EngineRejected;

    g_postRespawnEnabledGodMode = false;
    g_postRespawnPhase = PostRespawnPhase::WaitingForKnockdown;
    g_postRespawnDeadline = std::chrono::steady_clock::now() + kPostRespawnKnockdownDelay;
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ExecuteActorState(const CommandRecord& a_command) noexcept
{
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    const auto action = static_cast<GameplayAction>(payload.Action);

    switch (action) {
    case GameplayAction::SetActorValue:
    case GameplayAction::SetActorMaximum:
        if (payload.TargetHandle.Value == 0 || !HasOnlyActorValueArguments(payload) ||
            !IsValidActorValue(payload.LocalFormIdA) || !IsValidActorValueAmount(payload.ScalarA))
            return CommandStatus::Malformed;
        break;
    case GameplayAction::ModifyActorValue:
        if (payload.TargetHandle.Value == 0 || !HasOnlyActorValueArguments(payload) ||
            !IsValidActorValue(payload.LocalFormIdA) || !IsValidActorValueAmount(payload.ScalarA))
            return CommandStatus::Malformed;
        break;
    case GameplayAction::SyncExperience:
        if (payload.TargetHandle.Value != kLocalPlayerHandle.Value || payload.SecondaryHandle.Value != 0 ||
            !HasOnlyActorValueArguments(payload) ||
            !IsCombatSkillActorValue(payload.LocalFormIdA) || payload.ScalarA <= 0.0F ||
            payload.ScalarA > kMaximumSyncedExperience)
            return CommandStatus::Malformed;
        break;
    case GameplayAction::SetDeathState:
        if (payload.TargetHandle.Value == 0 || payload.TargetLocalFormId != 0 || payload.LocalFormIdA != 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            !IsBoolean(payload.ValueA) || payload.ValueB != 0 || payload.ScalarA != 0.0f ||
            payload.ScalarB != 0.0f || payload.ScalarC != 0.0f || payload.ScalarD != 0.0f ||
            payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        break;
    case GameplayAction::Respawn:
        if (payload.TargetHandle.Value == 0 || payload.SecondaryHandle.Value != 0 ||
            payload.TargetLocalFormId != 0 || !IsZero(payload))
            return CommandStatus::Malformed;
        break;
    case GameplayAction::Mount:
        if (payload.TargetHandle.Value == 0 || payload.TargetLocalFormId != 0 || !IsZero(payload))
            return CommandStatus::Malformed;
        break;
    case GameplayAction::SetLevel:
        if (payload.TargetHandle.Value == 0 || payload.SecondaryHandle.Value != 0 ||
            payload.TargetLocalFormId != 0 || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueA <= 0 ||
            payload.ValueA > std::numeric_limits<std::uint16_t>::max() || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        break;
    case GameplayAction::SetEssential:
        if (payload.TargetHandle.Value == 0 || payload.SecondaryHandle.Value != 0 ||
            payload.TargetLocalFormId != 0 || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || !IsBoolean(payload.ValueA) ||
            payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        break;
    case GameplayAction::SetFactionRank:
        if (payload.TargetHandle.Value == 0 || payload.SecondaryHandle.Value != 0 ||
            payload.TargetLocalFormId != 0 || payload.LocalFormIdA == 0 || payload.LocalFormIdB != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA < std::numeric_limits<std::int8_t>::min() ||
            payload.ValueA > std::numeric_limits<std::int8_t>::max() || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        break;
    case GameplayAction::ResetFactions:
        if (payload.TargetHandle.Value == 0 || payload.SecondaryHandle.Value != 0 ||
            payload.TargetLocalFormId != 0 || !IsZero(payload))
            return CommandStatus::Malformed;
        break;
    default:
        return CommandStatus::Malformed;
    }

    RE::NiPointer<RE::Actor> actor;
    const auto actorStatus = ResolveActor(a_command, actor);
    if (actorStatus != CommandStatus::Success)
        return actorStatus;

    switch (action) {
    case GameplayAction::SetActorValue:
        actor->SetActorValue(static_cast<RE::ActorValue>(payload.LocalFormIdA), payload.ScalarA);
        return CommandStatus::Success;
    case GameplayAction::SetActorMaximum:
        actor->SetBaseActorValue(static_cast<RE::ActorValue>(payload.LocalFormIdA), payload.ScalarA);
        return CommandStatus::Success;
    case GameplayAction::ModifyActorValue:
        actor->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage,
                             static_cast<RE::ActorValue>(payload.LocalFormIdA), payload.ScalarA);
        if (payload.LocalFormIdA == static_cast<std::uint32_t>(RE::ActorValue::kHealth) &&
            actor->GetActorValue(RE::ActorValue::kHealth) <= 0.0F && !actor->IsDead() &&
            !AvatarManager::Get().IsPlayerAvatar(a_command.Header.Identity, payload.TargetHandle))
            actor->KillDying();
        return CommandStatus::Success;
    case GameplayAction::SyncExperience:
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || actor.get() != player)
            return CommandStatus::EngineRejected;
        if (!LocalGameplayCapture::ArmExperienceSuppression(payload.LocalFormIdA))
            return CommandStatus::EngineRejected;
        player->AddSkillExperience(static_cast<RE::ActorValue>(payload.LocalFormIdA), payload.ScalarA);
        return CommandStatus::Success;
    }
    case GameplayAction::SetDeathState:
        if (payload.ValueA != 0)
            actor->KillDying();
        else
            actor->Resurrect(false, true);
        return CommandStatus::Success;
    case GameplayAction::Respawn:
        if (payload.TargetHandle.Value == kLocalPlayerHandle.Value)
            return RespawnLocalPlayer(*actor);
        actor->Resurrect(false, true);
        return actor->IsDead() ? CommandStatus::EngineRejected : CommandStatus::Success;
    case GameplayAction::Mount:
        if (payload.SecondaryHandle.Value == 0) {
            actor->SetLastRiddenMount({});
            return actor->NotifyAnimationGraph(RE::BSFixedString{"HorseExit"}) ?
                       CommandStatus::Success : CommandStatus::EngineRejected;
        }
        RE::NiPointer<RE::Actor> mount;
        if (const auto mountStatus = AvatarManager::Get().ResolveActorByHandle(
                a_command.Header.Identity, payload.SecondaryHandle, mount);
            mountStatus != CommandStatus::Success)
            return mountStatus;
        actor->SetLastRiddenMount(mount->GetHandle());
        return actor->PutActorOnMountQuick() ? CommandStatus::Success : CommandStatus::EngineRejected;
    case GameplayAction::SetLevel:
    {
        auto* base = actor->GetActorBase();
        if (!base || !base->IsDynamicForm())
            return CommandStatus::EngineRejected;
        base->SetActorBaseFlag(RE::ACTOR_BASE_DATA::Flag::kPCLevelMult, false, true);
        base->actorData.level = static_cast<std::uint16_t>(payload.ValueA);
        return CommandStatus::Success;
    }
    case GameplayAction::SetEssential:
    {
        auto* base = actor->GetActorBase();
        if (!base || !base->IsDynamicForm())
            return CommandStatus::EngineRejected;
        base->SetActorBaseFlag(RE::ACTOR_BASE_DATA::Flag::kEssential, payload.ValueA != 0, true);
        return CommandStatus::Success;
    }
    case GameplayAction::SetFactionRank:
    {
        auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(payload.LocalFormIdA);
        if (!faction)
            return CommandStatus::MissingForm;
        actor->AddToFaction(faction, static_cast<std::int8_t>(payload.ValueA));
        return CommandStatus::Success;
    }
    case GameplayAction::ResetFactions:
    {
        std::vector<RE::TESFaction*> factions;
        actor->VisitFactions([&factions](RE::TESFaction* a_faction, std::int8_t) {
            if (a_faction && std::find(factions.begin(), factions.end(), a_faction) == factions.end())
                factions.push_back(a_faction);
            return false;
        });
        for (auto* faction : factions)
            actor->RemoveFromFaction(faction);
        return CommandStatus::Success;
    }
    default:
        return CommandStatus::Malformed;
    }
}

[[nodiscard]] CommandStatus ExecuteObjectAction(const CommandRecord& a_command) noexcept
{
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    const auto action = static_cast<GameplayAction>(payload.Action);

    switch (action) {
    case GameplayAction::Activate:
        if (payload.TargetHandle.Value == 0 || payload.TargetLocalFormId == 0 ||
            payload.SecondaryHandle.Value != 0 || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueA < 0 || payload.ValueA > 4 ||
            payload.ValueB != 0 || payload.ScalarA != 0.0f || payload.ScalarB != 0.0f ||
            payload.ScalarC != 0.0f || payload.ScalarD != 0.0f || payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        break;
    case GameplayAction::SetLockState:
        if (payload.TargetHandle.Value != 0 || payload.TargetLocalFormId == 0 ||
            payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            !IsBoolean(payload.ValueA) || payload.ScalarA != 0.0f || payload.ScalarB != 0.0f ||
            payload.ScalarC != 0.0f || payload.ScalarD != 0.0f || payload.ActionFlags != 0 ||
            (payload.ValueA != 0 ? !IsValidLockLevel(payload.ValueB) : payload.ValueB != static_cast<std::int32_t>(RE::LOCK_LEVEL::kUnlocked)))
            return CommandStatus::Malformed;
        break;
    case GameplayAction::SetOpenState:
        if (payload.TargetHandle.Value != 0 || payload.TargetLocalFormId == 0 ||
            payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            !IsBoolean(payload.ValueA) || payload.ValueB != 0 || payload.ScalarA != 0.0f ||
            payload.ScalarB != 0.0f || payload.ScalarC != 0.0f || payload.ScalarD != 0.0f || payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        break;
    case GameplayAction::SetOwnership:
        if (payload.TargetHandle.Value != 0 || payload.TargetLocalFormId == 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0f || payload.ScalarB != 0.0f ||
            payload.ScalarC != 0.0f || payload.ScalarD != 0.0f || payload.ActionFlags != 0)
            return CommandStatus::Malformed;
        break;
    case GameplayAction::ScriptAnimation:
        if (payload.TargetHandle.Value != 0 || payload.TargetLocalFormId == 0 || !IsZero(payload))
            return CommandStatus::Malformed;
        break;
    default:
        return CommandStatus::Malformed;
    }

    RE::TESObjectREFR* object{};
    const auto objectStatus = ResolveObject(payload, object);
    if (objectStatus != CommandStatus::Success)
        return objectStatus;

    switch (action) {
    case GameplayAction::Activate:
    {
        RE::NiPointer<RE::Actor> activator;
        const auto actorStatus = ResolveActor(a_command, activator);
        if (actorStatus != CommandStatus::Success)
            return actorStatus;
        auto* base = object->GetObjectReference();
        if (!base)
            return CommandStatus::EngineRejected;
        if (base->GetFormType() == RE::FormType::Door &&
            static_cast<std::int32_t>(RE::BGSOpenCloseForm::GetOpenState(object)) != payload.ValueA)
            return CommandStatus::StaleEntity;
        return base->Activate(object, activator.get(), 0, nullptr, 1) ? CommandStatus::Success : CommandStatus::EngineRejected;
    }
    case GameplayAction::SetLockState:
    {
        const auto suppression = LocalGameplayCapture::ArmLockSuppression(
            object->GetFormID(), payload.ValueA != 0, static_cast<std::uint8_t>(payload.ValueB));
        if (suppression == 0)
            return CommandStatus::QueueOverflow;
        const auto* extraLock = object->extraList.GetByType<RE::ExtraLock>();
        auto* lock = extraLock ? extraLock->lock : nullptr;
        if (payload.ValueA == 0) {
            if (lock)
                lock->SetLocked(false);
            return CommandStatus::Success;
        }
        if (!lock)
            lock = object->AddLock();
        if (!lock) {
            LocalGameplayCapture::CancelLockSuppression(suppression);
            return CommandStatus::EngineRejected;
        }

        lock->baseLevel = static_cast<std::int8_t>(payload.ValueB);
        lock->SetLocked(true);
        return CommandStatus::Success;
    }
    case GameplayAction::SetOpenState:
    {
        auto* openClose = object->GetObjectReference() ? object->GetObjectReference()->As<RE::BGSOpenCloseForm>() : nullptr;
        if (!openClose)
            return CommandStatus::Unsupported;
        if (payload.ValueA != 0)
            openClose->HandleOpen(object, nullptr);
        else
            openClose->HandleClose(object, nullptr);
        return CommandStatus::Success;
    }
    case GameplayAction::SetOwnership:
        if (payload.LocalFormIdA != 0) {
            const auto* owner = RE::TESForm::LookupByID(payload.LocalFormIdA);
            if (!owner)
                return CommandStatus::MissingForm;
            if (!owner->Is(RE::FormType::NPC, RE::FormType::Faction))
                return CommandStatus::Malformed;
        }
        object->SetOwner(payload.LocalFormIdA != 0 ? RE::TESForm::LookupByID(payload.LocalFormIdA) : nullptr);
        return CommandStatus::Success;
    case GameplayAction::ScriptAnimation:
        // Script animation requires animation and event strings, which this
        // fixed-size payload intentionally does not carry.
        return CommandStatus::Unsupported;
    default:
        return CommandStatus::Malformed;
    }
}

[[nodiscard]] CommandStatus ExecuteNpcOwnershipAction(const CommandRecord& a_command) noexcept
{
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    const auto action = static_cast<GameplayAction>(payload.Action);
    if (payload.TargetHandle.Value != kLocalPlayerHandle.Value || payload.SecondaryHandle.Value != 0 ||
        payload.TargetLocalFormId == 0 || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
        payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
        payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
        payload.ScalarD != 0.0F || payload.ActionFlags != 0)
        return CommandStatus::Malformed;

    switch (action) {
    case GameplayAction::StartNpcObservation:
        return LocalGameplayCapture::StartNpcObservation(payload.TargetLocalFormId) ?
                   CommandStatus::Success : CommandStatus::MissingForm;
    case GameplayAction::StopNpcObservation:
        LocalGameplayCapture::StopNpcObservation(payload.TargetLocalFormId);
        return CommandStatus::Success;
    default:
        return CommandStatus::Malformed;
    }
}
} // namespace

CommandStatus ActorWorldManager::Execute(const CommandRecord& a_command) noexcept
{
    try {
        if (static_cast<CommandKind>(a_command.Header.Kind) != CommandKind::ApplyGameplayAction ||
            a_command.Header.PayloadSize != kFixedPayloadBytes || a_command.Header.Flags != 0 ||
            a_command.Header.Identity.Reserved0 != 0)
            return CommandStatus::Malformed;

        const auto& payload = a_command.Payload.ApplyGameplayAction;
        if (payload.Reserved0 != 0 ||
            !std::all_of(std::begin(payload.ReservedTail), std::end(payload.ReservedTail), [](std::uint8_t a_value) { return a_value == 0; }) ||
            !std::isfinite(payload.ScalarA) || !std::isfinite(payload.ScalarB) ||
            !std::isfinite(payload.ScalarC) || !std::isfinite(payload.ScalarD))
            return CommandStatus::Malformed;

        const auto domain = static_cast<GameplayDomain>(payload.Domain);
        const auto action = static_cast<GameplayAction>(payload.Action);
        if (!IsActionInDomain(domain, action))
            return CommandStatus::Malformed;

        switch (domain) {
        case GameplayDomain::ActorState:
            return ExecuteActorState(a_command);
        case GameplayDomain::Object:
            return ExecuteObjectAction(a_command);
        case GameplayDomain::NpcOwnership:
            return ExecuteNpcOwnershipAction(a_command);
        default:
            return CommandStatus::Unsupported;
        }
    } catch (...) {
        return CommandStatus::EngineRejected;
    }
}

void ActorWorldManager::ProcessPeriodic() noexcept
{
    if (g_postRespawnPhase == PostRespawnPhase::None ||
        std::chrono::steady_clock::now() < g_postRespawnDeadline)
        return;

    try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
        {
            Reset();
            return;
        }

        if (g_postRespawnPhase == PostRespawnPhase::WaitingForKnockdown)
        {
            if (!RE::PlayerCharacter::IsGodMode())
            {
                player->SetGodMode(true);
                g_postRespawnEnabledGodMode = true;
            }
            if (auto* process = player->GetActorRuntimeData().currentProcess)
                process->KnockExplosion(player, player->GetPosition(), 0.0F);
            g_postRespawnPhase = PostRespawnPhase::Protected;
            g_postRespawnDeadline = std::chrono::steady_clock::now() + kPostRespawnProtectionDuration;
            return;
        }

        if (g_postRespawnEnabledGodMode)
            player->SetGodMode(false);
        g_postRespawnEnabledGodMode = false;
        g_postRespawnPhase = PostRespawnPhase::None;
        g_postRespawnDeadline = {};
    } catch (...) {
        Reset();
    }
}

void ActorWorldManager::Reset() noexcept
{
    if (g_postRespawnEnabledGodMode)
    {
        try {
            if (auto* player = RE::PlayerCharacter::GetSingleton())
                player->SetGodMode(false);
        } catch (...) {
        }
    }
    g_postRespawnEnabledGodMode = false;
    g_postRespawnPhase = PostRespawnPhase::None;
    g_postRespawnDeadline = {};
}
} // namespace SkyrimTogetherVR::GameplayAdapter
