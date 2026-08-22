#pragma once

#include <cstdint>

namespace SkyrimTogether::Protocol
{
// Revision 13 adds the complete format-3 post-VRIK/post-HIGGS body hierarchy:
// pelvis/legs, spine, neck, clavicles, arms, and sparse named finger rotations.
// Revision 14 replaces the retained single HIGGS event with a bounded ordered
// mutation batch, keeping mutation delivery independent from HIGGS telemetry.
// Revision 15 separates VR client identity from optional direct relays and
// makes the mandatory native gameplay-core contract explicit at admission.
// Revision 16 identity-authorizes attacker-originated health deltas against an
// owned attacker entity and rejects duplicate action nonces. This is replay
// resistance and sender authority, not proof that a compromised client
// reported truthful damage.
// Revision 17 adds server-issued mutation event IDs for inventory, health,
// projectile, spell-cast, and magic-effect relays, plus explicit canonical
// actor and final-equipment resynchronization requests. Receivers deduplicate
// one retransmitted edge by event identity rather than equal payload bytes,
// and terminal recovery is no longer dependent on an unsolicited update.
// Revision 18 binds animation-variable arrays to their ordered descriptor list
// with a digest and carries the verified direction-float index. Receivers reject
// mismatched graph layouts instead of applying values to unrelated variables.
// Exact matching prevents older endpoints from decoding the changed layout.
// Additive negotiated capability bits do not change this revision because
// endpoints already intersect the advertised masks.
// Revision 19 adds HIGGS producer epochs, explicit mutation-window rebases,
// captured terminal references, and bounded grabbed-node identities.
// Revision 20 adds the optional PLANCK interface002 physics relay. It is not
// part of the base gameplay profile and is advertised only after the exact
// required local interface002 feature set has been proven operational.
// Revision 21 makes ordinary quest updates owner/revision-authenticated and
// makes canonical quest recovery owner-scoped.
inline constexpr std::uint32_t kGameplayProtocolRevision = 21;

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
    // Canonical native gameplay capture/apply and ownership are operational.
    // This is a network-routing contract; it does not advertise desktop UI,
    // input, administration UX, or optional VR embodiment extensions.
    NativeGameplayCore = 1ull << 25,
    VrGameplayClient = 1ull << 26,
    // The canonical transaction-id equipment protocol carries the complete
    // final worn state plus left/right spells and shout selection.
    FinalEquipmentTransactions = 1ull << 27,
    CanonicalStateResync = 1ull << 28,
    // Revisioned server-authored mount, object, and owner-scoped quest
    // snapshots. Deliberately not advertised by a client until it can apply
    // every snapshot and reject late request IDs/revisions.
    RevisionedCanonicalRecovery = 1ull << 29,
    PlanckPhysicsInterface002 = 1ull << 30,
};

using GameplayCapabilityMask = std::uint64_t;

// Small wire-policy helpers shared by the revisioned canonical recovery
// lanes. A canonical response must advance both the revision supplied with
// its request and the revision already committed locally. Request IDs are
// intentionally nonzero because zero remains an invalid wire sentinel.
namespace RevisionedCanonicalRecoveryPolicy
{
[[nodiscard]] constexpr std::uint32_t NextNonZeroRequestId(std::uint32_t& arNextRequestId) noexcept
{
    auto requestId = arNextRequestId++;
    if (arNextRequestId == 0)
        arNextRequestId = 1;
    if (requestId == 0) {
        requestId = arNextRequestId++;
        if (arNextRequestId == 0)
            arNextRequestId = 1;
    }
    return requestId;
}

[[nodiscard]] constexpr bool CanAcceptCanonicalResponse(
    const std::uint32_t aExpectedRequestId, const std::uint32_t aResponseRequestId,
    const std::uint64_t aKnownRevision, const std::uint64_t aCommittedRevision,
    const std::uint64_t aCanonicalRevision) noexcept
{
    return aExpectedRequestId != 0 && aExpectedRequestId == aResponseRequestId &&
           aCanonicalRevision > aKnownRevision && aCanonicalRevision > aCommittedRevision;
}

[[nodiscard]] constexpr bool IsAuthoritativeDismount(const std::uint32_t aMountId) noexcept
{
    return aMountId == 0;
}

[[nodiscard]] constexpr bool ShouldRetryMountApplication(
    const std::uint8_t aFailedAttempts, const std::uint8_t aMaximumAttempts) noexcept
{
    return aMaximumAttempts != 0 && aFailedAttempts < aMaximumAttempts;
}

// Quest recovery is keyed by the party member whose log is canonical, not by
// the peer that happened to request recovery after a remote native failure.
[[nodiscard]] constexpr bool CanAuthorizeQuestOwner(
    const std::uint32_t aRequesterPlayerId, const std::uint32_t aOwnerPlayerId,
    const std::uint32_t aRequesterPartyId, const std::uint32_t aOwnerPartyId) noexcept
{
    return aRequesterPlayerId != 0 && aOwnerPlayerId != 0 && aRequesterPartyId != 0 &&
           aRequesterPartyId == aOwnerPartyId;
}

[[nodiscard]] constexpr bool DoesQuestUpdateSupersedeSnapshot(
    const std::uint32_t aSnapshotOwnerPlayerId, const std::uint64_t aSnapshotRevision,
    const std::uint32_t aUpdateOwnerPlayerId, const std::uint64_t aUpdateRevision) noexcept
{
    return aSnapshotOwnerPlayerId != 0 && aSnapshotOwnerPlayerId == aUpdateOwnerPlayerId &&
           aUpdateRevision > aSnapshotRevision;
}

[[nodiscard]] constexpr bool CanCommitQuestSnapshot(
    const std::uint32_t aExpectedOwnerPlayerId, const std::uint32_t aResponseOwnerPlayerId,
    const std::uint32_t aExpectedRequestId, const std::uint32_t aResponseRequestId,
    const std::uint64_t aKnownRevision, const std::uint64_t aCommittedRevision,
    const std::uint64_t aObservedRevision, const std::uint64_t aResponseRevision) noexcept
{
    return aExpectedOwnerPlayerId != 0 && aExpectedOwnerPlayerId == aResponseOwnerPlayerId &&
           aExpectedRequestId != 0 && aExpectedRequestId == aResponseRequestId &&
           aResponseRevision > aKnownRevision && aResponseRevision > aCommittedRevision &&
           aResponseRevision >= aObservedRevision;
}
} // namespace RevisionedCanonicalRecoveryPolicy

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
    ToMask(GameplayCapability::PlanckPhysicsInterface002) |
    ToMask(GameplayCapability::VRAppearanceRelay);

// Direct packets are optional VR extensions. Only handlers that perform a
// complete receive/apply operation are advertised. VREquipmentRelay remains
// protocol-defined but unsupported until RequestVREquipmentUpdate has both a
// producer and an applying receiver; final equipment transactions use their
// separate canonical capability below.
inline constexpr GameplayCapabilityMask kFunctionalVRRelayCapabilities =
    ToMask(GameplayCapability::VRPoseRelay) |
    ToMask(GameplayCapability::VRHiggsRelay) |
    ToMask(GameplayCapability::PlanckPhysicsInterface002) |
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
// three production VR targets. Optional direct relays are added only after
// their corresponding runtime service has proved operational. Native gameplay
// requires the complete canonical final-equipment transaction contract.
inline constexpr GameplayCapabilityMask kVRConnectionOnlyProfileCapabilities =
    kCoreCapabilities | ToMask(GameplayCapability::VrClient);
inline constexpr GameplayCapabilityMask kVRAvatarSyncProfileCapabilities =
    kVRConnectionOnlyProfileCapabilities | kRemoteAvatarCoreCapabilities;
inline constexpr GameplayCapabilityMask kVRGameplayProfileCapabilities =
    kVRAvatarSyncProfileCapabilities | kVRNpcOwnershipCapabilities |
    kVRExactAnimationActionCapabilities |
    ToMask(GameplayCapability::NativeGameplayCore) |
    ToMask(GameplayCapability::VrGameplayClient) |
    ToMask(GameplayCapability::FinalEquipmentTransactions) |
    ToMask(GameplayCapability::CanonicalStateResync) |
    ToMask(GameplayCapability::RevisionedCanonicalRecovery);

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
    // A gameplay-capable client without exact action replay must remain in the
    // explicitly degraded AvatarSync profile. Do not advertise the gameplay
    // tuple unless the native bridge proved this required capability.
    if (aProfile == VRProductionProfile::Gameplay && aSupportsExactAnimationActions)
        capabilities |= kVRGameplayProfileCapabilities & ~kVRAvatarSyncProfileCapabilities;
    return capabilities;
}

inline constexpr GameplayCapabilityMask kServerCapabilities =
    kCoreCapabilities | kRemoteAvatarCapabilities | kVRNpcOwnershipCapabilities |
    kVRExactAnimationActionCapabilities | ToMask(GameplayCapability::VrClient) |
    ToMask(GameplayCapability::NativeGameplayCore) |
    ToMask(GameplayCapability::VrGameplayClient) |
    ToMask(GameplayCapability::FinalEquipmentTransactions) |
    ToMask(GameplayCapability::CanonicalStateResync) |
    ToMask(GameplayCapability::RevisionedCanonicalRecovery);
inline constexpr GameplayCapabilityMask kClientCapabilities =
    kCoreCapabilities | kRemoteAvatarCapabilities | kVRNpcOwnershipCapabilities |
    kVRExactAnimationActionCapabilities | ToMask(GameplayCapability::VrClient) |
    ToMask(GameplayCapability::NativeGameplayCore) |
    ToMask(GameplayCapability::VrGameplayClient) |
    ToMask(GameplayCapability::FinalEquipmentTransactions) |
    ToMask(GameplayCapability::CanonicalStateResync) |
    ToMask(GameplayCapability::RevisionedCanonicalRecovery);

[[nodiscard]] constexpr bool IsVrClient(const GameplayCapabilityMask aMask) noexcept
{
    return HasCapability(aMask, GameplayCapability::VrClient);
}

[[nodiscard]] constexpr bool IsVrGameplayClient(const GameplayCapabilityMask aMask) noexcept
{
    return IsVrClient(aMask) && HasCapability(aMask, GameplayCapability::VrGameplayClient);
}

[[nodiscard]] constexpr bool HasNativeGameplayCore(const GameplayCapabilityMask aMask) noexcept
{
    return HasCapability(aMask, GameplayCapability::NativeGameplayCore);
}

inline constexpr GameplayCapabilityMask kVRIdentityRequiredCapabilities =
    kVRRelayCapabilities | kVRNpcOwnershipCapabilities |
    ToMask(GameplayCapability::NativeGameplayCore) |
    ToMask(GameplayCapability::VrGameplayClient) |
    ToMask(GameplayCapability::FinalEquipmentTransactions) |
    ToMask(GameplayCapability::CanonicalStateResync) |
    ToMask(GameplayCapability::RevisionedCanonicalRecovery);

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

    return !HasNativeGameplayCore(aMask) && !HasVRNpcOwnershipContract(aMask) &&
           (aMask & kVRNpcOwnershipCapabilities) == 0 &&
           !HasCapability(aMask, GameplayCapability::FinalEquipmentTransactions) &&
           !HasCapability(aMask, GameplayCapability::CanonicalStateResync) &&
           !HasCapability(aMask, GameplayCapability::RevisionedCanonicalRecovery);
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
