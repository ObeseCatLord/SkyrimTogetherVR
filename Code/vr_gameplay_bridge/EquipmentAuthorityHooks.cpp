#include "EquipmentAuthorityHooks.h"

#include "ActorAuthorityHooks.h"
#include "BridgeEndpoint.h"
#include "VrHookDetachPolicy.h"

#include <MinHook.h>
#include <RE/A/ActorEquipManager.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <limits>

namespace SkyrimTogetherVR::GameplayAdapter::EquipmentAuthorityHooks
{
namespace
{
using LowEquipmentMutation = RE::AIProcess* (*) (
    RE::ActorEquipManager*,
    RE::Actor*,
    void*,
    void*);
// The game-owned payload layouts are intentionally opaque here. Their low
// entries have the same four-register pointer ABI, while only Object's +0x24
// rejection flag is independently proven and touched below.
struct ObjectEquipData;
struct MagicEquipData;
struct ShoutEquipData;
using EquipObjectMutation = RE::AIProcess* (*) (
    RE::ActorEquipManager*,
    RE::Actor*,
    RE::TESBoundObject*,
    ObjectEquipData*);
using EquipSpellMutation = RE::AIProcess* (*) (
    RE::ActorEquipManager*,
    RE::Actor*,
    RE::SpellItem*,
    MagicEquipData*);
using EquipShoutMutation = RE::AIProcess* (*) (
    RE::ActorEquipManager*,
    RE::Actor*,
    RE::TESShout*,
    ShoutEquipData*);
using PublicUnequipSpell = void (*) (
    RE::ActorEquipManager*,
    RE::Actor*,
    RE::SpellItem*,
    const RE::BGSEquipSlot*);
using PublicUnequipShout = void (*) (
    RE::ActorEquipManager*,
    RE::Actor*,
    RE::TESShout*);
using Operation = EquipmentAuthorityHookPolicy::Operation;
using Disposition = EquipmentAuthorityHookPolicy::Disposition;

constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};
constexpr std::size_t kEquipDataApplyNowOffset = 0x24;

struct HookRecord
{
    const char* Name{};
    Operation Operation{};
    void* Target{};
    LowEquipmentMutation Original{};
    VrHookDetachPolicy::HookState State{};
    std::atomic<std::uint64_t> Suppressions{};
    std::atomic_bool MissingTrampolineLogged{};
};

struct Invocation
{
    HookRecord* Hook{};
    RE::ActorEquipManager* Manager{};
    RE::Actor* Actor{};
    void* Form{};
    void* Payload{};
    RE::AIProcess* Result{};
};

HookRecord g_equipObjectHook{"ActorEquipManager::EquipObject", Operation::EquipObject};
HookRecord g_unequipObjectHook{"ActorEquipManager::UnequipObject", Operation::UnequipObject};
HookRecord g_equipSpellHook{"ActorEquipManager::EquipSpell", Operation::EquipSpell};
HookRecord g_unequipSpellHook{"ActorEquipManager::UnequipSpell", Operation::UnequipSpell};
HookRecord g_equipShoutHook{"ActorEquipManager::EquipShout", Operation::EquipShout};
HookRecord g_unequipShoutHook{"ActorEquipManager::UnequipShout", Operation::UnequipShout};
PublicUnequipSpell g_publicUnequipSpell{};
PublicUnequipShout g_publicUnequipShout{};
std::atomic_bool g_installAttempted{};
std::atomic_bool g_installed{};
std::atomic_bool g_replayDepthOverflowLogged{};
std::atomic_bool g_inventoryRemovalDepthOverflowLogged{};
thread_local std::uint32_t g_authoritativeReplayDepth{};
thread_local std::uint32_t g_admittedInventoryRemovalDepth{};

template <class T>
void LogNoThrow(T&& a_action) noexcept
{
    try
    {
        a_action();
    }
    catch (...)
    {
    }
}

[[nodiscard]] bool IsExpectedVrRuntime() noexcept
{
    return REL::Module::IsVR() && REL::Module::get().version() == kExpectedSkyrimVrRuntime;
}

[[nodiscard]] bool IsSpanWithin(
    const std::uintptr_t a_start,
    const std::uintptr_t a_size,
    const std::uintptr_t a_spanStart,
    const std::uintptr_t a_spanSize) noexcept
{
    if (a_spanSize == 0 || a_spanStart < a_start || a_start > std::numeric_limits<std::uintptr_t>::max() - a_size)
        return false;
    const auto offset = a_spanStart - a_start;
    return offset <= a_size && a_spanSize <= a_size - offset;
}

template <std::size_t N>
[[nodiscard]] bool IsVerifiedExecutableTarget(
    const std::uintptr_t a_address,
    const std::array<std::uint8_t, N>& a_prologue) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (!IsSpanWithin(
            static_cast<std::uintptr_t>(text.address()),
            static_cast<std::uintptr_t>(text.size()),
            a_address,
            static_cast<std::uintptr_t>(a_prologue.size())))
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    if (!IsSpanWithin(
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress),
            static_cast<std::uintptr_t>(memory.RegionSize),
            a_address,
            static_cast<std::uintptr_t>(a_prologue.size())))
        return false;

    constexpr DWORD kExecutableProtection =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kExecutableProtection) != 0 &&
           std::memcmp(reinterpret_cast<const void*>(a_address), a_prologue.data(), a_prologue.size()) == 0;
}

void RecordSuppression(HookRecord& ar_hook, const char* a_reason) noexcept
{
    const auto count = ar_hook.Suppressions.fetch_add(1, std::memory_order_relaxed) + 1;
    if (EquipmentAuthorityHookPolicy::ShouldLogAggregate(count))
    {
        LogNoThrow([&] {
            SKSE::log::debug(
                "SkyrimTogetherVRGameplayBridge: {} {} (aggregate={})",
                ar_hook.Name,
                a_reason,
                count);
        });
    }
}

void RecordMissingTrampoline(HookRecord& ar_hook) noexcept
{
    if (!ar_hook.MissingTrampolineLogged.exchange(true, std::memory_order_relaxed))
    {
        LogNoThrow([&] {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: enabled {} detour has no trampoline; suppressing mutation",
                ar_hook.Name);
        });
    }
}

[[nodiscard]] VrHookDetachPolicy::OperationResult DisableHook(void* a_context) noexcept
{
    const auto& hook = *static_cast<const HookRecord*>(a_context);
    const auto status = MH_DisableHook(hook.Target);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_DISABLED)
        return VrHookDetachPolicy::OperationResult::AlreadyDisabled;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    LogNoThrow([&] {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook disable failed ({})", hook.Name, static_cast<int>(status));
    });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult RemoveHook(void* a_context) noexcept
{
    const auto& hook = *static_cast<const HookRecord*>(a_context);
    const auto status = MH_RemoveHook(hook.Target);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    LogNoThrow([&] {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook removal failed ({})", hook.Name, static_cast<int>(status));
    });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] bool DetachHook(HookRecord& ar_hook) noexcept
{
    return VrHookDetachPolicy::Detach(ar_hook.State, {DisableHook, RemoveHook, &ar_hook});
}

void ResetHook(HookRecord& ar_hook, const char* a_name, const Operation a_operation) noexcept
{
    ar_hook.Name = a_name;
    ar_hook.Operation = a_operation;
    ar_hook.Target = nullptr;
    ar_hook.Original = nullptr;
    ar_hook.State = {};
    ar_hook.Suppressions.store(0, std::memory_order_relaxed);
    ar_hook.MissingTrampolineLogged.store(false, std::memory_order_relaxed);
}

void ClearDetachedState() noexcept
{
    ResetHook(g_equipObjectHook, "ActorEquipManager::EquipObject", Operation::EquipObject);
    ResetHook(g_unequipObjectHook, "ActorEquipManager::UnequipObject", Operation::UnequipObject);
    ResetHook(g_equipSpellHook, "ActorEquipManager::EquipSpell", Operation::EquipSpell);
    ResetHook(g_unequipSpellHook, "ActorEquipManager::UnequipSpell", Operation::UnequipSpell);
    ResetHook(g_equipShoutHook, "ActorEquipManager::EquipShout", Operation::EquipShout);
    ResetHook(g_unequipShoutHook, "ActorEquipManager::UnequipShout", Operation::UnequipShout);
    g_publicUnequipSpell = nullptr;
    g_publicUnequipShout = nullptr;
    g_replayDepthOverflowLogged.store(false, std::memory_order_relaxed);
    g_inventoryRemovalDepthOverflowLogged.store(false, std::memory_order_relaxed);
    g_installed.store(false, std::memory_order_release);
}

[[nodiscard]] bool RollbackInstall() noexcept
{
    g_installed.store(false, std::memory_order_release);
    const bool unequipShoutDetached = DetachHook(g_unequipShoutHook);
    const bool equipShoutDetached = DetachHook(g_equipShoutHook);
    const bool unequipSpellDetached = DetachHook(g_unequipSpellHook);
    const bool equipSpellDetached = DetachHook(g_equipSpellHook);
    const bool unequipObjectDetached = DetachHook(g_unequipObjectHook);
    const bool equipObjectDetached = DetachHook(g_equipObjectHook);
    if (unequipShoutDetached && equipShoutDetached && unequipSpellDetached && equipSpellDetached &&
        unequipObjectDetached && equipObjectDetached)
    {
        ClearDetachedState();
        g_installAttempted.store(false, std::memory_order_release);
        return true;
    }

    BridgeEndpoint::Get().Fault("equipment authority hook rollback could not prove detachment");
    LogNoThrow([] {
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: equipment authority hook rollback could not prove detachment; retaining callable trampolines");
    });
    return false;
}

[[nodiscard]] bool CreateAndEnableHook(HookRecord& ar_hook, void* a_detour) noexcept
{
    void* original{};
    const auto create = MH_CreateHook(ar_hook.Target, a_detour, &original);
    if (create != MH_OK)
    {
        LogNoThrow([&] {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook creation failed ({})", ar_hook.Name, static_cast<int>(create));
        });
        return false;
    }
    ar_hook.State.Created = true;
    ar_hook.Original = reinterpret_cast<LowEquipmentMutation>(original);
    if (!ar_hook.Original || original == ar_hook.Target)
    {
        LogNoThrow([&] {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook returned an invalid trampoline", ar_hook.Name);
        });
        return false;
    }

    // Preserve Created and Enabled until rollback proves the target detached.
    ar_hook.State.Enabled = true;
    const auto enable = MH_EnableHook(ar_hook.Target);
    if (enable != MH_OK)
    {
        LogNoThrow([&] {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook enable failed ({})", ar_hook.Name, static_cast<int>(enable));
        });
        return false;
    }
    return true;
}

void ExecuteInvocation(
    void* const a_context,
    const ActorAuthorityHooks::ManagedRemoteActorOperationDisposition a_actorDisposition) noexcept
{
    auto& invocation = *static_cast<Invocation*>(a_context);
    auto& hook = *invocation.Hook;
    const auto disposition = EquipmentAuthorityHookPolicy::Classify(
        hook.Operation,
        a_actorDisposition == ActorAuthorityHooks::ManagedRemoteActorOperationDisposition::ManagedRemote,
        a_actorDisposition == ActorAuthorityHooks::ManagedRemoteActorOperationDisposition::Retiring,
        IsAuthoritativeEquipmentReplay(),
        IsAdmittedInventoryRemoval());
    if (disposition == Disposition::Suppress)
    {
        // UnequipObject callers test this payload byte after the low mutator.
        // Keeping it false preserves the engine's rejected-unequip contract.
        if (hook.Operation == Operation::UnequipObject && invocation.Payload)
        {
            auto* const applyNow = static_cast<std::uint8_t*>(invocation.Payload) + kEquipDataApplyNowOffset;
            *applyNow = 0;
        }
        RecordSuppression(
            hook,
            a_actorDisposition == ActorAuthorityHooks::ManagedRemoteActorOperationDisposition::Retiring ?
                "rejected mutation for retiring remote actor" :
                "rejected non-authoritative remote mutation");
        return;
    }

    const auto original = hook.Original;
    if (!original)
    {
        RecordMissingTrampoline(hook);
        return;
    }
    invocation.Result = original(invocation.Manager, invocation.Actor, invocation.Form, invocation.Payload);
}

[[nodiscard]] RE::AIProcess* Invoke(
    HookRecord& ar_hook,
    RE::ActorEquipManager* a_manager,
    RE::Actor* a_actor,
    void* a_form,
    void* a_payload) noexcept
{
    Invocation invocation{&ar_hook, a_manager, a_actor, a_form, a_payload, nullptr};
    if (!ActorAuthorityHooks::WithManagedRemoteActorLease(a_actor, ExecuteInvocation, &invocation))
    {
        RecordSuppression(ar_hook, "rejected because the managed-actor registry operation was unavailable");
        return nullptr;
    }
    return invocation.Result;
}

RE::AIProcess* HookEquipObject(
    RE::ActorEquipManager* a_manager,
    RE::Actor* a_actor,
    RE::TESBoundObject* a_object,
    ObjectEquipData* a_data) noexcept
{
    return Invoke(g_equipObjectHook, a_manager, a_actor, a_object, a_data);
}

RE::AIProcess* HookUnequipObject(
    RE::ActorEquipManager* a_manager,
    RE::Actor* a_actor,
    RE::TESBoundObject* a_object,
    ObjectEquipData* a_data) noexcept
{
    return Invoke(g_unequipObjectHook, a_manager, a_actor, a_object, a_data);
}

RE::AIProcess* HookEquipSpell(
    RE::ActorEquipManager* a_manager,
    RE::Actor* a_actor,
    RE::SpellItem* a_spell,
    MagicEquipData* a_data) noexcept
{
    return Invoke(g_equipSpellHook, a_manager, a_actor, a_spell, a_data);
}

RE::AIProcess* HookUnequipSpell(
    RE::ActorEquipManager* a_manager,
    RE::Actor* a_actor,
    RE::SpellItem* a_spell,
    MagicEquipData* a_data) noexcept
{
    return Invoke(g_unequipSpellHook, a_manager, a_actor, a_spell, a_data);
}

RE::AIProcess* HookEquipShout(
    RE::ActorEquipManager* a_manager,
    RE::Actor* a_actor,
    RE::TESShout* a_shout,
    ShoutEquipData* a_data) noexcept
{
    return Invoke(g_equipShoutHook, a_manager, a_actor, a_shout, a_data);
}

RE::AIProcess* HookUnequipShout(
    RE::ActorEquipManager* a_manager,
    RE::Actor* a_actor,
    RE::TESShout* a_shout,
    ShoutEquipData* a_data) noexcept
{
    return Invoke(g_unequipShoutHook, a_manager, a_actor, a_shout, a_data);
}
} // namespace

ScopedAuthoritativeEquipmentReplay::ScopedAuthoritativeEquipmentReplay() noexcept
{
    if (!EquipmentAuthorityHookPolicy::CanEnterScope(g_authoritativeReplayDepth))
    {
        if (!g_replayDepthOverflowLogged.exchange(true, std::memory_order_relaxed))
            LogNoThrow([] {
                SKSE::log::critical("SkyrimTogetherVRGameplayBridge: equipment replay scope depth overflowed; rejecting further nested replay scopes");
            });
        return;
    }
    g_authoritativeReplayDepth = EquipmentAuthorityHookPolicy::EnterScope(g_authoritativeReplayDepth);
    _entered = true;
}

ScopedAuthoritativeEquipmentReplay::~ScopedAuthoritativeEquipmentReplay() noexcept
{
    if (_entered)
        g_authoritativeReplayDepth = EquipmentAuthorityHookPolicy::LeaveScope(g_authoritativeReplayDepth);
}

ScopedAdmittedInventoryRemoval::ScopedAdmittedInventoryRemoval() noexcept
{
    if (!EquipmentAuthorityHookPolicy::CanEnterScope(g_admittedInventoryRemovalDepth))
    {
        if (!g_inventoryRemovalDepthOverflowLogged.exchange(true, std::memory_order_relaxed))
            LogNoThrow([] {
                SKSE::log::critical("SkyrimTogetherVRGameplayBridge: inventory-removal scope depth overflowed; rejecting further nested removal scopes");
            });
        return;
    }
    g_admittedInventoryRemovalDepth = EquipmentAuthorityHookPolicy::EnterScope(g_admittedInventoryRemovalDepth);
    _entered = true;
}

ScopedAdmittedInventoryRemoval::~ScopedAdmittedInventoryRemoval() noexcept
{
    if (_entered)
        g_admittedInventoryRemovalDepth = EquipmentAuthorityHookPolicy::LeaveScope(g_admittedInventoryRemovalDepth);
}

bool IsAuthoritativeEquipmentReplay() noexcept
{
    return g_authoritativeReplayDepth != 0;
}

bool IsAdmittedInventoryRemoval() noexcept
{
    return g_admittedInventoryRemovalDepth != 0;
}

bool UnequipSpellSynchronously(
    RE::ActorEquipManager* const a_manager,
    RE::Actor* const a_actor,
    RE::SpellItem* const a_spell,
    const RE::BGSEquipSlot* const a_slot) noexcept
{
    const auto target = g_publicUnequipSpell;
    if (!g_installed.load(std::memory_order_acquire) || !IsAuthoritativeEquipmentReplay() || !target ||
        !a_manager || !a_actor || !a_spell)
        return false;
    try
    {
        target(a_manager, a_actor, a_spell, a_slot);
        return true;
    }
    catch (...)
    {
        BridgeEndpoint::Get().Fault("synchronous remote spell unequip threw");
        return false;
    }
}

bool UnequipShoutSynchronously(
    RE::ActorEquipManager* const a_manager,
    RE::Actor* const a_actor,
    RE::TESShout* const a_shout) noexcept
{
    const auto target = g_publicUnequipShout;
    if (!g_installed.load(std::memory_order_acquire) || !IsAuthoritativeEquipmentReplay() || !target ||
        !a_manager || !a_actor || !a_shout)
        return false;
    try
    {
        target(a_manager, a_actor, a_shout);
        return true;
    }
    catch (...)
    {
        BridgeEndpoint::Get().Fault("synchronous remote shout unequip threw");
        return false;
    }
}

bool Install() noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        return true;

    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    try
    {
        if (!EquipmentAuthorityHookPolicy::HasPinnedTargetConfiguration() || !IsExpectedVrRuntime())
        {
            LogNoThrow([] {
                SKSE::log::error("SkyrimTogetherVRGameplayBridge: equipment authority hooks require exact Skyrim VR 1.4.15.0");
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED)
        {
            LogNoThrow([&] {
                SKSE::log::error("SkyrimTogetherVRGameplayBridge: MinHook initialization failed for equipment authority hooks ({})", static_cast<int>(initialize));
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        REL::Relocation<LowEquipmentMutation> equipObject{REL::Offset(EquipmentAuthorityHookPolicy::kEquipObjectVrRva)};
        REL::Relocation<LowEquipmentMutation> unequipObject{REL::Offset(EquipmentAuthorityHookPolicy::kUnequipObjectVrRva)};
        REL::Relocation<LowEquipmentMutation> equipSpell{REL::Offset(EquipmentAuthorityHookPolicy::kEquipSpellVrRva)};
        REL::Relocation<LowEquipmentMutation> unequipSpell{REL::Offset(EquipmentAuthorityHookPolicy::kUnequipSpellVrRva)};
        REL::Relocation<LowEquipmentMutation> equipShout{REL::Offset(EquipmentAuthorityHookPolicy::kEquipShoutVrRva)};
        REL::Relocation<LowEquipmentMutation> unequipShout{REL::Offset(EquipmentAuthorityHookPolicy::kUnequipShoutVrRva)};
        REL::Relocation<PublicUnequipSpell> publicUnequipSpell{REL::Offset(EquipmentAuthorityHookPolicy::kPublicUnequipSpellVrRva)};
        REL::Relocation<PublicUnequipShout> publicUnequipShout{REL::Offset(EquipmentAuthorityHookPolicy::kPublicUnequipShoutVrRva)};
        const auto moduleBase = REL::Module::get().base();
        if (moduleBase == 0 ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - EquipmentAuthorityHookPolicy::kEquipObjectVrRva ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - EquipmentAuthorityHookPolicy::kUnequipObjectVrRva ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - EquipmentAuthorityHookPolicy::kEquipSpellVrRva ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - EquipmentAuthorityHookPolicy::kUnequipSpellVrRva ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - EquipmentAuthorityHookPolicy::kEquipShoutVrRva ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - EquipmentAuthorityHookPolicy::kUnequipShoutVrRva ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - EquipmentAuthorityHookPolicy::kPublicUnequipSpellVrRva ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - EquipmentAuthorityHookPolicy::kPublicUnequipShoutVrRva ||
            equipObject.offset() != EquipmentAuthorityHookPolicy::kEquipObjectVrRva ||
            equipObject.address() != moduleBase + EquipmentAuthorityHookPolicy::kEquipObjectVrRva ||
            !IsVerifiedExecutableTarget(equipObject.address(), EquipmentAuthorityHookPolicy::kEquipObjectVrPrologue) ||
            unequipObject.offset() != EquipmentAuthorityHookPolicy::kUnequipObjectVrRva ||
            unequipObject.address() != moduleBase + EquipmentAuthorityHookPolicy::kUnequipObjectVrRva ||
            !IsVerifiedExecutableTarget(unequipObject.address(), EquipmentAuthorityHookPolicy::kUnequipObjectVrPrologue) ||
            equipSpell.offset() != EquipmentAuthorityHookPolicy::kEquipSpellVrRva ||
            equipSpell.address() != moduleBase + EquipmentAuthorityHookPolicy::kEquipSpellVrRva ||
            !IsVerifiedExecutableTarget(equipSpell.address(), EquipmentAuthorityHookPolicy::kEquipSpellVrPrologue) ||
            unequipSpell.offset() != EquipmentAuthorityHookPolicy::kUnequipSpellVrRva ||
            unequipSpell.address() != moduleBase + EquipmentAuthorityHookPolicy::kUnequipSpellVrRva ||
            !IsVerifiedExecutableTarget(unequipSpell.address(), EquipmentAuthorityHookPolicy::kUnequipSpellVrPrologue) ||
            equipShout.offset() != EquipmentAuthorityHookPolicy::kEquipShoutVrRva ||
            equipShout.address() != moduleBase + EquipmentAuthorityHookPolicy::kEquipShoutVrRva ||
            !IsVerifiedExecutableTarget(equipShout.address(), EquipmentAuthorityHookPolicy::kEquipShoutVrPrologue) ||
            unequipShout.offset() != EquipmentAuthorityHookPolicy::kUnequipShoutVrRva ||
            unequipShout.address() != moduleBase + EquipmentAuthorityHookPolicy::kUnequipShoutVrRva ||
            !IsVerifiedExecutableTarget(unequipShout.address(), EquipmentAuthorityHookPolicy::kUnequipShoutVrPrologue) ||
            publicUnequipSpell.offset() != EquipmentAuthorityHookPolicy::kPublicUnequipSpellVrRva ||
            publicUnequipSpell.address() != moduleBase + EquipmentAuthorityHookPolicy::kPublicUnequipSpellVrRva ||
            !IsVerifiedExecutableTarget(publicUnequipSpell.address(), EquipmentAuthorityHookPolicy::kPublicUnequipSpellVrPrologue) ||
            publicUnequipShout.offset() != EquipmentAuthorityHookPolicy::kPublicUnequipShoutVrRva ||
            publicUnequipShout.address() != moduleBase + EquipmentAuthorityHookPolicy::kPublicUnequipShoutVrRva ||
            !IsVerifiedExecutableTarget(publicUnequipShout.address(), EquipmentAuthorityHookPolicy::kPublicUnequipShoutVrPrologue))
        {
            LogNoThrow([] {
                SKSE::log::error("SkyrimTogetherVRGameplayBridge: equipment authority targets failed exact validation");
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        g_equipObjectHook.Target = reinterpret_cast<void*>(equipObject.address());
        g_unequipObjectHook.Target = reinterpret_cast<void*>(unequipObject.address());
        g_equipSpellHook.Target = reinterpret_cast<void*>(equipSpell.address());
        g_unequipSpellHook.Target = reinterpret_cast<void*>(unequipSpell.address());
        g_equipShoutHook.Target = reinterpret_cast<void*>(equipShout.address());
        g_unequipShoutHook.Target = reinterpret_cast<void*>(unequipShout.address());
        g_publicUnequipSpell = publicUnequipSpell.get();
        g_publicUnequipShout = publicUnequipShout.get();
        if (!CreateAndEnableHook(g_equipObjectHook, reinterpret_cast<void*>(&HookEquipObject)) ||
            !CreateAndEnableHook(g_unequipObjectHook, reinterpret_cast<void*>(&HookUnequipObject)) ||
            !CreateAndEnableHook(g_equipSpellHook, reinterpret_cast<void*>(&HookEquipSpell)) ||
            !CreateAndEnableHook(g_unequipSpellHook, reinterpret_cast<void*>(&HookUnequipSpell)) ||
            !CreateAndEnableHook(g_equipShoutHook, reinterpret_cast<void*>(&HookEquipShout)) ||
            !CreateAndEnableHook(g_unequipShoutHook, reinterpret_cast<void*>(&HookUnequipShout)))
        {
            static_cast<void>(RollbackInstall());
            return false;
        }

        g_installed.store(true, std::memory_order_release);
        LogNoThrow([] {
            SKSE::log::info(
                "SkyrimTogetherVRGameplayBridge: installed verified equipment authority hooks (object, spell, shout equip/unequip)");
        });
        return true;
    }
    catch (...)
    {
        BridgeEndpoint::Get().Fault("equipment authority hook target resolution threw");
        LogNoThrow([] {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: equipment authority hook installation rejected an exception");
        });
        static_cast<void>(RollbackInstall());
        return false;
    }
}

bool Uninstall() noexcept
{
    const bool unequipShoutDetached = DetachHook(g_unequipShoutHook);
    const bool equipShoutDetached = DetachHook(g_equipShoutHook);
    const bool unequipSpellDetached = DetachHook(g_unequipSpellHook);
    const bool equipSpellDetached = DetachHook(g_equipSpellHook);
    const bool unequipObjectDetached = DetachHook(g_unequipObjectHook);
    const bool equipObjectDetached = DetachHook(g_equipObjectHook);
    if (!unequipShoutDetached || !equipShoutDetached || !unequipSpellDetached || !equipSpellDetached ||
        !unequipObjectDetached || !equipObjectDetached)
    {
        BridgeEndpoint::Get().Fault("equipment authority hook uninstall could not prove detachment");
        LogNoThrow([] {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: equipment authority hook uninstall incomplete; preserving callable trampolines");
        });
        return false;
    }

    ClearDetachedState();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::EquipmentAuthorityHooks
