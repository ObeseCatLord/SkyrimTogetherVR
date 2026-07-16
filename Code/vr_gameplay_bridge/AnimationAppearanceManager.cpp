#include "AnimationAppearanceManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr std::int32_t kMaximumItemCount = 10'000;
constexpr std::size_t kMaximumStagedEquipmentTransactions = 512;
constexpr std::uint32_t kRightHandEquipSlotFormId = 0x00013F42;
constexpr std::uint32_t kLeftHandEquipSlotFormId = 0x00013F43;

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
    const bool actorTarget = payload.TargetHandle.Value != 0 && payload.TargetLocalFormId == 0;
    const bool inventoryTarget = domain == GameplayDomain::Inventory &&
                                 ((payload.TargetHandle.Value != 0) != (payload.TargetLocalFormId != 0));
    if (!IsActionInDomain(domain, action) || (!actorTarget && !inventoryTarget) ||
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

[[nodiscard]] CommandStatus ApplyAppearance(
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
            a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return CommandStatus::Malformed;
        if (!RE::TESForm::LookupByID<RE::TESRace>(a_payload.LocalFormIdA))
            return CommandStatus::MissingForm;
        npc->race = RE::TESForm::LookupByID<RE::TESRace>(a_payload.LocalFormIdA);
        npc->originalRace = nullptr;
        a_actor.Update3DModel();
        return CommandStatus::Success;
    case GameplayAction::SetSex:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA > 1 || a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return CommandStatus::Malformed;
        npc->actorData.actorBaseFlags.set(a_payload.ValueA != 0, RE::ACTOR_BASE_DATA::Flag::kFemale);
        a_actor.Update3DModel();
        return CommandStatus::Success;
    case GameplayAction::SetWeight:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || a_payload.ScalarA < 0.0F || a_payload.ScalarA > 100.0F ||
            a_payload.ScalarB != 0.0F || a_payload.ScalarC != 0.0F || a_payload.ScalarD != 0.0F)
            return CommandStatus::Malformed;
        npc->weight = a_payload.ScalarA;
        a_actor.Update3DModel();
        return CommandStatus::Success;
    case GameplayAction::SetName:
        // CommandRecord is fixed-size and has no UTF-8/UTF-16 value field.
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA != 0 || a_payload.ValueA != 0 ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return CommandStatus::Malformed;
        return CommandStatus::Unsupported;
    case GameplayAction::SetHeadPart:
        if (!HasNoUnusedForms(a_payload) || a_payload.LocalFormIdA == 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA >= static_cast<std::int32_t>(RE::BGSHeadPart::HeadPartType::kTotal) ||
            a_payload.ValueB != 0 || !HasNoScalars(a_payload))
            return CommandStatus::Malformed;
        if (!RE::TESForm::LookupByID<RE::BGSHeadPart>(a_payload.LocalFormIdA))
            return CommandStatus::MissingForm;
        npc->ChangeHeadPart(RE::TESForm::LookupByID<RE::BGSHeadPart>(a_payload.LocalFormIdA));
        a_actor.Update3DModel();
        return CommandStatus::Success;
    case GameplayAction::SetTint:
        if (a_payload.LocalFormIdA != 0 ||
            a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 || a_payload.ValueA < 0 ||
            a_payload.ValueA >= static_cast<std::int32_t>(RE::TintMask::Type::kTotal) ||
            a_payload.ValueB != 0 || a_payload.ScalarA < 0.0F || a_payload.ScalarA > 1.0F ||
            a_payload.ScalarB != 0.0F || a_payload.ScalarC != 0.0F || a_payload.ScalarD != 0.0F)
            return CommandStatus::Malformed;
        if (a_payload.ValueA != static_cast<std::int32_t>(RE::TintMask::Type::kSkinTone))
            return CommandStatus::Unsupported;
        npc->bodyTintColor.red = static_cast<std::uint8_t>((a_payload.LocalFormIdB >> 16) & 0xffu);
        npc->bodyTintColor.green = static_cast<std::uint8_t>((a_payload.LocalFormIdB >> 8) & 0xffu);
        npc->bodyTintColor.blue = static_cast<std::uint8_t>(a_payload.LocalFormIdB & 0xffu);
        a_actor.Update3DModel();
        return CommandStatus::Success;
    default:
        return CommandStatus::Malformed;
    }

    return CommandStatus::Malformed;
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
    const CommandRecord& a_command, RE::Actor& a_actor, const GameplayActionPayload& a_payload) noexcept
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
        const auto knownFlags = kEquipmentSnapshotWorn | kEquipmentSnapshotWornLeft;
        if (transactionId != staged.TransactionId || a_payload.LocalFormIdA == 0 || a_payload.LocalFormIdD != 0 ||
            a_payload.ValueA <= 0 || a_payload.ValueA > kMaximumItemCount || a_payload.ValueB != 0 ||
            !noScalars || (a_payload.ActionFlags & ~knownFlags) != 0 ||
            (a_payload.ActionFlags & knownFlags) == 0 || staged.Entries.size() >= staged.ExpectedEntries ||
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
        const auto inventoryEntry = object ? inventory.find(object) : inventory.end();
        if (!object || inventoryEntry == inventory.end() || inventoryEntry->second.first < static_cast<std::int32_t>(entry.Count))
            return eraseAnd(object ? CommandStatus::EngineRejected : CommandStatus::MissingForm);
    }
    std::vector<RE::TESBoundObject*> wornItems;
    for (const auto& [object, data] : inventory) {
        if (object && data.second && data.second->IsWorn())
            wornItems.push_back(object);
    }
    std::sort(wornItems.begin(), wornItems.end(), [](const auto* aLeft, const auto* aRight) {
        return aLeft->GetFormID() < aRight->GetFormID();
    });

    // All form, actor, manager, and count validation is complete before the
    // first engine mutation. The bounded staging entry is consumed only here.
    for (auto* object : wornItems)
        manager->UnequipObject(&a_actor, object, nullptr, 1, nullptr, true, true, false, false);
    for (const auto& entry : staged.Entries) {
        auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(entry.FormId);
        const auto handItem = object->GetFormType() == RE::FormType::Weapon || object->GetFormType() == RE::FormType::Ammo;
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
                                          RE::MakeFunctionArguments(aSpell, static_cast<std::int32_t>(aSource)), callback);
        };
        const auto unequipShout = [vm, handle](RE::TESShout* aShout) noexcept {
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            return vm->DispatchMethodCall(handle, RE::BSFixedString("Actor"), RE::BSFixedString("UnequipShout"),
                                          RE::MakeFunctionArguments(aShout), callback);
        };
        if ((needsLeftUnequip && !unequipSpell(currentLeftSpell, RE::MagicSystem::CastingSource::kLeftHand)) ||
            (needsRightUnequip && !unequipSpell(currentRightSpell, RE::MagicSystem::CastingSource::kRightHand)) ||
            (needsShoutUnequip && !unequipShout(currentShout)))
            return eraseAnd(CommandStatus::EngineRejected);
    }
    if (leftSpell)
        manager->EquipSpell(&a_actor, leftSpell, RE::TESForm::LookupByID<RE::BGSEquipSlot>(kLeftHandEquipSlotFormId));
    if (rightSpell)
        manager->EquipSpell(&a_actor, rightSpell, RE::TESForm::LookupByID<RE::BGSEquipSlot>(kRightHandEquipSlotFormId));
    if (shout)
        manager->EquipShout(&a_actor, shout);
    s_stagedEquipment.erase(stagedIt);
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ApplyEquipment(
    const CommandRecord& a_command,
    RE::Actor& a_actor,
    const GameplayActionPayload& a_payload) noexcept
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
                   RE::MakeFunctionArguments(spell, source),
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
                   RE::MakeFunctionArguments(shout),
                   callback) ?
                   CommandStatus::Success : CommandStatus::EngineRejected;
    }
    return RE::TESForm::LookupByID(a_payload.LocalFormIdA) ? CommandStatus::Unsupported : CommandStatus::MissingForm;
}

[[nodiscard]] CommandStatus ApplyInventory(
    RE::TESObjectREFR& a_owner,
    const GameplayActionPayload& a_payload) noexcept
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
} // namespace

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
                return ApplyAppearance(*actor, payload);
            return ApplyEquipment(a_command, *actor, payload);
        }
        case GameplayDomain::Inventory:
        {
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
    s_stagedEquipment.clear();
}
} // namespace SkyrimTogetherVR::GameplayAdapter
