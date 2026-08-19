#include "DropHooks.h"

#include "BridgeEndpoint.h"
#include "LocalGameplayCapture.h"
#include "VrHookDetachPolicy.h"
#include "VrNoThrow.h"

#include <MinHook.h>
#include <RE/E/ExtraDataList.h>
#include <RE/E/ExtraUniqueID.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESBoundObject.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>

namespace SkyrimTogetherVR::GameplayAdapter::DropHooks
{
namespace
{
using DropObject = RE::ObjectRefHandle* (*) (
    RE::PlayerCharacter*,
    RE::ObjectRefHandle*,
    RE::TESBoundObject*,
    RE::ExtraDataList*,
    std::int32_t,
    RE::NiPoint3*,
    RE::NiPoint3*);

constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};
constexpr std::size_t kMaximumPendingDropDepth = 8;

struct PendingDropScope
{
    DropHookPolicy::PendingDrop Pending{};
    bool HadNestedCall{};
};

DropObject g_originalDropObject{};
void* g_hookTarget{};
VrHookDetachPolicy::HookState g_hookState{};
std::atomic_bool g_installAttempted{};
std::atomic_bool g_installed{};
std::atomic_bool g_missingTrampolineLogged{};
std::atomic<std::uint64_t> g_ambiguousEvents{};
std::atomic<std::uint64_t> g_mismatchedEvents{};
thread_local std::array<PendingDropScope, kMaximumPendingDropDepth> g_pendingDrops{};
thread_local std::size_t g_pendingDropDepth{};
thread_local std::uint64_t g_nextPendingDropGeneration{};

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
    const std::array<std::uint8_t, N>& a_prologue) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (!IsSpanWithinSegment(
            static_cast<std::uintptr_t>(text.address()),
            static_cast<std::uintptr_t>(text.size()),
            a_address,
            static_cast<std::uintptr_t>(a_prologue.size())))
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    if (!IsSpanWithinSegment(
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

[[nodiscard]] bool IsReadableVtableSlot(
    const std::uintptr_t a_vtableSlotAddress,
    const std::uintptr_t a_expectedTarget) noexcept
{
    if (a_vtableSlotAddress == 0 || a_expectedTarget == 0 ||
        a_vtableSlotAddress > std::numeric_limits<std::uintptr_t>::max() - sizeof(std::uintptr_t))
        return false;

    const auto rdata = REL::Module::get().segment(REL::Segment::rdata);
    if (!IsSpanWithinSegment(
            static_cast<std::uintptr_t>(rdata.address()),
            static_cast<std::uintptr_t>(rdata.size()),
            a_vtableSlotAddress,
            sizeof(std::uintptr_t)))
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_vtableSlotAddress), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !IsSpanWithinSegment(
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress),
            static_cast<std::uintptr_t>(memory.RegionSize),
            a_vtableSlotAddress,
            sizeof(std::uintptr_t)))
        return false;

    const auto protection = memory.Protect & 0xFF;
    if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
        protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
        protection != PAGE_EXECUTE_WRITECOPY)
        return false;

    const auto* const slot = reinterpret_cast<const std::uintptr_t*>(a_vtableSlotAddress);
    return *slot == a_expectedTarget;
}

[[nodiscard]] std::uint64_t NextPendingDropGeneration() noexcept
{
    ++g_nextPendingDropGeneration;
    if (g_nextPendingDropGeneration == 0)
        ++g_nextPendingDropGeneration;
    return g_nextPendingDropGeneration;
}

[[nodiscard]] bool TryMakePendingDrop(
    RE::PlayerCharacter& a_player,
    RE::TESBoundObject* ap_object,
    RE::ExtraDataList* ap_extraList,
    const std::int32_t a_count,
    DropHookPolicy::PendingDrop& ar_pending) noexcept
{
    if (!ap_object || a_count <= 0)
        return false;

    const auto actorFormId = a_player.GetFormID();
    const auto objectFormId = ap_object->GetFormID();
    if (actorFormId == 0 || objectFormId == 0)
        return false;

    DropHookPolicy::PendingDrop pending{};
    pending.ActorAddress = reinterpret_cast<std::uintptr_t>(std::addressof(a_player));
    pending.ObjectAddress = reinterpret_cast<std::uintptr_t>(ap_object);
    pending.ActorFormId = actorFormId;
    pending.ObjectFormId = objectFormId;
    pending.Count = a_count;
    pending.Generation = NextPendingDropGeneration();
    if (ap_extraList) {
        if (const auto* unique = ap_extraList->GetByType<RE::ExtraUniqueID>()) {
            if (unique->baseID != objectFormId || unique->uniqueID == 0)
                return false;
            pending.StableUniqueId = unique->uniqueID;
            pending.HasStableUniqueId = true;
        }
    }
    ar_pending = pending;
    return true;
}

class ScopedPendingDrop final
{
public:
    explicit ScopedPendingDrop(const DropHookPolicy::PendingDrop& a_pending) noexcept
    {
        if (g_pendingDropDepth >= g_pendingDrops.size()) {
            if (g_pendingDropDepth != 0)
                g_pendingDrops[g_pendingDropDepth - 1].HadNestedCall = true;
            return;
        }

        if (g_pendingDropDepth != 0)
            g_pendingDrops[g_pendingDropDepth - 1].HadNestedCall = true;
        _index = g_pendingDropDepth;
        _generation = a_pending.Generation;
        g_pendingDrops[g_pendingDropDepth++] = {a_pending, _index != 0};
        _entered = true;
    }

    ~ScopedPendingDrop() noexcept
    {
        if (!_entered)
            return;
        if (g_pendingDropDepth == _index + 1 &&
            g_pendingDrops[_index].Pending.Generation == _generation) {
            g_pendingDrops[_index] = {};
            --g_pendingDropDepth;
            return;
        }

        // A scope-order failure cannot be attributed safely. Clear the
        // current thread's transient state rather than retaining stale drops.
        g_pendingDrops = {};
        g_pendingDropDepth = 0;
    }

    ScopedPendingDrop(const ScopedPendingDrop&) = delete;
    ScopedPendingDrop& operator=(const ScopedPendingDrop&) = delete;

private:
    std::size_t _index{};
    std::uint64_t _generation{};
    bool _entered{};
};

void RecordEventDiagnostic(
    std::atomic<std::uint64_t>& ar_counter,
    const char* a_reason) noexcept
{
    const auto count = ar_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (DropHookPolicy::ShouldLogAggregate(count)) {
        LogNoThrow([&] {
            SKSE::log::warn(
                "SkyrimTogetherVRGameplayBridge: local PlayerCharacter::DropObject attribution {} (aggregate={})",
                a_reason,
                count);
        });
    }
}

void RecordMissingTrampoline() noexcept
{
    if (!g_missingTrampolineLogged.exchange(true, std::memory_order_relaxed)) {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("PlayerCharacter::DropObject detour has no trampoline"); });
        LogNoThrow([] {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: enabled PlayerCharacter::DropObject detour has no trampoline; "
                "failing closed without invoking the engine body");
        });
    }
}

[[nodiscard]] VrHookDetachPolicy::OperationResult DisableHook(void*) noexcept
{
    const auto status = MH_DisableHook(g_hookTarget);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_DISABLED)
        return VrHookDetachPolicy::OperationResult::AlreadyDisabled;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    LogNoThrow([&] {
        SKSE::log::error(
            "SkyrimTogetherVRGameplayBridge: PlayerCharacter::DropObject hook disable failed ({})",
            static_cast<int>(status));
    });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult RemoveHook(void*) noexcept
{
    const auto status = MH_RemoveHook(g_hookTarget);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    LogNoThrow([&] {
        SKSE::log::error(
            "SkyrimTogetherVRGameplayBridge: PlayerCharacter::DropObject hook remove failed ({})",
            static_cast<int>(status));
    });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] bool DetachHook() noexcept
{
    return VrHookDetachPolicy::Detach(g_hookState, {DisableHook, RemoveHook, nullptr});
}

void ForgetDetachedHook() noexcept
{
    g_originalDropObject = nullptr;
    g_hookTarget = nullptr;
    g_hookState = {};
    g_installed.store(false, std::memory_order_release);
    g_missingTrampolineLogged.store(false, std::memory_order_relaxed);
    g_ambiguousEvents.store(0, std::memory_order_relaxed);
    g_mismatchedEvents.store(0, std::memory_order_relaxed);
}

void RetainFaultedHook(const char* a_operation) noexcept
{
    NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("PlayerCharacter::DropObject hook rollback could not prove detachment"); });
    LogNoThrow([&] {
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: PlayerCharacter::DropObject {} could not prove detachment; retaining "
            "the trampoline while the bridge is faulted",
            a_operation);
    });
}

RE::ObjectRefHandle* HookDropObject(
    RE::PlayerCharacter* ap_player,
    RE::ObjectRefHandle* ap_result,
    RE::TESBoundObject* ap_object,
    RE::ExtraDataList* ap_extraList,
    const std::int32_t a_count,
    RE::NiPoint3* ap_location,
    RE::NiPoint3* ap_rotation) noexcept
{
    return NoThrow::FailClosed<RE::ObjectRefHandle*>([&] {
        const auto original = g_originalDropObject;
        if (!original) {
            RecordMissingTrampoline();
            return ap_result;
        }

        auto* const localPlayer = RE::PlayerCharacter::GetSingleton();
        if (!g_installed.load(std::memory_order_acquire) || !ap_player || ap_player != localPlayer ||
            LocalGameplayCapture::IsRemoteInventorySuppressed())
            return original(ap_player, ap_result, ap_object, ap_extraList, a_count, ap_location, ap_rotation);

        DropHookPolicy::PendingDrop pending{};
        if (!TryMakePendingDrop(*ap_player, ap_object, ap_extraList, a_count, pending))
            return original(ap_player, ap_result, ap_object, ap_extraList, a_count, ap_location, ap_rotation);

        ScopedPendingDrop scope{pending};
        return original(ap_player, ap_result, ap_object, ap_extraList, a_count, ap_location, ap_rotation);
    }, ap_result);
}
} // namespace

DropHookPolicy::ContainerChangedDisposition ConsumeContainerChangedEvent(
    const DropHookPolicy::ContainerChangedEvent& a_event,
    const bool a_remoteSuppressed) noexcept
{
    auto* pending = g_pendingDropDepth != 0 ? std::addressof(g_pendingDrops[g_pendingDropDepth - 1]) : nullptr;
    const auto disposition = DropHookPolicy::ClassifyContainerChangedEvent(
        pending ? std::addressof(pending->Pending) : nullptr,
        a_event,
        a_remoteSuppressed,
        pending && (g_pendingDropDepth != 1 || pending->HadNestedCall));
    if (disposition == DropHookPolicy::ContainerChangedDisposition::MatchedDrop)
        pending->Pending.Consumed = true;
    else if (disposition == DropHookPolicy::ContainerChangedDisposition::NestedAmbiguity)
        RecordEventDiagnostic(g_ambiguousEvents, "rejected nested scope ambiguity");
    else if (disposition == DropHookPolicy::ContainerChangedDisposition::Mismatch)
        RecordEventDiagnostic(g_mismatchedEvents, "rejected mismatched container event");
    return disposition;
}

bool Install() noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        return true;

    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    try {
        if (!DropHookPolicy::HasPinnedTargetConfiguration() || !IsExpectedVrRuntime()) {
            LogNoThrow([] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: PlayerCharacter::DropObject hook requires exact Skyrim VR 1.4.15.0");
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
            LogNoThrow([&] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: MinHook initialization failed for PlayerCharacter::DropObject ({})",
                    static_cast<int>(initialize));
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        REL::Relocation<DropObject> target{REL::Offset(DropHookPolicy::kPlayerDropObjectVrRva)};
        const auto moduleBase = REL::Module::get().base();
        if (moduleBase == 0 ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - DropHookPolicy::kPlayerCharacterDropObjectVtableEntryRva ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - DropHookPolicy::kPlayerDropObjectVrRva ||
            !IsReadableVtableSlot(
                moduleBase + DropHookPolicy::kPlayerCharacterDropObjectVtableEntryRva,
                moduleBase + DropHookPolicy::kPlayerDropObjectVrRva)) {
            LogNoThrow([] {
                SKSE::log::error("SkyrimTogetherVRGameplayBridge: PlayerCharacter::DropObject vtable contract validation failed");
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }
        if (moduleBase == 0 ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - DropHookPolicy::kPlayerDropObjectVrRva ||
            !DropHookPolicy::IsExactPlayerDropObjectTarget(target.offset()) ||
            target.address() != moduleBase + DropHookPolicy::kPlayerDropObjectVrRva ||
            !IsVerifiedExecutableTarget(target.address(), DropHookPolicy::kPlayerDropObjectVrPrologue)) {
            LogNoThrow([&] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: PlayerCharacter::DropObject target validation failed at RVA 0x{:X}",
                    target.offset());
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        g_hookTarget = reinterpret_cast<void*>(target.address());
        void* trampoline{};
        const auto create = MH_CreateHook(g_hookTarget, reinterpret_cast<void*>(&HookDropObject), &trampoline);
        if (create != MH_OK || !trampoline || trampoline == g_hookTarget) {
            LogNoThrow([&] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: PlayerCharacter::DropObject hook creation or trampoline validation failed ({})",
                    static_cast<int>(create));
            });
            if (create == MH_OK) {
                g_hookState.Created = true;
                if (!DetachHook()) {
                    RetainFaultedHook("creation rollback");
                    return false;
                }
            }
            ForgetDetachedHook();
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        g_originalDropObject = reinterpret_cast<DropObject>(trampoline);
        g_hookState.Created = true;
        // Keep the trampoline reachable until rollback proves removal even if
        // MinHook reports an enable failure after changing the target.
        g_hookState.Enabled = true;
        const auto enable = MH_EnableHook(g_hookTarget);
        if (enable != MH_OK) {
            LogNoThrow([&] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: PlayerCharacter::DropObject hook enable failed ({})",
                    static_cast<int>(enable));
            });
            if (DetachHook()) {
                ForgetDetachedHook();
                g_installAttempted.store(false, std::memory_order_release);
                return false;
            }
            RetainFaultedHook("install rollback");
            return false;
        }

        g_installed.store(true, std::memory_order_release);
        LogNoThrow([] {
            SKSE::log::info(
                "SkyrimTogetherVRGameplayBridge: installed exact PlayerCharacter::DropObject capture at RVA 0x{:X}",
                DropHookPolicy::kPlayerDropObjectVrRva);
        });
        return true;
    } catch (...) {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("PlayerCharacter::DropObject hook target resolution threw"); });
        LogNoThrow([] {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: PlayerCharacter::DropObject target resolution threw; refusing hook");
        });
        if (g_hookState.Created && !DetachHook()) {
            RetainFaultedHook("exception rollback");
            return false;
        }
        ForgetDetachedHook();
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }
}

bool Uninstall() noexcept
{
    g_installed.store(false, std::memory_order_release);
    if (!g_hookState.Created) {
        ForgetDetachedHook();
        g_installAttempted.store(false, std::memory_order_release);
        return true;
    }
    if (!DetachHook()) {
        RetainFaultedHook("uninstall");
        return false;
    }

    LogNoThrow([] {
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: removed PlayerCharacter::DropObject capture (ambiguous={}, mismatched={})",
            g_ambiguousEvents.load(std::memory_order_relaxed),
            g_mismatchedEvents.load(std::memory_order_relaxed));
    });
    ForgetDetachedHook();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::DropHooks
