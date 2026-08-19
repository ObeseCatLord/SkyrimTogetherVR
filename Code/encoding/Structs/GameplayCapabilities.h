#pragma once

#include <cstdint>

namespace SkyrimTogether::Protocol
{
// Revision 13 adds the complete format-3 post-VRIK/post-HIGGS body hierarchy:
// pelvis/legs, spine, neck, clavicles, arms, and sparse named finger rotations.
// Revision 14 replaces the retained single HIGGS event with a bounded ordered
// mutation batch, keeping mutation delivery independent from HIGGS telemetry.
// Revision 15 separates VR client identity from optional direct relays and
// makes the mandatory native gameplay-parity contract explicit at admission.
// Exact matching prevents older endpoints from decoding the changed layout.
// Additive negotiated capability bits do not change this revision because
// endpoints already intersect the advertised masks.
inline constexpr std::uint32_t kGameplayProtocolRevision = 15;

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
    VrClient = 1ull << 24,
    NativeGameplayParity = 1ull << 25,
    VrGameplayClient = 1ull << 26,
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

// Complete direct-packet vocabulary. Presence here means the packet has a
// schema and relay, not that it is safe to negotiate as gameplay.
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

// Direct packets are optional VR extensions. Only handlers that perform a
// complete receive/apply operation are advertised. Equipment, magic, combat,
// and projectile diagnostics remain protocol-defined but cannot be negotiated
// as gameplay capabilities.
inline constexpr GameplayCapabilityMask kFunctionalVRRelayCapabilities =
    ToMask(GameplayCapability::VRPoseRelay) |
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
    kRemoteAvatarCoreCapabilities | kFunctionalVRRelayCapabilities;

enum class VRProductionProfile : std::uint8_t
{
    ConnectionOnly,
    AvatarSync,
    Gameplay,
};

// These profile baselines are the complete non-relay contracts emitted by the
// three production VR targets. Direct relays are intentionally added only
// after their corresponding runtime service has proved operational.
inline constexpr GameplayCapabilityMask kVRConnectionOnlyProfileCapabilities =
    kCoreCapabilities | ToMask(GameplayCapability::VrClient);
inline constexpr GameplayCapabilityMask kVRAvatarSyncProfileCapabilities =
    kVRConnectionOnlyProfileCapabilities | kRemoteAvatarCoreCapabilities;
inline constexpr GameplayCapabilityMask kVRGameplayProfileCapabilities =
    kVRAvatarSyncProfileCapabilities | kVRNpcOwnershipCapabilities |
    ToMask(GameplayCapability::NativeGameplayParity) |
    ToMask(GameplayCapability::VrGameplayClient);

[[nodiscard]] constexpr GameplayCapabilityMask BuildVRProductionCapabilities(
    const VRProductionProfile aProfile,
    const GameplayCapabilityMask aOperationalDirectRelayCapabilities = 0,
    const bool aSupportsExactAnimationActions = false) noexcept
{
    auto capabilities = kVRConnectionOnlyProfileCapabilities;
    if (aProfile == VRProductionProfile::ConnectionOnly)
        return capabilities;

    capabilities = kVRAvatarSyncProfileCapabilities;
    capabilities |= aOperationalDirectRelayCapabilities & kFunctionalVRRelayCapabilities;
    if (aSupportsExactAnimationActions)
        capabilities |= kVRExactAnimationActionCapabilities;
    if (aProfile == VRProductionProfile::Gameplay)
        capabilities |= kVRGameplayProfileCapabilities & ~kVRAvatarSyncProfileCapabilities;
    return capabilities;
}

inline constexpr GameplayCapabilityMask kServerCapabilities =
    kCoreCapabilities | kRemoteAvatarCapabilities | kVRNpcOwnershipCapabilities |
    kVRExactAnimationActionCapabilities | ToMask(GameplayCapability::VrClient) |
    ToMask(GameplayCapability::NativeGameplayParity) |
    ToMask(GameplayCapability::VrGameplayClient);
inline constexpr GameplayCapabilityMask kClientCapabilities =
    kCoreCapabilities | kRemoteAvatarCapabilities | kVRNpcOwnershipCapabilities |
    kVRExactAnimationActionCapabilities | ToMask(GameplayCapability::VrClient) |
    ToMask(GameplayCapability::NativeGameplayParity) |
    ToMask(GameplayCapability::VrGameplayClient);

[[nodiscard]] constexpr bool IsVrClient(const GameplayCapabilityMask aMask) noexcept
{
    return HasCapability(aMask, GameplayCapability::VrClient);
}

[[nodiscard]] constexpr bool IsVrGameplayClient(const GameplayCapabilityMask aMask) noexcept
{
    return IsVrClient(aMask) && HasCapability(aMask, GameplayCapability::VrGameplayClient);
}

[[nodiscard]] constexpr bool HasNativeGameplayParity(const GameplayCapabilityMask aMask) noexcept
{
    return HasCapability(aMask, GameplayCapability::NativeGameplayParity);
}

inline constexpr GameplayCapabilityMask kVRIdentityRequiredCapabilities =
    kVRRelayCapabilities | kVRNpcOwnershipCapabilities |
    ToMask(GameplayCapability::NativeGameplayParity) |
    ToMask(GameplayCapability::VrGameplayClient);

[[nodiscard]] constexpr bool HasVrGameplayIntent(const GameplayCapabilityMask aMask) noexcept
{
    return HasCapability(aMask, GameplayCapability::VrGameplayClient);
}

[[nodiscard]] constexpr bool HasVRNpcOwnershipContract(const GameplayCapabilityMask aMask) noexcept
{
    return (aMask & kVRNpcOwnershipCapabilities) == kVRNpcOwnershipCapabilities;
}

[[nodiscard]] constexpr bool HasCompleteVRGameplayContract(const GameplayCapabilityMask aMask) noexcept
{
    return (aMask & kVRGameplayProfileCapabilities) == kVRGameplayProfileCapabilities;
}

[[nodiscard]] constexpr bool CanAdmitGameplayClient(const GameplayCapabilityMask aMask) noexcept
{
    if ((aMask & kCoreCapabilities) != kCoreCapabilities)
        return false;

    // These capabilities alter VR-only routing and actor-data semantics. They
    // must never be accepted as an otherwise indistinguishable desktop peer.
    if (!IsVrClient(aMask))
        return (aMask & kVRIdentityRequiredCapabilities) == 0;

    if ((aMask & (kVRRelayCapabilities & ~kFunctionalVRRelayCapabilities)) != 0)
        return false;

    const auto avatarCoreCapabilities = aMask & kRemoteAvatarCoreCapabilities;
    if (avatarCoreCapabilities != 0 && avatarCoreCapabilities != kRemoteAvatarCoreCapabilities)
        return false;
    if ((aMask & kVRRelayCapabilities) != 0 &&
        avatarCoreCapabilities != kRemoteAvatarCoreCapabilities)
        return false;

    if (HasVrGameplayIntent(aMask))
        return HasCompleteVRGameplayContract(aMask);

    return !HasNativeGameplayParity(aMask) && !HasVRNpcOwnershipContract(aMask) &&
           (aMask & kVRNpcOwnershipCapabilities) == 0;
}

// Assignment rejection uses ServerId 0 as an explicit wire sentinel. Only VR
// gameplay clients negotiate support for that sparse assignment semantics.
[[nodiscard]] constexpr bool CanReceiveAssignmentRejection(const GameplayCapabilityMask aMask) noexcept
{
    return IsVrGameplayClient(aMask) && CanAdmitGameplayClient(aMask);
}

[[nodiscard]] constexpr bool CanOwnNpc(const GameplayCapabilityMask aMask) noexcept
{
    // Desktop clients retain their existing ownership semantics, while an
    // unmarked VR tuple cannot fall through this branch as a desktop client.
    if (!IsVrClient(aMask))
        return (aMask & kVRIdentityRequiredCapabilities) == 0;
    return IsVrGameplayClient(aMask) && HasVRNpcOwnershipContract(aMask) &&
           CanAdmitGameplayClient(aMask);
}
} // namespace SkyrimTogether::Protocol
