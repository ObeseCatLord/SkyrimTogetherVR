#pragma once

#include <cstdint>

namespace SkyrimTogether::Protocol
{
inline constexpr std::uint32_t kGameplayProtocolRevision = 1;

enum class GameplayCapability : std::uint64_t
{
    CanonicalEntityIdentity = 1ull << 0,
    SessionIdentity = 1ull << 1,
    RemoteAvatarLifecycle = 1ull << 2,
    RemoteRootTransform = 1ull << 3,
    VrPose = 1ull << 4,
    VrikPose = 1ull << 5,
    HiggsInteractionIntent = 1ull << 6,
    PlanckInteractionIntent = 1ull << 7,
    FullBodyTrackingPose = 1ull << 8,
};

using GameplayCapabilityMask = std::uint64_t;

[[nodiscard]] constexpr GameplayCapabilityMask ToMask(GameplayCapability aCapability) noexcept
{
    return static_cast<GameplayCapabilityMask>(aCapability);
}

[[nodiscard]] constexpr bool HasCapability(GameplayCapabilityMask aMask, GameplayCapability aCapability) noexcept
{
    return (aMask & ToMask(aCapability)) != 0;
}

inline constexpr GameplayCapabilityMask kCoreCapabilities =
    ToMask(GameplayCapability::CanonicalEntityIdentity) |
    ToMask(GameplayCapability::SessionIdentity);

// Capabilities are added here only after both endpoints implement their full
// semantics. Merely having message types or telemetry does not advertise them.
inline constexpr GameplayCapabilityMask kRemoteAvatarCapabilities =
    ToMask(GameplayCapability::RemoteAvatarLifecycle) |
    ToMask(GameplayCapability::RemoteRootTransform);

inline constexpr GameplayCapabilityMask kServerCapabilities =
    kCoreCapabilities | kRemoteAvatarCapabilities;
inline constexpr GameplayCapabilityMask kClientCapabilities =
    kCoreCapabilities | kRemoteAvatarCapabilities;
} // namespace SkyrimTogether::Protocol
