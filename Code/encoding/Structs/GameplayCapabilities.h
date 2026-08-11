#pragma once

#include <cstdint>

namespace SkyrimTogether::Protocol
{
// Revision 13 adds the complete format-3 post-VRIK/post-HIGGS body hierarchy:
// pelvis/legs, spine, neck, clavicles, arms, and sparse named finger rotations.
// Revision 14 replaces the retained single HIGGS event with a bounded ordered
// mutation batch, keeping mutation delivery independent from HIGGS telemetry.
// Exact matching prevents older endpoints from decoding the changed layout.
// Additive negotiated capability bits do not change this revision because
// endpoints already intersect the advertised masks.
inline constexpr std::uint32_t kGameplayProtocolRevision = 14;

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
    RemoteSpatialTransfer = 1ull << 9,
    AnimationGraphSnapshot = 1ull << 10,
    VRPoseRelay = 1ull << 11,
    VRMovementRelay = 1ull << 12,
    VREquipmentRelay = 1ull << 13,
    VRActivationRelay = 1ull << 14,
    VRMagicRelay = 1ull << 15,
    VRCombatPlanckRelay = 1ull << 16,
    VRProjectileRelay = 1ull << 17,
    VRGrabRelay = 1ull << 18,
    VRHiggsRelay = 1ull << 19,
    VRAppearanceRelay = 1ull << 20,
    // Advertise only after the client can capture complete NPC ActorData and
    // continue authoritative movement/value/inventory replication at runtime.
    NpcOwnership = 1ull << 21,
    ExactAnimationActions = 1ull << 22,
    InventoryStackTransactions = 1ull << 23,
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

// This bundle is requested with the VR remote-avatar feature gate. Every
// member has a matching client service and server relay implementation.
inline constexpr GameplayCapabilityMask kVRRelayCapabilities =
    ToMask(GameplayCapability::VRPoseRelay) |
    ToMask(GameplayCapability::VRMovementRelay) |
    ToMask(GameplayCapability::VREquipmentRelay) |
    ToMask(GameplayCapability::VRActivationRelay) |
    ToMask(GameplayCapability::VRMagicRelay) |
    ToMask(GameplayCapability::VRCombatPlanckRelay) |
    ToMask(GameplayCapability::VRProjectileRelay) |
    ToMask(GameplayCapability::VRGrabRelay) |
    ToMask(GameplayCapability::VRHiggsRelay) |
    ToMask(GameplayCapability::VRAppearanceRelay);

// NPC ownership is negotiated separately from the remote-avatar relay.  A VR
// client requests it only while its mapped CommonLib bridge is actively able
// to provide the complete bounded snapshots required by the original wire.
inline constexpr GameplayCapabilityMask kVRNpcOwnershipCapabilities =
    ToMask(GameplayCapability::NpcOwnership) |
    ToMask(GameplayCapability::InventoryStackTransactions);

inline constexpr GameplayCapabilityMask kVRExactAnimationActionCapabilities =
    ToMask(GameplayCapability::ExactAnimationActions);

inline constexpr GameplayCapabilityMask kRemoteAvatarCoreCapabilities =
    ToMask(GameplayCapability::RemoteAvatarLifecycle) |
    ToMask(GameplayCapability::RemoteRootTransform) |
    ToMask(GameplayCapability::RemoteSpatialTransfer) |
    ToMask(GameplayCapability::AnimationGraphSnapshot);

// Capabilities are added here only after both endpoints implement their full
// semantics. Merely having message types or telemetry does not advertise them.
// This aggregate is the existing client-side VR feature-gate request.
inline constexpr GameplayCapabilityMask kRemoteAvatarCapabilities =
    kRemoteAvatarCoreCapabilities | kVRRelayCapabilities;

inline constexpr GameplayCapabilityMask kServerCapabilities =
    kCoreCapabilities | kRemoteAvatarCapabilities | kVRNpcOwnershipCapabilities |
    kVRExactAnimationActionCapabilities;
inline constexpr GameplayCapabilityMask kClientCapabilities =
    kCoreCapabilities | kRemoteAvatarCapabilities | kVRNpcOwnershipCapabilities |
    kVRExactAnimationActionCapabilities;

[[nodiscard]] constexpr bool IsVrGameplayClient(const GameplayCapabilityMask aMask) noexcept
{
    return (aMask & kVRRelayCapabilities) != 0;
}

// Assignment rejection uses ServerId 0 as an explicit wire sentinel. Only VR
// gameplay clients negotiate support for that sparse assignment semantics.
[[nodiscard]] constexpr bool CanReceiveAssignmentRejection(const GameplayCapabilityMask aMask) noexcept
{
    return IsVrGameplayClient(aMask);
}

[[nodiscard]] constexpr bool CanOwnNpc(const GameplayCapabilityMask aMask) noexcept
{
    return !IsVrGameplayClient(aMask) ||
           (aMask & kVRNpcOwnershipCapabilities) == kVRNpcOwnershipCapabilities;
}
} // namespace SkyrimTogether::Protocol
