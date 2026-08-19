#include "WaypointHooks.h"

#include "BridgeEndpoint.h"
#include "LocalGameplayCapture.h"
#include "VrHookDetachPolicy.h"
#include "VrNoThrow.h"

#include <MinHook.h>

#include <atomic>
#include <cstring>
#include <limits>

namespace SkyrimTogetherVR::GameplayAdapter::WaypointHooks
{
namespace
{
constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};

struct HookRecord
{
    const char* Name{};
    void* Target{};
    VrHookDetachPolicy::HookState State{};
};

SetWaypoint g_setWaypointTarget{};
RemoveWaypoint g_removeWaypointTarget{};
SetWaypoint g_originalSetWaypoint{};
RemoveWaypoint g_originalRemoveWaypoint{};
HookRecord g_setWaypointHook{"PlayerCharacter::SetWaypoint"};
HookRecord g_removeWaypointHook{"PlayerCharacter::RemoveWaypoint"};
std::atomic_bool g_installAttempted{};
std::atomic_bool g_installed{};
std::atomic<std::uint64_t> g_remoteReplaySuppressions{};
std::atomic<std::uint64_t> g_observationRejections{};
std::atomic<std::uint64_t> g_publicationRejections{};
std::atomic_bool g_missingSetWaypointTrampolineLogged{};
std::atomic_bool g_missingRemoveWaypointTrampolineLogged{};
thread_local std::uint32_t g_remoteReplayDepth{};

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

void RecordAggregate(
    std::atomic<std::uint64_t>& ar_counter,
    const char* a_reason) noexcept
{
    const auto count = ar_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (ShouldLogAggregate(count))
        NoThrow::BestEffort([&] { SKSE::log::warn("SkyrimTogetherVRGameplayBridge: waypoint {} (count={})", a_reason, count); });
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
    NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook disable failed ({})", hook.Name, static_cast<int>(status)); });
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
    NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook removal failed ({})", hook.Name, static_cast<int>(status)); });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] bool DetachHook(HookRecord& ar_hook) noexcept
{
    return VrHookDetachPolicy::Detach(ar_hook.State, {DisableHook, RemoveHook, &ar_hook});
}

void ClearDetachedState() noexcept
{
    g_setWaypointTarget = nullptr;
    g_removeWaypointTarget = nullptr;
    g_originalSetWaypoint = nullptr;
    g_originalRemoveWaypoint = nullptr;
    g_setWaypointHook = {"PlayerCharacter::SetWaypoint"};
    g_removeWaypointHook = {"PlayerCharacter::RemoveWaypoint"};
    g_installed.store(false, std::memory_order_release);
    g_missingSetWaypointTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingRemoveWaypointTrampolineLogged.store(false, std::memory_order_relaxed);
}

[[nodiscard]] bool RollbackInstall() noexcept
{
    g_installed.store(false, std::memory_order_release);
    const bool removeDetached = DetachHook(g_removeWaypointHook);
    const bool setDetached = DetachHook(g_setWaypointHook);
    if (removeDetached && setDetached) {
        ClearDetachedState();
        g_installAttempted.store(false, std::memory_order_release);
        return true;
    }

    NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("waypoint hook rollback could not prove detachment"); });
    NoThrow::BestEffort([] { SKSE::log::critical(
        "SkyrimTogetherVRGameplayBridge: waypoint hook rollback could not prove detachment; retaining callable trampolines");
    });
    return false;
}

[[nodiscard]] bool CreateAndEnableHook(
    HookRecord& ar_hook,
    void* a_detour,
    void** a_original) noexcept
{
    const auto create = MH_CreateHook(ar_hook.Target, a_detour, a_original);
    if (create != MH_OK) {
        NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook creation failed ({})", ar_hook.Name, static_cast<int>(create)); });
        return false;
    }
    ar_hook.State.Created = true;
    if (!*a_original || *a_original == ar_hook.Target) {
        NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook returned an invalid trampoline", ar_hook.Name); });
        return false;
    }

    // Retain state until rollback proves that a failed enable restored the
    // target, because MinHook cannot guarantee it otherwise.
    ar_hook.State.Enabled = true;
    const auto enable = MH_EnableHook(ar_hook.Target);
    if (enable != MH_OK) {
        NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook enable failed ({})", ar_hook.Name, static_cast<int>(enable)); });
        return false;
    }
    return true;
}

void HookSetWaypoint(
    RE::PlayerCharacter* a_player,
    RE::NiPoint3* a_position,
    RE::TESWorldSpace* a_worldspace) noexcept
{
    try {
    const auto original = g_originalSetWaypoint;
    if (!original) {
        if (!g_missingSetWaypointTrampolineLogged.exchange(true, std::memory_order_relaxed))
            NoThrow::BestEffort([] { SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled SetWaypoint detour has no trampoline"); });
        return;
    }

    original(a_player, a_position, a_worldspace);
    if (!g_installed.load(std::memory_order_acquire))
        return;
    if (IsRemoteWaypointReplay()) {
        RecordAggregate(g_remoteReplaySuppressions, "remote replay suppression");
        return;
    }
    if (!a_player || !a_position || !a_worldspace) {
        RecordAggregate(g_observationRejections, "set observation rejected null input");
        return;
    }

    const auto* runtime = a_player->GetVRInfoRuntimeData();
    const auto marker = runtime ? runtime->playerMapMarker.get() : nullptr;
    if (!marker || marker->GetWorldspace() != a_worldspace || marker->GetPosition() != *a_position) {
        RecordAggregate(g_observationRejections, "set postcondition rejected");
        return;
    }

    const auto capture = LocalGameplayCapture::CaptureExactWaypointSet(*a_player, *a_worldspace, *a_position);
    if (capture == LocalGameplayCapture::ExactWaypointCaptureResult::Rejected)
        RecordAggregate(g_publicationRejections, "set publication rejected");
    } catch (...) {
        // Do not retry the engine body after a post-call capture failure.
    }
}

void HookRemoveWaypoint(RE::PlayerCharacter* a_player) noexcept
{
    try {
    const auto original = g_originalRemoveWaypoint;
    if (!original) {
        if (!g_missingRemoveWaypointTrampolineLogged.exchange(true, std::memory_order_relaxed))
            NoThrow::BestEffort([] { SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled RemoveWaypoint detour has no trampoline"); });
        return;
    }

    original(a_player);
    if (!g_installed.load(std::memory_order_acquire))
        return;
    if (IsRemoteWaypointReplay()) {
        RecordAggregate(g_remoteReplaySuppressions, "remote replay suppression");
        return;
    }
    if (!a_player) {
        RecordAggregate(g_observationRejections, "remove observation rejected null input");
        return;
    }

    const auto* runtime = a_player->GetVRInfoRuntimeData();
    if (!runtime || runtime->playerMapMarker.get()) {
        RecordAggregate(g_observationRejections, "remove postcondition rejected");
        return;
    }

    const auto capture = LocalGameplayCapture::CaptureExactWaypointRemove(*a_player);
    if (capture == LocalGameplayCapture::ExactWaypointCaptureResult::Rejected)
        RecordAggregate(g_publicationRejections, "remove publication rejected");
    } catch (...) {
        // Do not retry the engine body after a post-call capture failure.
    }
}
} // namespace

ScopedRemoteWaypointReplay::ScopedRemoteWaypointReplay() noexcept
{
    ++g_remoteReplayDepth;
}

ScopedRemoteWaypointReplay::~ScopedRemoteWaypointReplay() noexcept
{
    if (g_remoteReplayDepth != 0)
        --g_remoteReplayDepth;
}

bool IsRemoteWaypointReplay() noexcept
{
    return g_remoteReplayDepth != 0;
}

bool InvokeSetWaypoint(
    RE::PlayerCharacter* a_player,
    RE::NiPoint3* a_position,
    RE::TESWorldSpace* a_worldspace) noexcept
{
    const auto target = g_setWaypointTarget;
    if (!g_installed.load(std::memory_order_acquire) || !target || !a_player || !a_position || !a_worldspace)
        return false;
    try {
        target(a_player, a_position, a_worldspace);
        return true;
    } catch (...) {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("SetWaypoint native invocation threw"); });
        return false;
    }
}

bool InvokeRemoveWaypoint(RE::PlayerCharacter* a_player) noexcept
{
    const auto target = g_removeWaypointTarget;
    if (!g_installed.load(std::memory_order_acquire) || !target || !a_player)
        return false;
    try {
        target(a_player);
        return true;
    } catch (...) {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("RemoveWaypoint native invocation threw"); });
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

    try {
        if (!HasPinnedTargetConfiguration() || !IsExpectedVrRuntime()) {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: waypoint hooks require exact Skyrim VR 1.4.15.0");
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: MinHook initialization failed for waypoint hooks ({})", static_cast<int>(initialize));
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        REL::Relocation<SetWaypoint> setWaypoint{REL::Offset(kSetWaypointVrRva)};
        REL::Relocation<RemoveWaypoint> removeWaypoint{REL::Offset(kRemoveWaypointVrRva)};
        const auto moduleBase = REL::Module::get().base();
        if (setWaypoint.offset() != kSetWaypointVrRva ||
            setWaypoint.address() != moduleBase + kSetWaypointVrRva ||
            removeWaypoint.offset() != kRemoveWaypointVrRva ||
            removeWaypoint.address() != moduleBase + kRemoveWaypointVrRva ||
            !IsVerifiedExecutableTarget(setWaypoint.address(), kSetWaypointVrPrologue) ||
            !IsVerifiedExecutableTarget(removeWaypoint.address(), kRemoveWaypointVrPrologue)) {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: waypoint targets failed verification (set=0x{:X}, remove=0x{:X})",
                setWaypoint.offset(), removeWaypoint.offset());
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        g_setWaypointTarget = setWaypoint.get();
        g_removeWaypointTarget = removeWaypoint.get();
        g_setWaypointHook.Target = reinterpret_cast<void*>(setWaypoint.address());
        g_removeWaypointHook.Target = reinterpret_cast<void*>(removeWaypoint.address());
        if (!CreateAndEnableHook(
                g_setWaypointHook,
                reinterpret_cast<void*>(&HookSetWaypoint),
                reinterpret_cast<void**>(&g_originalSetWaypoint)) ||
            !CreateAndEnableHook(
                g_removeWaypointHook,
                reinterpret_cast<void*>(&HookRemoveWaypoint),
                reinterpret_cast<void**>(&g_originalRemoveWaypoint))) {
            static_cast<void>(RollbackInstall());
            return false;
        }

        g_installed.store(true, std::memory_order_release);
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: installed verified local waypoint hooks (SetWaypoint RVA 0x{:X}, RemoveWaypoint RVA 0x{:X})",
            kSetWaypointVrRva, kRemoveWaypointVrRva);
        return true;
    } catch (...) {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("waypoint hook target resolution threw"); });
        NoThrow::BestEffort([] { SKSE::log::critical("SkyrimTogetherVRGameplayBridge: waypoint hook installation rejected an exception"); });
        static_cast<void>(RollbackInstall());
        return false;
    }
}

bool Uninstall() noexcept
{
    g_installed.store(false, std::memory_order_release);
    const bool removeDetached = DetachHook(g_removeWaypointHook);
    const bool setDetached = DetachHook(g_setWaypointHook);
    if (!removeDetached || !setDetached) {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("waypoint hook uninstall could not prove detachment"); });
        NoThrow::BestEffort([] { SKSE::log::critical("SkyrimTogetherVRGameplayBridge: waypoint hook uninstall incomplete; preserving callable trampolines"); });
        return false;
    }

    NoThrow::BestEffort([&] { SKSE::log::info(
        "SkyrimTogetherVRGameplayBridge: waypoint hook summary remoteReplaySuppressions={} observationRejections={} publicationRejections={}",
        g_remoteReplaySuppressions.load(std::memory_order_relaxed),
        g_observationRejections.load(std::memory_order_relaxed),
        g_publicationRejections.load(std::memory_order_relaxed));
    });
    ClearDetachedState();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::WaypointHooks
