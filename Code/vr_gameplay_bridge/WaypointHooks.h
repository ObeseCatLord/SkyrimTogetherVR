#pragma once

#include <array>
#include <cstdint>

namespace RE
{
class NiPoint3;
class PlayerCharacter;
class TESWorldSpace;
}

namespace SkyrimTogetherVR::GameplayAdapter::WaypointHooks
{
// These direct RVAs are verified against the pinned Skyrim VR 1.4.15.0
// executable. The generated desktop rows are unrelated functions in VR.
inline constexpr std::uintptr_t kSetWaypointVrRva = 0x06C74D0;
inline constexpr std::array<std::uint8_t, 16> kSetWaypointVrPrologue{
    0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24,
};
inline constexpr std::uintptr_t kRemoveWaypointVrRva = 0x06C7630;
inline constexpr std::array<std::uint8_t, 16> kRemoveWaypointVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8D, 0x05, 0x1F, 0x20, 0x8C,
};

using SetWaypoint = void (*)(RE::PlayerCharacter*, RE::NiPoint3*, RE::TESWorldSpace*);
using RemoveWaypoint = void (*)(RE::PlayerCharacter*);

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return kSetWaypointVrRva == 0x06C74D0 && kSetWaypointVrPrologue.size() == 16 &&
           kRemoveWaypointVrRva == 0x06C7630 && kRemoveWaypointVrPrologue.size() == 16;
}

// This policy remains independent of game state so it can be tested without
// loading SkyrimVR.exe. The actual baseline update belongs to local capture.
[[nodiscard]] constexpr bool ShouldPublishObservedWaypoint(
    const bool a_remoteReplay,
    const bool a_postconditionSatisfied,
    const bool a_matchesBaseline) noexcept
{
    return !a_remoteReplay && a_postconditionSatisfied && !a_matchesBaseline;
}

[[nodiscard]] constexpr bool ShouldLogAggregate(const std::uint64_t a_count) noexcept
{
    return a_count != 0 && (a_count & (a_count - 1)) == 0;
}

class ScopedRemoteWaypointReplay final
{
public:
    ScopedRemoteWaypointReplay() noexcept;
    ~ScopedRemoteWaypointReplay() noexcept;

    ScopedRemoteWaypointReplay(const ScopedRemoteWaypointReplay&) = delete;
    ScopedRemoteWaypointReplay& operator=(const ScopedRemoteWaypointReplay&) = delete;
};

[[nodiscard]] bool IsRemoteWaypointReplay() noexcept;

// Calls the verified, detoured native target. Inbound callers must hold
// ScopedRemoteWaypointReplay so the local outbound capture detour cannot
// echo the server-authoritative mutation.
[[nodiscard]] bool InvokeSetWaypoint(
    RE::PlayerCharacter* a_player,
    RE::NiPoint3* a_position,
    RE::TESWorldSpace* a_worldspace) noexcept;
[[nodiscard]] bool InvokeRemoveWaypoint(RE::PlayerCharacter* a_player) noexcept;

[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::WaypointHooks

namespace SkyrimTogetherVR::GameplayAdapter::MountCapturePolicy
{
inline constexpr std::uint32_t kHorseEnterAnimationEventId = 20;

[[nodiscard]] constexpr bool ShouldPublishEventAssistedMount(
    const std::uint32_t a_animationEventId,
    const bool a_validMount,
    const bool a_baselineCaptured,
    const bool a_matchesBaseline) noexcept
{
    return a_animationEventId == kHorseEnterAnimationEventId && a_validMount &&
           (!a_baselineCaptured || !a_matchesBaseline);
}

// The first unmounted observation establishes a baseline. Once a mount state
// has been captured, every transition is durable, including the zero-valued
// dismount sentinel consumed by GameplayAction::Mount.
[[nodiscard]] constexpr bool ShouldPublishObservedMount(
    const std::uint32_t a_mountFormId,
    const bool a_baselineCaptured,
    const std::uint32_t a_baselineMountFormId) noexcept
{
    return (a_mountFormId != 0 && !a_baselineCaptured) ||
           (a_baselineCaptured && a_mountFormId != a_baselineMountFormId);
}
} // namespace SkyrimTogetherVR::GameplayAdapter::MountCapturePolicy
