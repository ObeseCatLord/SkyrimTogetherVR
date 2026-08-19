#include "SummonAuthorityHooks.h"

#include "ActorAuthorityHooks.h"
#include "BridgeEndpoint.h"
#include "VrHookDetachPolicy.h"

#include <MinHook.h>
#include <RE/A/ActiveEffect.h>

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>

namespace SkyrimTogetherVR::GameplayAdapter::SummonAuthorityHooks
{
namespace
{
using SummonCreatureEffectFactory = RE::ActiveEffect* (*)(RE::Actor*, RE::MagicItem*, RE::Effect*);
using Disposition = SummonAuthorityHookPolicy::Disposition;

constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};

struct HookRecord
{
    const char* Name{};
    void* Target{};
    VrHookDetachPolicy::HookState State{};
};

struct Invocation
{
    RE::Actor* Caster{};
    RE::MagicItem* Spell{};
    RE::Effect* Effect{};
    RE::ActiveEffect* Result{};
};

SummonCreatureEffectFactory g_originalSummonCreatureEffectFactory{};
HookRecord g_summonCreatureEffectFactoryHook{"SummonCreatureEffect factory"};
std::atomic_bool g_installAttempted{};
std::atomic_bool g_installed{};
std::atomic_bool g_missingTrampolineLogged{};
std::atomic<std::uint64_t> g_suppressions{};

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

[[nodiscard]] bool IsSpanWithinSegment(
    const std::uintptr_t a_segmentAddress,
    const std::uintptr_t a_segmentSize,
    const std::uintptr_t a_spanAddress,
    const std::uintptr_t a_spanSize) noexcept
{
    if (a_spanSize == 0 || a_spanAddress < a_segmentAddress ||
        a_segmentAddress > std::numeric_limits<std::uintptr_t>::max() - a_segmentSize)
        return false;

    const auto offset = a_spanAddress - a_segmentAddress;
    return offset <= a_segmentSize && a_spanSize <= a_segmentSize - offset;
}

template <std::size_t N>
[[nodiscard]] bool IsVerifiedExecutableTarget(
    const std::uintptr_t a_address,
    const std::array<std::uint8_t, N>& a_prologue,
    const std::uintptr_t a_factoryExtent) noexcept
{
    if (!IsExpectedVrRuntime() || a_factoryExtent < a_prologue.size())
        return false;

    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (!IsSpanWithinSegment(
            static_cast<std::uintptr_t>(text.address()),
            static_cast<std::uintptr_t>(text.size()),
            a_address,
            a_factoryExtent))
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !IsSpanWithinSegment(
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress),
            static_cast<std::uintptr_t>(memory.RegionSize),
            a_address,
            a_factoryExtent))
        return false;

    constexpr DWORD kExecutableProtection =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kExecutableProtection) != 0 &&
           std::memcmp(reinterpret_cast<const void*>(a_address), a_prologue.data(), a_prologue.size()) == 0;
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
        SKSE::log::error(
            "SkyrimTogetherVRGameplayBridge: {} hook disable failed ({})",
            hook.Name,
            static_cast<int>(status));
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
        SKSE::log::error(
            "SkyrimTogetherVRGameplayBridge: {} hook removal failed ({})",
            hook.Name,
            static_cast<int>(status));
    });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] bool DetachHook() noexcept
{
    return VrHookDetachPolicy::Detach(
        g_summonCreatureEffectFactoryHook.State,
        {DisableHook, RemoveHook, &g_summonCreatureEffectFactoryHook});
}

void ClearDetachedState() noexcept
{
    g_originalSummonCreatureEffectFactory = nullptr;
    g_summonCreatureEffectFactoryHook = {"SummonCreatureEffect factory"};
    g_installed.store(false, std::memory_order_release);
    g_missingTrampolineLogged.store(false, std::memory_order_relaxed);
    g_suppressions.store(0, std::memory_order_relaxed);
}

void RetainFaultedHook(const char* a_operation) noexcept
{
    BridgeEndpoint::Get().Fault("SummonCreatureEffect factory hook rollback could not prove detachment");
    LogNoThrow([&] {
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: SummonCreatureEffect factory {} could not prove detachment; retaining the "
            "trampoline so a possible live detour remains callable while the bridge is faulted",
            a_operation);
    });
}

[[nodiscard]] bool RollbackInstall() noexcept
{
    g_installed.store(false, std::memory_order_release);
    if (!DetachHook())
    {
        RetainFaultedHook("install rollback");
        return false;
    }

    ClearDetachedState();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
}

void RecordSuppression(
    const ActorAuthorityHooks::ManagedRemoteActorOperationDisposition a_casterDisposition) noexcept
{
    const auto count = g_suppressions.fetch_add(1, std::memory_order_relaxed) + 1;
    if (SummonAuthorityHookPolicy::ShouldLogAggregate(count))
    {
        const auto* reason = a_casterDisposition == ActorAuthorityHooks::ManagedRemoteActorOperationDisposition::Retiring ?
                                 "retiring managed remote caster" :
                                 "managed remote caster";
        LogNoThrow([&] {
            SKSE::log::debug(
                "SkyrimTogetherVRGameplayBridge: suppressed SummonCreatureEffect factory for {} (count={})",
                reason,
                count);
        });
    }
}

void RecordMissingTrampoline() noexcept
{
    if (!g_missingTrampolineLogged.exchange(true, std::memory_order_relaxed))
    {
        LogNoThrow([] {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: enabled SummonCreatureEffect factory detour has no trampoline; "
                "rejecting the summon factory call");
        });
    }
}

void ExecuteInvocation(
    void* const a_context,
    const ActorAuthorityHooks::ManagedRemoteActorOperationDisposition a_casterDisposition) noexcept
{
    auto& invocation = *static_cast<Invocation*>(a_context);
    const auto disposition = SummonAuthorityHookPolicy::Classify(
        a_casterDisposition == ActorAuthorityHooks::ManagedRemoteActorOperationDisposition::ManagedRemote,
        a_casterDisposition == ActorAuthorityHooks::ManagedRemoteActorOperationDisposition::Retiring);
    if (disposition == Disposition::Suppress)
    {
        // The dispatcher accepts nullptr and omits effect post-initialization.
        RecordSuppression(a_casterDisposition);
        return;
    }

    const auto original = g_originalSummonCreatureEffectFactory;
    if (!original)
    {
        RecordMissingTrampoline();
        return;
    }
    invocation.Result = original(invocation.Caster, invocation.Spell, invocation.Effect);
}

RE::ActiveEffect* HookSummonCreatureEffectFactory(
    RE::Actor* a_caster,
    RE::MagicItem* a_spell,
    RE::Effect* a_effect)
{
    Invocation invocation{a_caster, a_spell, a_effect, nullptr};
    if (!ActorAuthorityHooks::WithManagedRemoteActorLease(a_caster, ExecuteInvocation, &invocation))
    {
        // A missing lease operation cannot safely authorize the factory call.
        RecordSuppression(ActorAuthorityHooks::ManagedRemoteActorOperationDisposition::Retiring);
        return nullptr;
    }
    return invocation.Result;
}

[[nodiscard]] bool CreateAndEnableHook() noexcept
{
    void* trampoline{};
    const auto create = MH_CreateHook(
        g_summonCreatureEffectFactoryHook.Target,
        reinterpret_cast<void*>(&HookSummonCreatureEffectFactory),
        &trampoline);
    if (create != MH_OK)
    {
        LogNoThrow([&] {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: SummonCreatureEffect factory hook creation failed ({})",
                static_cast<int>(create));
        });
        return false;
    }
    g_summonCreatureEffectFactoryHook.State.Created = true;
    g_originalSummonCreatureEffectFactory = reinterpret_cast<SummonCreatureEffectFactory>(trampoline);
    if (!g_originalSummonCreatureEffectFactory || trampoline == g_summonCreatureEffectFactoryHook.Target)
    {
        LogNoThrow([] {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: SummonCreatureEffect factory hook returned an invalid trampoline");
        });
        return false;
    }

    // A failed enable does not prove MinHook left the target unchanged.
    g_summonCreatureEffectFactoryHook.State.Enabled = true;
    const auto enable = MH_EnableHook(g_summonCreatureEffectFactoryHook.Target);
    if (enable != MH_OK)
    {
        LogNoThrow([&] {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: SummonCreatureEffect factory hook enable failed ({})",
                static_cast<int>(enable));
        });
        return false;
    }
    return true;
}
} // namespace

bool Install() noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        return true;

    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    try
    {
        if (!SummonAuthorityHookPolicy::HasPinnedTargetConfiguration() || !IsExpectedVrRuntime())
        {
            LogNoThrow([] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: SummonCreatureEffect factory hook requires exact Skyrim VR 1.4.15.0");
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED)
        {
            LogNoThrow([&] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: MinHook initialization failed for SummonCreatureEffect factory ({})",
                    static_cast<int>(initialize));
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        REL::Relocation<SummonCreatureEffectFactory> target{
            REL::Offset(SummonAuthorityHookPolicy::kSummonCreatureEffectFactoryVrRva)};
        const auto moduleBase = REL::Module::get().base();
        if (moduleBase == 0 ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() -
                             SummonAuthorityHookPolicy::kSummonCreatureEffectFactoryVrRva ||
            target.offset() != SummonAuthorityHookPolicy::kSummonCreatureEffectFactoryVrRva ||
            target.address() != moduleBase + SummonAuthorityHookPolicy::kSummonCreatureEffectFactoryVrRva ||
            !IsVerifiedExecutableTarget(
                target.address(),
                SummonAuthorityHookPolicy::kSummonCreatureEffectFactoryVrPrologue,
                SummonAuthorityHookPolicy::kSummonCreatureEffectFactoryExtent))
        {
            LogNoThrow([&] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: SummonCreatureEffect factory target validation failed at RVA 0x{:X}",
                    target.offset());
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        g_summonCreatureEffectFactoryHook.Target = reinterpret_cast<void*>(target.address());
        if (!CreateAndEnableHook())
        {
            if (RollbackInstall())
                return false;
            return false;
        }

        g_installed.store(true, std::memory_order_release);
        LogNoThrow([] {
            SKSE::log::info(
                "SkyrimTogetherVRGameplayBridge: installed exact SummonCreatureEffect authority hook at RVA 0x{:X}",
                SummonAuthorityHookPolicy::kSummonCreatureEffectFactoryVrRva);
        });
        return true;
    }
    catch (...)
    {
        BridgeEndpoint::Get().Fault("SummonCreatureEffect factory hook target resolution threw");
        LogNoThrow([] {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: SummonCreatureEffect factory hook installation rejected an exception");
        });
        if (RollbackInstall())
            return false;
        return false;
    }
}

bool Uninstall() noexcept
{
    if (!g_summonCreatureEffectFactoryHook.State.Created)
    {
        ClearDetachedState();
        g_installAttempted.store(false, std::memory_order_release);
        return true;
    }
    if (!DetachHook())
    {
        RetainFaultedHook("uninstall");
        return false;
    }

    const auto suppressions = g_suppressions.load(std::memory_order_relaxed);
    LogNoThrow([&] {
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: removed SummonCreatureEffect authority hook (suppressed={})",
            suppressions);
    });
    ClearDetachedState();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::SummonAuthorityHooks
