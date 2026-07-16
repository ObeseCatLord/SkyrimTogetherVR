#include "MagicHooks.h"

#include "AvatarManager.h"
#include "BridgeEndpoint.h"

#include <MinHook.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace SkyrimTogetherVR::GameplayAdapter::MagicHooks
{
namespace
{
using InterruptCastImpl = void (*)(RE::ActorMagicCaster*, bool);
using SpellCast = void (*)(RE::ActorMagicCaster*, bool, std::uint32_t, RE::MagicItem*);
using AddTarget = bool (*)(RE::MagicTarget*, RE::MagicTarget::AddTargetData&);
using RemoveSpell = bool (*)(RE::Actor*, RE::SpellItem*);

constexpr std::uint32_t kMagicEffectAreaTarget = 1u << 0;
constexpr std::uint32_t kMagicEffectDualCasted = 1u << 1;
constexpr std::uint32_t kMagicEffectHostile = 1u << 2;
constexpr std::uint64_t kAddTargetVrRva = 0x05579C0;
constexpr std::array<std::uint8_t, 16> kAddTargetVrPrologue{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48,
    0x8B, 0xEC, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00,
};
constexpr std::uint64_t kRemoveSpellVrRva = 0x06385F0;
constexpr std::array<std::uint8_t, 16> kRemoveSpellVrPrologue{
    0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC,
    0x20, 0x45, 0x32, 0xF6, 0x48, 0x8B, 0xFA, 0x48,
};

InterruptCastImpl g_originalInterruptCast{};
SpellCast g_originalSpellCast{};
AddTarget g_originalAddTarget{};
RemoveSpell g_originalRemoveSpell{};
std::atomic<bool> g_installing{};
std::atomic<bool> g_installed{};
std::atomic<std::uint64_t> g_nextActionId{};
thread_local std::uint32_t g_remoteMagicApplicationDepth{};
void* g_interruptTarget{};
void* g_spellTarget{};
void* g_addTargetTarget{};
void* g_removeSpellTarget{};

[[nodiscard]] bool IsExecutableTarget(const std::uintptr_t a_address) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    return a_address >= text.address() && a_address < text.address() + text.size();
}

[[nodiscard]] bool IsValidFormId(const RE::FormID a_formId) noexcept
{
    return a_formId != 0 && a_formId != std::numeric_limits<RE::FormID>::max() &&
           RE::TESForm::LookupByID(a_formId) != nullptr;
}

[[nodiscard]] bool IsValidCastingSource(const RE::MagicSystem::CastingSource a_source) noexcept
{
    const auto value = static_cast<std::int32_t>(a_source);
    return value >= static_cast<std::int32_t>(RE::MagicSystem::CastingSource::kLeftHand) &&
           value <= static_cast<std::int32_t>(RE::MagicSystem::CastingSource::kInstant);
}

[[nodiscard]] bool IsBoundedMagicScalar(const float a_value) noexcept
{
    return std::isfinite(a_value) && a_value >= 0.0F && a_value <= kMaximumProjectilePower;
}

[[nodiscard]] std::uint64_t NextActionId() noexcept
{
    auto actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    if (actionId == 0)
        actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    return actionId;
}

[[nodiscard]] bool PublishMagicAction(
    const GameplayAction a_action,
    const GameplayActionPayload& a_payload) noexcept
{
    try {
        auto& endpoint = BridgeEndpoint::Get();
        if (!endpoint.IsOperational() || !IsActionInDomain(GameplayDomain::Magic, a_action) ||
            a_payload.TargetHandle.Value != kLocalPlayerHandle.Value || a_payload.TargetLocalFormId == 0 ||
            a_payload.SecondaryHandle.Value != 0 || !std::isfinite(a_payload.ScalarA) ||
            !std::isfinite(a_payload.ScalarB) || !std::isfinite(a_payload.ScalarC) ||
            !std::isfinite(a_payload.ScalarD))
            return false;

        const auto* mapping = endpoint.Mapping();
        if (!mapping || !HasCapability(mapping->Header.ActiveCapabilities.load(std::memory_order_acquire),
                                       Capability::CombatAndMagic))
            return false;

        EventRecord record{};
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayAction);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = endpoint.SnapshotIdentity(0);
        record.Header.Identity.ActionId = NextActionId();
        if (record.Header.Identity.ActionId == 0)
            return false;
        record.Payload.LocalGameplayAction = a_payload;
        record.Payload.LocalGameplayAction.Domain = static_cast<std::uint16_t>(GameplayDomain::Magic);
        record.Payload.LocalGameplayAction.Action = static_cast<std::uint16_t>(a_action);
        return endpoint.TryPushEvent(record);
    } catch (...) {
        return false;
    }
}

void PublishSpellCast(const RE::ActorMagicCaster& a_caster, const RE::Actor& a_actor, RE::MagicItem* a_spell) noexcept
{
    if (!a_spell)
        return;

    const auto casterFormId = a_actor.GetFormID();
    const auto spellFormId = a_spell->GetFormID();
    const auto source = a_caster.GetCastingSource();
    if (!IsValidFormId(casterFormId) || !IsValidFormId(spellFormId) || !IsValidCastingSource(source))
        return;

    std::uint32_t desiredTargetFormId{};
    const auto desiredTarget = std::as_const(a_caster.desiredTarget).get();
    if (desiredTarget) {
        const auto formId = desiredTarget->GetFormID();
        if (IsValidFormId(formId))
            desiredTargetFormId = formId;
    }

    GameplayActionPayload payload{};
    payload.TargetHandle = kLocalPlayerHandle;
    payload.TargetLocalFormId = casterFormId;
    payload.LocalFormIdA = spellFormId;
    payload.LocalFormIdB = desiredTargetFormId;
    payload.ValueA = static_cast<std::int32_t>(source);
    payload.ActionFlags = a_caster.GetIsDualCasting() ? 1u : 0u;
    PublishMagicAction(GameplayAction::CastSpell, payload);
}

void PublishInterruptCast(const RE::ActorMagicCaster& a_caster, const RE::Actor& a_actor) noexcept
{
    const auto casterFormId = a_actor.GetFormID();
    const auto source = a_caster.GetCastingSource();
    if (!IsValidFormId(casterFormId) || !IsValidCastingSource(source))
        return;

    GameplayActionPayload payload{};
    payload.TargetHandle = kLocalPlayerHandle;
    payload.TargetLocalFormId = casterFormId;
    payload.ValueA = static_cast<std::int32_t>(source);
    PublishMagicAction(GameplayAction::InterruptCast, payload);
}

void PublishAddTarget(RE::MagicTarget& a_target, const RE::MagicTarget::AddTargetData& a_data) noexcept
{
    auto* targetActor = a_target.GetTargetAsActor();
    auto* casterActor = a_data.caster ? a_data.caster->As<RE::Actor>() : nullptr;
    const auto* magicItem = a_data.magicItem;
    const auto* effect = a_data.effect;
    const auto* baseEffect = effect ? effect->baseEffect : nullptr;
    if (!targetActor || !magicItem || !effect || !baseEffect || !IsBoundedMagicScalar(a_data.magnitude) ||
        !IsBoundedMagicScalar(a_data.power) || !IsValidCastingSource(a_data.castingSource))
        return;

    const auto targetFormId = targetActor->GetFormID();
    const auto magicItemFormId = magicItem->GetFormID();
    const auto baseEffectFormId = baseEffect->GetFormID();
    if (!IsValidFormId(targetFormId) || !IsValidFormId(magicItemFormId) || !IsValidFormId(baseEffectFormId))
        return;

    std::uint32_t casterFormId{};
    if (casterActor) {
        casterFormId = casterActor->GetFormID();
        if (!IsValidFormId(casterFormId))
            return;
    }

    auto& avatars = AvatarManager::Get();
    if (avatars.IsManagedRemoteActor(targetActor) && casterActor && avatars.IsManagedRemoteActor(casterActor))
        return;

    GameplayActionPayload payload{};
    payload.TargetHandle = kLocalPlayerHandle;
    payload.TargetLocalFormId = targetFormId;
    payload.LocalFormIdA = magicItemFormId;
    payload.LocalFormIdB = baseEffectFormId;
    payload.LocalFormIdC = casterFormId;
    payload.ValueA = static_cast<std::int32_t>(a_data.castingSource);
    payload.ScalarA = a_data.magnitude;
    payload.ScalarB = a_data.power;
    payload.ActionFlags = (a_data.areaTarget ? kMagicEffectAreaTarget : 0u) |
                          (a_data.dualCasted ? kMagicEffectDualCasted : 0u) |
                          (baseEffect->IsHostile() ? kMagicEffectHostile : 0u);
    PublishMagicAction(GameplayAction::ApplyMagicEffect, payload);
}

void PublishRemoveSpell(const RE::Actor& a_actor, const RE::SpellItem& a_spell) noexcept
{
    if (&a_actor != RE::PlayerCharacter::GetSingleton() || !IsValidFormId(a_actor.GetFormID()) ||
        !IsValidFormId(a_spell.GetFormID()))
        return;

    GameplayActionPayload payload{};
    payload.TargetHandle = kLocalPlayerHandle;
    payload.TargetLocalFormId = a_actor.GetFormID();
    payload.LocalFormIdA = a_spell.GetFormID();
    PublishMagicAction(GameplayAction::RemoveSpell, payload);
}

void HookInterruptCast(RE::ActorMagicCaster* a_caster, const bool a_depleteEnergy) noexcept
{
    try {
        auto* actor = a_caster ? a_caster->actor : nullptr;
        if (actor && AvatarManager::Get().IsManagedRemoteActor(actor) && g_remoteMagicApplicationDepth == 0)
            return;

        if (g_originalInterruptCast)
            g_originalInterruptCast(a_caster, a_depleteEnergy);

        if (g_remoteMagicApplicationDepth == 0 && a_caster && actor)
            PublishInterruptCast(*a_caster, *actor);
    } catch (...) {
    }
}

void HookSpellCast(
    RE::ActorMagicCaster* a_caster,
    const bool a_doCast,
    const std::uint32_t a_targetCount,
    RE::MagicItem* a_spell) noexcept
{
    try {
        auto* actor = a_caster ? a_caster->actor : nullptr;
        if (actor && AvatarManager::Get().IsManagedRemoteActor(actor) && g_remoteMagicApplicationDepth == 0)
            return;

        if (g_originalSpellCast)
            g_originalSpellCast(a_caster, a_doCast, a_targetCount, a_spell);

        if (g_remoteMagicApplicationDepth == 0 && a_doCast && a_caster && actor)
            PublishSpellCast(*a_caster, *actor, a_spell);
    } catch (...) {
    }
}

bool HookAddTarget(RE::MagicTarget* a_target, RE::MagicTarget::AddTargetData& a_data) noexcept
{
    bool result{};
    try {
        if (!g_originalAddTarget)
            return false;

        result = g_originalAddTarget(a_target, a_data);
        if (result && g_remoteMagicApplicationDepth == 0 && a_target)
            PublishAddTarget(*a_target, a_data);
    } catch (...) {
    }
    return result;
}

bool HookRemoveSpell(RE::Actor* a_actor, RE::SpellItem* a_spell) noexcept
{
    try {
        if (!g_originalRemoveSpell || !a_actor || !a_spell)
            return false;
        if (AvatarManager::Get().IsManagedRemoteActor(a_actor) && g_remoteMagicApplicationDepth == 0)
            return false;

        const auto result = g_originalRemoveSpell(a_actor, a_spell);
        if (result && g_remoteMagicApplicationDepth == 0)
            PublishRemoveSpell(*a_actor, *a_spell);
        return result;
    } catch (...) {
        return false;
    }
}

void LogHookFailure(const char* a_operation, const int a_status) noexcept
{
    try {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} failed ({})", a_operation, a_status);
    } catch (...) {
    }
}

void LogHookFailure(const char* a_operation) noexcept
{
    try {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: {}", a_operation);
    } catch (...) {
    }
}

void CleanupHooks(
    void* a_interruptTarget,
    const bool a_interruptCreated,
    const bool a_interruptEnabled,
    void* a_spellTarget,
    const bool a_spellCreated,
    const bool a_spellEnabled,
    void* a_addTargetTarget,
    const bool a_addTargetCreated,
    const bool a_addTargetEnabled,
    void* a_removeSpellTarget,
    const bool a_removeSpellCreated,
    const bool a_removeSpellEnabled) noexcept
{
    const auto cleanup = [](const char* a_operation, const MH_STATUS a_status) noexcept {
        if (a_status != MH_OK)
            LogHookFailure(a_operation, static_cast<int>(a_status));
    };

    if (a_removeSpellEnabled)
        cleanup("Actor::RemoveSpell hook disable during cleanup", MH_DisableHook(a_removeSpellTarget));
    if (a_addTargetEnabled)
        cleanup("MagicTarget::AddTarget hook disable during cleanup", MH_DisableHook(a_addTargetTarget));
    if (a_spellEnabled)
        cleanup("ActorMagicCaster::SpellCast hook disable during cleanup", MH_DisableHook(a_spellTarget));
    if (a_interruptEnabled)
        cleanup("ActorMagicCaster::InterruptCastImpl hook disable during cleanup", MH_DisableHook(a_interruptTarget));
    if (a_removeSpellCreated)
        cleanup("Actor::RemoveSpell hook removal during cleanup", MH_RemoveHook(a_removeSpellTarget));
    if (a_addTargetCreated)
        cleanup("MagicTarget::AddTarget hook removal during cleanup", MH_RemoveHook(a_addTargetTarget));
    if (a_spellCreated)
        cleanup("ActorMagicCaster::SpellCast hook removal during cleanup", MH_RemoveHook(a_spellTarget));
    if (a_interruptCreated)
        cleanup("ActorMagicCaster::InterruptCastImpl hook removal during cleanup", MH_RemoveHook(a_interruptTarget));

    g_originalInterruptCast = nullptr;
    g_originalSpellCast = nullptr;
    g_originalAddTarget = nullptr;
    g_originalRemoveSpell = nullptr;
}

void CleanupInstalledHooks() noexcept
{
    CleanupHooks(g_interruptTarget, g_interruptTarget != nullptr, g_interruptTarget != nullptr,
                 g_spellTarget, g_spellTarget != nullptr, g_spellTarget != nullptr,
                 g_addTargetTarget, g_addTargetTarget != nullptr, g_addTargetTarget != nullptr,
                 g_removeSpellTarget, g_removeSpellTarget != nullptr, g_removeSpellTarget != nullptr);
    g_interruptTarget = nullptr;
    g_spellTarget = nullptr;
    g_addTargetTarget = nullptr;
    g_removeSpellTarget = nullptr;
    g_installed.store(false, std::memory_order_release);
}
} // namespace

ScopedRemoteMagicApplication::ScopedRemoteMagicApplication() noexcept
{
    ++g_remoteMagicApplicationDepth;
}

ScopedRemoteMagicApplication::~ScopedRemoteMagicApplication() noexcept
{
    if (g_remoteMagicApplicationDepth != 0)
        --g_remoteMagicApplicationDepth;
}

bool Install() noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        return true;

    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    const auto finish = [](const bool a_result) noexcept {
        g_installing.store(false, std::memory_order_release);
        return a_result;
    };

    try {
        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
            LogHookFailure("MinHook initialization for magic hooks", static_cast<int>(initialize));
            return finish(false);
        }

        const auto vtable = REL::Relocation<void**>(RE::VTABLE_ActorMagicCaster[0]);
        void* const interruptTarget = vtable.get()[8];
        void* const spellTarget = vtable.get()[9];
        REL::Relocation<AddTarget> addTarget{REL::ID(33742)};
        REL::Relocation<RemoveSpell> removeSpell{RELOCATION_ID(37772, 38717)};
        void* const addTargetTarget = reinterpret_cast<void*>(addTarget.address());
        void* const removeSpellTarget = reinterpret_cast<void*>(removeSpell.address());
        if (!interruptTarget || !spellTarget || !addTargetTarget || !removeSpellTarget ||
            !IsExecutableTarget(reinterpret_cast<std::uintptr_t>(interruptTarget)) ||
            !IsExecutableTarget(reinterpret_cast<std::uintptr_t>(spellTarget)) ||
            !IsExecutableTarget(addTarget.address()) || !IsExecutableTarget(removeSpell.address())) {
            LogHookFailure("magic hook target resolution returned a non-executable address");
            return finish(false);
        }
        if (addTarget.offset() != kAddTargetVrRva ||
            std::memcmp(addTargetTarget, kAddTargetVrPrologue.data(), kAddTargetVrPrologue.size()) != 0) {
            LogHookFailure("MagicTarget::AddTarget VR address or prologue mismatch");
            return finish(false);
        }
        if (removeSpell.offset() != kRemoveSpellVrRva ||
            std::memcmp(removeSpellTarget, kRemoveSpellVrPrologue.data(), kRemoveSpellVrPrologue.size()) != 0) {
            LogHookFailure("Actor::RemoveSpell VR address or prologue mismatch");
            return finish(false);
        }

        bool interruptCreated{};
        bool spellCreated{};
        bool addTargetCreated{};
        bool removeSpellCreated{};
        bool interruptEnabled{};
        bool spellEnabled{};
        bool addTargetEnabled{};
        bool removeSpellEnabled{};
        const auto fail = [&](const char* a_operation, const MH_STATUS a_status) noexcept {
            LogHookFailure(a_operation, static_cast<int>(a_status));
            CleanupHooks(interruptTarget, interruptCreated, interruptEnabled, spellTarget, spellCreated, spellEnabled,
                         addTargetTarget, addTargetCreated, addTargetEnabled,
                         removeSpellTarget, removeSpellCreated, removeSpellEnabled);
            return finish(false);
        };

        auto status = MH_CreateHook(interruptTarget, reinterpret_cast<void*>(&HookInterruptCast),
                                    reinterpret_cast<void**>(&g_originalInterruptCast));
        if (status != MH_OK)
            return fail("ActorMagicCaster::InterruptCastImpl hook creation", status);
        interruptCreated = true;

        status = MH_CreateHook(spellTarget, reinterpret_cast<void*>(&HookSpellCast),
                               reinterpret_cast<void**>(&g_originalSpellCast));
        if (status != MH_OK)
            return fail("ActorMagicCaster::SpellCast hook creation", status);
        spellCreated = true;

        status = MH_CreateHook(addTargetTarget, reinterpret_cast<void*>(&HookAddTarget),
                               reinterpret_cast<void**>(&g_originalAddTarget));
        if (status != MH_OK)
            return fail("MagicTarget::AddTarget hook creation at REL::ID(33742)", status);
        addTargetCreated = true;

        status = MH_CreateHook(removeSpellTarget, reinterpret_cast<void*>(&HookRemoveSpell),
                               reinterpret_cast<void**>(&g_originalRemoveSpell));
        if (status != MH_OK)
            return fail("Actor::RemoveSpell hook creation", status);
        removeSpellCreated = true;

        status = MH_EnableHook(interruptTarget);
        if (status != MH_OK)
            return fail("ActorMagicCaster::InterruptCastImpl hook enable", status);
        interruptEnabled = true;

        status = MH_EnableHook(spellTarget);
        if (status != MH_OK)
            return fail("ActorMagicCaster::SpellCast hook enable", status);
        spellEnabled = true;

        status = MH_EnableHook(addTargetTarget);
        if (status != MH_OK)
            return fail("MagicTarget::AddTarget hook enable at REL::ID(33742)", status);
        addTargetEnabled = true;

        status = MH_EnableHook(removeSpellTarget);
        if (status != MH_OK)
            return fail("Actor::RemoveSpell hook enable", status);
        removeSpellEnabled = true;

        g_installed.store(true, std::memory_order_release);
        g_interruptTarget = interruptTarget;
        g_spellTarget = spellTarget;
        g_addTargetTarget = addTargetTarget;
        g_removeSpellTarget = removeSpellTarget;
        g_installing.store(false, std::memory_order_release);
        try {
            SKSE::log::info("SkyrimTogetherVRGameplayBridge: installed ActorMagicCaster slots 8/9, MagicTarget::AddTarget, and Actor::RemoveSpell hooks");
        } catch (...) {
        }
        return true;
    } catch (...) {
        LogHookFailure("exception while installing magic hooks");
        return finish(false);
    }
}

void Uninstall() noexcept
{
    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;

    CleanupInstalledHooks();
    g_installing.store(false, std::memory_order_release);
}
} // namespace SkyrimTogetherVR::GameplayAdapter::MagicHooks
