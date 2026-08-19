#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace SkyrimTogetherVR::GameplayAdapter::CalendarHooks
{
// Direct VR Address Library ID for the pinned Calendar time-advance body.
// The VR database's GameDaysPassed argument label is incorrect: decrypted
// code and live callers prove Calendar::Update(Calendar*, float elapsedRealSeconds).
// Desktop ID 36291 must not be
// translated here: its VR correlation at 0x5DB010 is an unrelated form thunk.
inline constexpr std::uint64_t kCalendarUpdateVrAddressId = 35402;
inline constexpr std::uint64_t kCalendarUpdateVrRva = 0x05AD8F0;
inline constexpr float kMaximumAuthoritativeTimeScale = 1000.0F;
inline constexpr float kMaximumGameHoursPerAdvanceChunk = 12.0F;
inline constexpr std::uint32_t kMaximumAuthoritativeAdvanceChunks = 8;
inline constexpr std::array<std::uint8_t, 24> kCalendarUpdateVrPrologue{
    0x40, 0x53, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x41, 0x30, 0x48, 0x8B, 0xD9, 0x0F, 0x29, 0x74, 0x24, 0x70, 0x0F, 0x28, 0xF1,
};

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return kCalendarUpdateVrAddressId == 35402 && kCalendarUpdateVrRva == 0x05AD8F0 && kCalendarUpdateVrPrologue.size() == 24;
}

// Classify a non-empty address span without forming either endpoint. This is
// deliberately expressed as subtraction so a malformed address or size near
// UINTPTR_MAX cannot wrap into an apparently valid range.
[[nodiscard]] constexpr bool IsSpanWithinSegment(
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

// A validated snapshot owns the online clock only for the session that
// produced it. Every inactive, faulted, disconnected, or transitional bridge
// state falls through to the native Calendar::Update body immediately.
[[nodiscard]] constexpr bool
ShouldSuppressCalendarUpdate(
    const bool a_endpointOperational,
    const bool a_snapshotActive,
    const std::uint64_t a_snapshotServerInstanceNonce,
    const std::uint64_t a_snapshotConnectionGeneration,
    const std::uint64_t a_currentServerInstanceNonce,
    const std::uint64_t a_currentConnectionGeneration) noexcept
{
    return a_endpointOperational && a_snapshotActive &&
           a_snapshotServerInstanceNonce != 0 && a_snapshotConnectionGeneration != 0 &&
           a_snapshotServerInstanceNonce == a_currentServerInstanceNonce &&
           a_snapshotConnectionGeneration == a_currentConnectionGeneration;
}

enum class AuthoritativeAdvanceDisposition : std::uint8_t
{
    PassThrough,
    Advance,
    Reanchor,
};

struct AuthoritativeAdvancePlan
{
    AuthoritativeAdvanceDisposition Disposition{AuthoritativeAdvanceDisposition::PassThrough};
    float MaximumChunkSeconds{};
    std::uint32_t ChunkCount{};
};

// Epoch zero is never issued to a snapshot or reservation.  Refuse to wrap
// rather than allowing a very old native completion to look current again.
[[nodiscard]] constexpr std::uint64_t NextAuthoritativeTickEpoch(const std::uint64_t a_epoch) noexcept
{
    return a_epoch == std::numeric_limits<std::uint64_t>::max() ? 0 : a_epoch + 1;
}

// Only one native Calendar::Update dispatch may own a reservation at a time.
// Calls that arrive while it is in flight leave their elapsed interval behind
// the already advanced anchor for a later hook invocation.
[[nodiscard]] constexpr bool CanReserveAuthoritativeAdvance(
    const bool a_snapshotActive,
    const bool a_dispatchInFlight,
    const std::uint64_t a_epoch) noexcept
{
    return a_snapshotActive && !a_dispatchInFlight && a_epoch != 0;
}

// A reset or replacement advances the snapshot epoch.  A completion may only
// remain associated with the snapshot that reserved it when this is true.
[[nodiscard]] constexpr bool IsAuthoritativeReservationCurrent(
    const bool a_dispatchInFlight,
    const std::uint64_t a_reservationEpoch,
    const bool a_snapshotActive,
    const std::uint64_t a_snapshotEpoch) noexcept
{
    return a_dispatchInFlight && a_reservationEpoch != 0 && a_snapshotActive &&
           a_reservationEpoch == a_snapshotEpoch;
}

// Calendar::Update accepts real seconds. Keep every trampoline call below
// twelve game-hours so the engine can process at most one midnight per call.
[[nodiscard]] inline AuthoritativeAdvancePlan PlanAuthoritativeAdvance(
    const bool a_snapshotActive,
    const float a_elapsedRealSeconds,
    const float a_timeScale) noexcept
{
    if (!a_snapshotActive)
        return {};
    if (!std::isfinite(a_elapsedRealSeconds) || a_elapsedRealSeconds < 0.0F ||
        !std::isfinite(a_timeScale) || a_timeScale < 0.0F || a_timeScale > kMaximumAuthoritativeTimeScale)
        return {AuthoritativeAdvanceDisposition::Reanchor};
    if (a_elapsedRealSeconds == 0.0F || a_timeScale == 0.0F)
        return {AuthoritativeAdvanceDisposition::Advance};

    const auto maximumChunkSeconds = (kMaximumGameHoursPerAdvanceChunk * 3600.0F) / a_timeScale;
    if (!std::isfinite(maximumChunkSeconds) || maximumChunkSeconds <= 0.0F)
        return {AuthoritativeAdvanceDisposition::Reanchor};

    const auto requestedChunks = std::ceil(a_elapsedRealSeconds / maximumChunkSeconds);
    if (!std::isfinite(requestedChunks) || requestedChunks <= 0.0F)
        return {AuthoritativeAdvanceDisposition::Reanchor};
    const auto chunks = requestedChunks >= static_cast<float>(kMaximumAuthoritativeAdvanceChunks) ?
                            kMaximumAuthoritativeAdvanceChunks :
                            static_cast<std::uint32_t>(requestedChunks);
    return {AuthoritativeAdvanceDisposition::Advance, maximumChunkSeconds, chunks};
}

// A server snapshot changes clock display and scale, not the local save's
// elapsed-day history. The engine itself derives GameDaysPassed from this
// invariant after every native update.
[[nodiscard]] inline bool IsCalendarSnapshotInvariant(
    const float a_gameDaysPassed,
    const float a_rawDaysPassed,
    const float a_gameHour,
    const float a_timeScale) noexcept
{
    return std::isfinite(a_gameDaysPassed) && std::isfinite(a_rawDaysPassed) && std::isfinite(a_gameHour) &&
           std::isfinite(a_timeScale) && a_gameDaysPassed >= 0.0F && a_rawDaysPassed >= 0.0F &&
           std::floor(a_rawDaysPassed) == a_rawDaysPassed && a_gameHour >= 0.0F && a_gameHour < 24.0F &&
           a_timeScale >= 0.0F && a_timeScale <= kMaximumAuthoritativeTimeScale &&
           std::fabs(a_gameDaysPassed - (a_rawDaysPassed + a_gameHour / 24.0F)) <= 0.0001F;
}

// These APIs intentionally expose no Calendar pointer or engine operation.
// ApplyCalendar activates only after its writes satisfy the invariant; every
// hook call revalidates the mapped bridge identity and clears this state on a
// disconnect or session rollover.
[[nodiscard]] bool ActivateAuthoritativeTick(
    std::uint64_t a_serverInstanceNonce,
    std::uint64_t a_connectionGeneration,
    float a_timeScale) noexcept;
void ResetAuthoritativeTick() noexcept;

[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::CalendarHooks
