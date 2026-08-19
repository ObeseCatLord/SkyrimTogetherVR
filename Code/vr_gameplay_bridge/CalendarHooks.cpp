#include "CalendarHooks.h"

#include "BridgeEndpoint.h"
#include "VrHookDetachPolicy.h"
#include "VrNoThrow.h"

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

namespace SkyrimTogetherVR::GameplayAdapter::CalendarHooks
{
namespace
{
constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};

// The verified target is a non-virtual x64 body with RCX=Calendar* and
// XMM1=float delta. A free-function type has the same ABI as the member body.
using CalendarUpdate = void (*)(RE::Calendar*, float) noexcept;

CalendarUpdate g_originalUpdate{};
void* g_hookTarget{};
std::atomic_bool g_installAttempted{};
std::atomic_bool g_missingTrampolineLogged{};
VrHookDetachPolicy::HookState g_hookState{};

struct AuthoritativeTickState
{
    std::uint64_t ServerInstanceNonce{};
    std::uint64_t ConnectionGeneration{};
    std::chrono::steady_clock::time_point LastAnchor{};
    float TimeScale{};
    std::uint64_t Epoch{};
    bool Active{};
};

struct AuthoritativeDispatchState
{
    std::uint64_t ReservationEpoch{};
    bool InFlight{};
};

std::mutex g_authoritativeTickLock{};
AuthoritativeTickState g_authoritativeTick{};
AuthoritativeDispatchState g_authoritativeDispatch{};
std::atomic_uint64_t g_reanchorCount{};

[[nodiscard]] bool IsExpectedVrRuntime() noexcept
{
    return REL::Module::IsVR() && REL::Module::get().version() == kExpectedSkyrimVrRuntime;
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

[[nodiscard]] VrHookDetachPolicy::OperationResult DisableHook(void*) noexcept
{
    const auto status = MH_DisableHook(g_hookTarget);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_DISABLED)
        return VrHookDetachPolicy::OperationResult::AlreadyDisabled;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: Calendar::Update hook disable failed ({})", static_cast<int>(status)); });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult RemoveHook(void*) noexcept
{
    const auto status = MH_RemoveHook(g_hookTarget);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: Calendar::Update hook removal failed ({})", static_cast<int>(status)); });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] bool DetachHook() noexcept
{
    return VrHookDetachPolicy::Detach(g_hookState, {DisableHook, RemoveHook, nullptr});
}

void ForgetDetachedHook() noexcept
{
    g_hookTarget = nullptr;
    g_originalUpdate = nullptr;
    g_hookState = {};
    ResetAuthoritativeTick();
    g_missingTrampolineLogged.store(false, std::memory_order_relaxed);
}

void RetainFaultedHook(const char* a_operation) noexcept
{
    NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("Calendar::Update hook rollback could not prove detachment"); });
    NoThrow::BestEffort([&] { SKSE::log::critical(
        "SkyrimTogetherVRGameplayBridge: Calendar::Update {} could not prove detachment; retaining the "
        "trampoline so a possible live detour remains callable while the bridge is faulted",
        a_operation); });
}

[[nodiscard]] constexpr bool ShouldLogPowerOfTwo(const std::uint64_t a_count) noexcept
{
    return a_count != 0 && (a_count & (a_count - 1)) == 0;
}

void LogBoundedCatchup(const char* a_reason) noexcept
{
    const auto count = g_reanchorCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (ShouldLogPowerOfTwo(count))
        NoThrow::BestEffort([&] { SKSE::log::warn(
            "SkyrimTogetherVRGameplayBridge: bounded authoritative Calendar::Update catch-up ({}, count={})",
            a_reason, count); });
}

// This invalidates only the snapshot.  An already entered trampoline keeps
// its separate in-flight reservation until it returns, so a replacement
// snapshot cannot start a second Calendar mutation concurrently.
void ResetAuthoritativeTickLocked() noexcept
{
    if (!g_authoritativeTick.Active)
        return;

    const auto nextEpoch = NextAuthoritativeTickEpoch(g_authoritativeTick.Epoch);
    g_authoritativeTick = {};
    if (nextEpoch == 0)
    {
        // Epoch exhaustion is fail-closed for this process.  Keeping UINT64_MAX
        // prevents a later activation from ever reusing a stale token.
        g_authoritativeTick.Epoch = std::numeric_limits<std::uint64_t>::max();
        return;
    }
    g_authoritativeTick.Epoch = nextEpoch;
}

[[nodiscard]] bool IsReservationCurrent(const std::uint64_t a_reservationEpoch) noexcept
{
    const std::scoped_lock lock{g_authoritativeTickLock};
    return IsAuthoritativeReservationCurrent(
        g_authoritativeDispatch.InFlight, a_reservationEpoch, g_authoritativeTick.Active,
        g_authoritativeTick.Epoch);
}

void CompleteCalendarDispatch(const std::uint64_t a_reservationEpoch) noexcept
{
    const std::scoped_lock lock{g_authoritativeTickLock};
    // Do not write the snapshot here: callbacks made by the native body may
    // have reset it or installed a replacement.  The in-flight guard belongs
    // to this native call alone and may be released even when its epoch is no
    // longer current.  Epoch zero denotes a vanilla pass-through dispatch.
    if (g_authoritativeDispatch.InFlight &&
        g_authoritativeDispatch.ReservationEpoch == a_reservationEpoch)
    {
        g_authoritativeDispatch = {};
    }
}

class ScopedCalendarDispatch final
{
public:
    explicit ScopedCalendarDispatch(const std::uint64_t a_reservationEpoch) noexcept : m_reservationEpoch(a_reservationEpoch) {}

    ~ScopedCalendarDispatch() noexcept
    {
        CompleteCalendarDispatch(m_reservationEpoch);
    }

private:
    std::uint64_t m_reservationEpoch;
};

void HookUpdate(RE::Calendar* a_calendar, const float a_delta) noexcept
{
    try {
    const auto original = g_originalUpdate;
    if (!original)
    {
        if (!g_missingTrampolineLogged.exchange(true, std::memory_order_relaxed))
        {
            NoThrow::BestEffort([] { SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: enabled Calendar::Update detour has no trampoline; "
                "the bridge remains faulted until the hook can be removed");
            });
        }
        return;
    }

    // Reserve under this lock, then invoke the native body after it is
    // released.  Calendar::Update dispatches event sinks, which can reset or
    // replace the snapshot and must therefore never run under this mutex.
    std::unique_lock lock{g_authoritativeTickLock};
    if (g_authoritativeDispatch.InFlight)
    {
        // A nested or concurrent hook cannot safely mutate Calendar while the
        // reservation owner is inside the native body.  Its authoritative
        // elapsed time remains behind the advanced anchor and is recoverable
        // on a later hook call.
        return;
    }

    auto& endpoint = BridgeEndpoint::Get();
    const auto* mapping = endpoint.Mapping();
    SessionIdentitySnapshot session{};
    if (!endpoint.IsOperational() || !mapping ||
        !TrySnapshotSessionIdentity(mapping->Header, session) || !endpoint.IsOperational())
    {
        ResetAuthoritativeTickLocked();
        g_authoritativeDispatch = {0, true};
        const ScopedCalendarDispatch dispatch{0};
        lock.unlock();
        original(a_calendar, a_delta);
        return;
    }

    if (!ShouldSuppressCalendarUpdate(
            true, g_authoritativeTick.Active, g_authoritativeTick.ServerInstanceNonce,
            g_authoritativeTick.ConnectionGeneration, session.ServerInstanceNonce,
            session.ConnectionGeneration))
    {
        if (g_authoritativeTick.Active)
            ResetAuthoritativeTickLocked();
        // Online transport alone is not sufficient authority. Before the
        // first validated snapshot, vanilla must keep the local clock moving.
        g_authoritativeDispatch = {0, true};
        const ScopedCalendarDispatch dispatch{0};
        lock.unlock();
        original(a_calendar, a_delta);
        return;
    }

    if (!a_calendar || !a_calendar->gameHour || !a_calendar->timeScale)
    {
        ResetAuthoritativeTickLocked();
        g_authoritativeDispatch = {0, true};
        const ScopedCalendarDispatch dispatch{0};
        lock.unlock();
        original(a_calendar, a_delta);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsedRealSeconds =
        std::chrono::duration<float>(now - g_authoritativeTick.LastAnchor).count();
    const auto authoritativeTimeScale = g_authoritativeTick.TimeScale;
    const auto plan = PlanAuthoritativeAdvance(true, elapsedRealSeconds, authoritativeTimeScale);
    if (plan.Disposition == AuthoritativeAdvanceDisposition::Reanchor)
    {
        // A non-monotonic/invalid process clock must never be converted into a
        // fabricated in-game jump.
        g_authoritativeTick.LastAnchor = now;
        lock.unlock();
        LogBoundedCatchup("invalid monotonic interval or timescale");
        return;
    }
    if (plan.ChunkCount == 0)
    {
        g_authoritativeTick.LastAnchor = now;
        return;
    }

    if (!CanReserveAuthoritativeAdvance(
            g_authoritativeTick.Active, g_authoritativeDispatch.InFlight, g_authoritativeTick.Epoch))
    {
        return;
    }

    std::array<float, kMaximumAuthoritativeAdvanceChunks> reservedDeltas{};
    float remainingRealSeconds = elapsedRealSeconds;
    bool boundedCatchup{};
    for (std::uint32_t chunk = 0; chunk < plan.ChunkCount && remainingRealSeconds > 0.0F; ++chunk)
    {
        const auto delta = std::min(remainingRealSeconds, plan.MaximumChunkSeconds);
        reservedDeltas[chunk] = delta;
        // Advance the anchor before releasing the lock.  Reentrant callers
        // therefore cannot consume this exact interval a second time.
        g_authoritativeTick.LastAnchor += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float>(delta));
        remainingRealSeconds -= delta;
    }
    boundedCatchup = remainingRealSeconds > 0.0F;

    const auto reservationEpoch = g_authoritativeTick.Epoch;
    g_authoritativeDispatch = {reservationEpoch, true};
    const ScopedCalendarDispatch dispatch{reservationEpoch};
    lock.unlock();

    for (const auto delta : reservedDeltas)
    {
        if (delta == 0.0F || !IsReservationCurrent(reservationEpoch))
            break;

        // Time scale is part of the accepted snapshot. Reassert it before
        // each bounded native call so local scripts cannot alter the time
        // model.  No calendar mutex is held across this write or trampoline.
        a_calendar->timeScale->value = authoritativeTimeScale;
        original(a_calendar, delta);
    }

    if (boundedCatchup)
        LogBoundedCatchup("advance chunk budget reached");
    } catch (...) {
        // The original body is never retried after an exception.
    }
}
} // namespace

bool ActivateAuthoritativeTick(
    const std::uint64_t a_serverInstanceNonce,
    const std::uint64_t a_connectionGeneration,
    const float a_timeScale) noexcept
{
    try {
    if (a_serverInstanceNonce == 0 || a_connectionGeneration == 0 ||
        !std::isfinite(a_timeScale) || a_timeScale < 0.0F || a_timeScale > kMaximumAuthoritativeTimeScale)
        return false;

    auto& endpoint = BridgeEndpoint::Get();
    const auto* mapping = endpoint.Mapping();
    SessionIdentitySnapshot session{};
    if (!endpoint.IsOperational() || !mapping || !TrySnapshotSessionIdentity(mapping->Header, session) ||
        session.ServerInstanceNonce != a_serverInstanceNonce ||
        session.ConnectionGeneration != a_connectionGeneration)
        return false;

    const std::scoped_lock lock{g_authoritativeTickLock};
    const auto epoch = NextAuthoritativeTickEpoch(g_authoritativeTick.Epoch);
    if (epoch == 0)
    {
        // Never wrap an epoch: a completion from an implausibly old native
        // dispatch must not become current again.
        ResetAuthoritativeTickLocked();
        return false;
    }
    g_authoritativeTick = {
        a_serverInstanceNonce, a_connectionGeneration, std::chrono::steady_clock::now(), a_timeScale, epoch, true};
    g_reanchorCount.store(0, std::memory_order_relaxed);
    return true;
    } catch (...) {
        return false;
    }
}

void ResetAuthoritativeTick() noexcept
{
    const std::scoped_lock lock{g_authoritativeTickLock};
    ResetAuthoritativeTickLocked();
    g_reanchorCount.store(0, std::memory_order_relaxed);
}

bool Install() noexcept
{
    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return g_hookState.Created && g_originalUpdate != nullptr;

    try
    {
        if (!HasPinnedTargetConfiguration() || !IsExpectedVrRuntime())
        {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: Calendar::Update hook requires exact Skyrim VR 1.4.15.0");
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED)
        {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: MinHook initialization failed for Calendar::Update ({})", static_cast<int>(initialize));
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        REL::Relocation<CalendarUpdate> target{REL::ID(kCalendarUpdateVrAddressId)};
        if (target.offset() != kCalendarUpdateVrRva || !IsVerifiedExecutableTarget(target.address(), kCalendarUpdateVrPrologue))
        {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: Calendar::Update target validation failed for VR address ID {} "
                "at RVA 0x{:X}",
                kCalendarUpdateVrAddressId, target.offset());
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        g_hookTarget = reinterpret_cast<void*>(target.address());
        void* trampoline{};
        const auto create = MH_CreateHook(g_hookTarget, reinterpret_cast<void*>(&HookUpdate), &trampoline);
        if (create != MH_OK || !trampoline || trampoline == g_hookTarget)
        {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: Calendar::Update hook creation or trampoline validation failed ({})", static_cast<int>(create));
            if (create == MH_OK)
            {
                g_hookState.Created = true;
                if (!DetachHook())
                {
                    RetainFaultedHook("creation rollback");
                    return false;
                }
            }
            ForgetDetachedHook();
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }
        g_originalUpdate = reinterpret_cast<CalendarUpdate>(trampoline);
        g_hookState.Created = true;

        // A failed enable may still have altered the target. Retain the
        // trampoline until detach proves the original bytes are restored.
        g_hookState.Enabled = true;
        const auto enable = MH_EnableHook(g_hookTarget);
        if (enable != MH_OK)
        {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: Calendar::Update hook enable failed ({})", static_cast<int>(enable));
            if (DetachHook())
            {
                ForgetDetachedHook();
                g_installAttempted.store(false, std::memory_order_release);
                return false;
            }
            RetainFaultedHook("install rollback");
            return false;
        }

        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: installed exact Calendar::Update time-lock hook at VR address ID {} "
            "(RVA 0x{:X})",
            kCalendarUpdateVrAddressId, kCalendarUpdateVrRva);
        return true;
    }
    catch (...)
    {
        NoThrow::BestEffort([] {
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: Calendar::Update target resolution threw; refusing time-lock hook");
        });
        if (g_hookState.Created && !DetachHook())
        {
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
    try {
    if (!g_hookState.Created)
    {
        ForgetDetachedHook();
        g_installAttempted.store(false, std::memory_order_release);
        return true;
    }
    if (!DetachHook())
    {
        RetainFaultedHook("uninstall");
        return false;
    }
    ForgetDetachedHook();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
    } catch (...) {
        return false;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::CalendarHooks
