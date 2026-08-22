#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <entt/entt.hpp>

#include <vr_common/VRGameplayBridge.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifySpellCast.h>
#include <Structs/ActionEvent.h>
#include <Structs/Inventory.h>
#include <Structs/VRAppearance.h>
#include <Structs/VREquipmentUpdate.h>
#include <Structs/VRHiggsState.h>
#include <Structs/VRPlanckPhysicsEvent.h>

struct DisconnectedEvent;
struct NotifyActorMaxValueChanges;
struct NotifyActorResync;
struct NotifyActorValueChanges;
struct NotifyDeathStateChange;
struct NotifyHealthChangeBroadcast;
struct NotifyDrawWeapon;
struct NotifyEquipmentChanges;
struct NotifyFactionsChanges;
struct NotifyInventoryChanges;
struct NotifyMount;
struct NotifyMountResync;
struct NotifyRemoveSpell;
struct NotifyRemoveCharacter;
struct NotifyPlayerLeft;
struct NotifyPlayerLevel;
struct NotifyProjectileLaunch;
struct NotifyRespawn;
struct NotifySpawnData;
struct NotifySpellCast;
struct NotifyInterruptCast;
struct NotifyVRCombatHitEvent;
struct NotifyVREquipmentUpdate;
struct NotifyVRGrabEvent;
struct NotifyVRHiggsState;
struct NotifyVRPlanckPhysicsEvent;
struct NotifyVRAppearance;
struct NotifyVRMagicEffectEvent;
struct NotifyVRPoseUpdate;
struct NotifyVRProjectileEvent;
struct ServerReferencesMoveRequest;
namespace SkyrimTogetherVR { struct RemoteGameplayBridgeResultEvent; }
namespace SkyrimTogetherVR { struct LocalGameplayBridgeEvent; }
struct TransportService;
struct VRAvatarService;
struct VRNpcOwnershipService;
struct World;
struct UpdateEvent;

namespace SkyrimTogetherVR::ActorReplicationRecovery
{
enum class Disposition : std::uint8_t
{
    Retry,
    Terminal,
};

// These failures are reported before an equipment snapshot's final commit
// action can mutate the actor. A reported result has an explicit pre-mutation
// meaning; a missing result does not, because it may be buffered after a
// bridge-side mutation.
[[nodiscard]] constexpr bool IsRetryablePreMutationStatus(
    const GameplayBridge::CommandStatus aStatus) noexcept
{
    switch (aStatus) {
    case GameplayBridge::CommandStatus::Inactive:
    case GameplayBridge::CommandStatus::StaleEntity:
    case GameplayBridge::CommandStatus::InvalidHandle:
    case GameplayBridge::CommandStatus::MissingForm:
    case GameplayBridge::CommandStatus::MissingCell:
    case GameplayBridge::CommandStatus::EngineRejected:
    case GameplayBridge::CommandStatus::QueueOverflow:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr Disposition ClassifySpawnResult(
    const GameplayBridge::CommandStatus, const std::uint8_t,
    const std::uint8_t) noexcept
{
    // Retained spawn snapshots have no canonical state revision or ordering
    // barrier. Replaying one can overwrite a newer server update, even when a
    // particular failed command reports no mutation. Wait for fresh canonical
    // spawn data instead.
    return Disposition::Terminal;
}

[[nodiscard]] constexpr Disposition ClassifySpawnTimeout(
    const std::uint8_t, const std::uint8_t) noexcept
{
    return Disposition::Terminal;
}

// Spawn action history can contain one-shot actor actions and legacy animation
// events. It is valid only for the initial submission of an entity generation;
// retained spawn snapshots are never replayed for recovery.
[[nodiscard]] constexpr bool ShouldReplaySpawnActionHistory(const std::uint8_t aResyncAttempts) noexcept
{
    return aResyncAttempts == 0;
}

[[nodiscard]] constexpr bool IsBeforeEquipmentFinalMutation(
    const std::uint16_t aResultIndex, const std::uint16_t aExpectedResults) noexcept
{
    return aExpectedResults != 0 && aResultIndex + 1 < aExpectedResults;
}

[[nodiscard]] constexpr Disposition ClassifyEquipmentResult(
    const GameplayBridge::CommandStatus aStatus, const bool aBeforeFinalMutation,
    const std::uint8_t aResultFailures, const std::uint8_t aMaximumResultFailures) noexcept
{
    return aBeforeFinalMutation && IsRetryablePreMutationStatus(aStatus) &&
               aResultFailures < aMaximumResultFailures ?
               Disposition::Retry : Disposition::Terminal;
}

[[nodiscard]] constexpr Disposition ClassifyEquipmentTimeout(
    const bool, const std::uint8_t,
    const std::uint8_t) noexcept
{
    // The result queue can retain a successful final commit while the client
    // waits. Without a result, position in the expected sequence cannot prove
    // that mutation has not happened, so replay is ambiguous.
    return Disposition::Terminal;
}

// Admission order is assigned when semantic work is staged, before it can
// enter the bridge. A later item must not consume the same ledger revision as
// an earlier unadmitted item in its acceptance domain.
[[nodiscard]] constexpr bool IsEarlierAdmissionOrder(
    const std::uint64_t aCandidateOrder, const std::uint64_t aOtherOrder) noexcept
{
    return aOtherOrder != 0 && aOtherOrder < aCandidateOrder;
}

[[nodiscard]] constexpr bool CanRefreshUnadmittedAcceptance(const bool aAdmitted) noexcept
{
    return !aAdmitted;
}

// Retries are cadence-limited, but a head that cannot enter the bridge must
// still age out. Keeping the timeout policy separate prevents a retry from
// extending an unadmitted FIFO reservation indefinitely.
[[nodiscard]] constexpr bool HasCumulativeAdmissionTimedOut(
    const double aAdmissionAge, const double aTimeout) noexcept
{
    return aTimeout > 0.0 && aAdmissionAge >= aTimeout;
}

[[nodiscard]] constexpr bool ShouldRetireUnadmittedAdmissionHead(
    const bool aAdmitted, const bool aIsAdmissionHead, const double aAdmissionAge,
    const double aTimeout) noexcept
{
    return !aAdmitted && aIsAdmissionHead &&
           HasCumulativeAdmissionTimedOut(aAdmissionAge, aTimeout);
}

// A canonical mount revision becomes committed only after the native action
// has succeeded for the same rider/entity generation that staged it.
[[nodiscard]] constexpr bool CanCommitCanonicalMountCompletion(
    const bool aNativeSuccess, const bool aCurrentGeneration,
    const std::uint64_t aCanonicalRevision) noexcept
{
    return aNativeSuccess && aCurrentGeneration && aCanonicalRevision != 0;
}

[[nodiscard]] constexpr bool MustRefreshCanonicalMountRecovery(
    const bool aNativeSuccess, const bool aCurrentGeneration,
    const bool aSuperseded) noexcept
{
    return !aSuperseded && (!aNativeSuccess || !aCurrentGeneration);
}

[[nodiscard]] constexpr bool IsDuplicateAdmissionIdentity(
    const std::uint32_t aLeftPlayerId, const std::uint16_t aLeftDomain,
    const std::uint32_t aLeftSequence, const std::uint64_t aLeftSignature,
    const std::uint8_t aLeftChannel, const std::uint32_t aRightPlayerId,
    const std::uint16_t aRightDomain, const std::uint32_t aRightSequence,
    const std::uint64_t aRightSignature, const std::uint8_t aRightChannel) noexcept
{
    return aLeftPlayerId == aRightPlayerId && aLeftDomain == aRightDomain &&
           aLeftSequence == aRightSequence && aLeftSignature == aRightSignature &&
           aLeftChannel == aRightChannel;
}

[[nodiscard]] constexpr bool IsRetiredLedgerIdentity(
    const std::uint32_t aCandidateId, const std::uint32_t aServerId,
    const std::uint32_t aMappedPlayerId) noexcept
{
    return aCandidateId != 0 &&
           (aCandidateId == aServerId || (aMappedPlayerId != 0 && aCandidateId == aMappedPlayerId));
}

[[nodiscard]] constexpr bool IsServerIdIdentityReplacement(
    const std::uint32_t aExistingPlayerId, const std::uint32_t aIncomingPlayerId) noexcept
{
    return aExistingPlayerId != aIncomingPlayerId;
}

[[nodiscard]] constexpr bool IsPlayerIdIdentityReplacement(
    const std::uint32_t aPlayerId, const std::uint32_t aExistingServerId,
    const std::uint32_t aIncomingServerId) noexcept
{
    return aPlayerId != 0 && aExistingServerId != 0 &&
           aExistingServerId != aIncomingServerId;
}

[[nodiscard]] constexpr bool IsLocalServerIdIdentityReplacement(
    const std::uint32_t aPreviousServerId, const std::uint32_t aIncomingServerId) noexcept
{
    return aPreviousServerId != 0 && aPreviousServerId != aIncomingServerId;
}

[[nodiscard]] constexpr bool IsSameSpawnEntityIdentity(
    const GameplayBridge::BridgeIdentity& acLeft,
    const GameplayBridge::BridgeIdentity& acRight) noexcept
{
    return acLeft.ServerInstanceNonce == acRight.ServerInstanceNonce &&
           acLeft.ConnectionGeneration == acRight.ConnectionGeneration &&
           acLeft.LifecycleEpoch == acRight.LifecycleEpoch &&
           acLeft.EntityId == acRight.EntityId &&
           acLeft.EntityGeneration == acRight.EntityGeneration;
}

[[nodiscard]] constexpr bool IsSpawnEntityIdentityReplacement(
    const std::uint32_t aExistingServerId,
    const GameplayBridge::BridgeIdentity& acExisting,
    const std::uint32_t aIncomingServerId,
    const GameplayBridge::BridgeIdentity& acIncoming) noexcept
{
    if (aExistingServerId == aIncomingServerId)
        return !IsSameSpawnEntityIdentity(acExisting, acIncoming);

    return acExisting.ServerInstanceNonce == acIncoming.ServerInstanceNonce &&
           acExisting.ConnectionGeneration == acIncoming.ConnectionGeneration &&
           acExisting.LifecycleEpoch == acIncoming.LifecycleEpoch &&
           acExisting.EntityId == acIncoming.EntityId &&
           acExisting.EntityGeneration != acIncoming.EntityGeneration;
}

// Canonical resync retries run in bounded rounds. Exhausting one round gets a
// new request identity after this capped exponential delay, so quarantine is
// never permanent solely because a response was lost.
[[nodiscard]] constexpr std::uint8_t CanonicalResyncBackoffMultiplier(
    const std::uint8_t aExhaustedRounds) noexcept
{
    constexpr std::uint8_t kMaximumExponent = 4;
    const auto exponent = aExhaustedRounds < kMaximumExponent ? aExhaustedRounds : kMaximumExponent;
    return static_cast<std::uint8_t>(1u << exponent);
}

[[nodiscard]] constexpr bool ShouldLogCanonicalResyncExhaustion(
    const std::uint8_t aExhaustedRounds) noexcept
{
    return aExhaustedRounds != 0 && (aExhaustedRounds & (aExhaustedRounds - 1)) == 0;
}

[[nodiscard]] constexpr bool ShouldRotateCanonicalResyncRequest(
    const std::uint8_t aAttempts, const std::uint8_t aMaximumAttempts) noexcept
{
    return aMaximumAttempts != 0 && aAttempts >= aMaximumAttempts;
}

[[nodiscard]] constexpr bool CanLiftCanonicalResyncQuarantine(
    const bool aStagingSucceeded) noexcept
{
    return aStagingSucceeded;
}

struct SpawnRecoveryState
{
    GameplayBridge::BridgeIdentity EntityIdentity{};
    std::uint8_t ResyncAttempts{};
    bool HasEntityIdentity{};

    [[nodiscard]] constexpr bool MatchesCurrentEntity(
        const GameplayBridge::BridgeIdentity& acIdentity) const noexcept
    {
        return HasEntityIdentity && IsSameSpawnEntityIdentity(EntityIdentity, acIdentity);
    }

    constexpr void Reset() noexcept { *this = {}; }
};
} // namespace SkyrimTogetherVR::ActorReplicationRecovery

/**
 * Maps remote server gameplay messages to fixed GameplayActionPayload records.
 *
 * Payload encoding is action-specific but always deterministic: LocalFormIdA-D
 * hold translated GameIds in wire-field order, ValueA-B hold integral wire
 * fields, ScalarA-D hold floating wire fields, and ActionFlags packs boolean,
 * enum, and compact node metadata. Pose and equipment indices occupy bits 8+;
 * boolean flags occupy bits 0-4. Opaque strings/appearance blobs are never
 * decoded here; face-tint color/type/alpha is emitted when supplied.
 */
struct VRActorReplicationService
{
    VRActorReplicationService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                              VRAvatarService& aAvatars, VRNpcOwnershipService& aNpcOwnership) noexcept;
    ~VRActorReplicationService() noexcept = default;

    TP_NOCOPYMOVE(VRActorReplicationService);

    [[nodiscard]] bool TryGetLatestLocalActorAction(ActionEvent& arAction) const noexcept;
    [[nodiscard]] bool TryGetLatestLocalActorAction(std::uint32_t aActorLocalFormId,
                                                     ActionEvent& arAction) const noexcept;

private:
    struct SequenceLedger
    {
        std::uint32_t LastSequence{0};
        std::uint64_t LastSignature{0};
        std::uint64_t Revision{0};
        bool HasSequence{false};
        bool HasSignature{false};
    };

    // Channel 0 is canonical/domain traffic; channel 1 is the independent
    // dedicated grab observer. Their producer sequence counters are unrelated.
    using DomainLedgers = std::array<SequenceLedger, 36>;

    struct HiggsEventLedger
    {
        std::uint64_t ProducerEpoch{0};
        std::uint32_t LastAdmittedMutationSequence{0};
        std::uint32_t LastSkippedMutationSequence{0};
        std::uint32_t LastTerminalMutationSequence{0};
        std::uint32_t LastQueuedMutationSequence{0};
        bool HasAdmittedMutationSequence{false};
        bool HasSkippedMutationSequence{false};
        bool HasTerminalMutationSequence{false};
        bool HasQueuedMutationSequence{false};
        std::uint32_t SkippedMutationCount{0};
    };

    struct PendingHiggsMutation
    {
        VRHiggsEventSnapshot Event{};
        VRHiggsGrabTransform GrabTransform{};
        bool TwoHanding{false};
        bool HasGrabTransform{false};
        double ResolutionElapsed{0.0};
        std::uint8_t ResolutionAttempts{0};
    };

    // This is a snapshot of the committed ledger, not an ActionId. A retry
    // rebuilds bridge commands and gets new identities while retaining the
    // original semantic admission.
    struct AcceptanceToken
    {
        std::uint32_t PlayerId{};
        SkyrimTogetherVR::GameplayBridge::GameplayDomain Domain{};
        std::uint32_t Sequence{};
        std::uint64_t Signature{};
        std::uint64_t LedgerRevision{};
        std::uint8_t Channel{};
        bool Valid{};
    };

    // Sequence-bearing work is retained by the monotonic domain ledger after
    // commit. Sequence-zero and post-admission ledger anomalies need an exact
    // semantic identity so that an older payload cannot be admitted again.
    struct SemanticTombstone
    {
        std::uint32_t PlayerId{};
        SkyrimTogetherVR::GameplayBridge::GameplayDomain Domain{};
        std::uint32_t Sequence{};
        std::uint64_t Signature{};
        std::uint8_t Channel{};

        constexpr bool operator==(const SemanticTombstone&) const noexcept = default;
    };

    struct SemanticTombstoneHash
    {
        [[nodiscard]] std::size_t operator()(const SemanticTombstone& acTombstone) const noexcept
        {
            auto hash = static_cast<std::uint64_t>(acTombstone.PlayerId);
            hash = (hash * 0x9e3779b97f4a7c15ull) ^
                   static_cast<std::uint64_t>(acTombstone.Domain);
            hash = (hash * 0x9e3779b97f4a7c15ull) ^ acTombstone.Sequence;
            hash = (hash * 0x9e3779b97f4a7c15ull) ^ acTombstone.Signature;
            hash = (hash * 0x9e3779b97f4a7c15ull) ^ acTombstone.Channel;
            return static_cast<std::size_t>(hash ^ (hash >> 32));
        }
    };

    struct PendingGameplayWork
    {
        std::uint64_t WorkId{};
        std::uint64_t AdmissionOrder{};
        std::uint32_t ServerId{};
        std::uint32_t PlayerId{};
        SkyrimTogetherVR::GameplayBridge::GameplayDomain Domain{};
        std::vector<SkyrimTogetherVR::GameplayBridge::GameplayAction> Actions{};
        std::vector<SkyrimTogetherVR::GameplayBridge::GameplayActionPayload> Payloads{};
        SkyrimTogetherVR::GameplayBridge::ApplyProjectileLaunchPayload Projectile{};
        std::string Text{};
        std::uint64_t TextId{};
        AcceptanceToken Acceptance{};
        double AdmissionAgeElapsed{};
        double RetryWaitElapsed{};
        double ResultWaitElapsed{};
        std::uint8_t Attempts{};
        std::uint16_t NextResultIndex{};
        std::uint32_t HiggsMutationSequence{};
        bool TargetIsPlayer{};
        bool IsProjectile{};
        bool IsText{};
        bool AwaitingResult{};
        bool Admitted{};
        bool AcceptanceCommitted{};
        bool Terminal{};
    };

    struct GameplayResultOwner
    {
        std::uint64_t WorkId{};
        SkyrimTogetherVR::GameplayBridge::BridgeIdentity Identity{};
        SkyrimTogetherVR::GameplayBridge::AdapterHandle TargetHandle{};
        std::uint32_t TargetLocalFormId{};
        SkyrimTogetherVR::GameplayBridge::GameplayDomain Domain{};
        SkyrimTogetherVR::GameplayBridge::GameplayAction Action{};
        std::uint16_t ResultIndex{};
    };

    struct PendingMagicEffect
    {
        NotifyAddTarget Message{};
        AcceptanceToken Acceptance{};
        std::uint8_t Attempts{0};
        double RetryElapsed{0.0};
    };

    struct PendingSpellCast
    {
        NotifySpellCast Message{};
        AcceptanceToken Acceptance{};
        std::uint8_t Attempts{0};
        double RetryElapsed{0.0};
    };

    struct PendingMount
    {
        std::uint32_t MountServerId{};
        AcceptanceToken Acceptance{};
        std::uint64_t CanonicalRevision{};
        std::uint8_t Attempts{};
        double RetryElapsed{};
    };

    struct MountWorkTracking
    {
        std::uint32_t RiderServerId{};
        std::uint64_t CanonicalRevision{};
        std::uint64_t Generation{};
        SkyrimTogetherVR::GameplayBridge::BridgeIdentity EntityIdentity{};
        bool Superseded{};
    };

    struct PendingMountResync
    {
        std::uint64_t KnownRevision{};
        std::uint64_t ApplyingRevision{};
        std::uint64_t ServerInstanceNonce{};
        std::uint64_t ConnectionGeneration{};
        std::uint64_t LifecycleEpoch{};
        double RetryElapsed{};
        double ApplyRetryElapsed{};
        std::uint32_t RequestId{};
        std::uint32_t CanonicalMountId{};
        std::uint8_t Attempts{};
        std::uint8_t ExhaustedRounds{};
        std::uint8_t ApplyAttempts{};
        bool RequestSent{};
        bool Applying{};
        bool NativePending{};
    };

    struct MagicActorReference
    {
        SkyrimTogetherVR::GameplayBridge::AdapterHandle Handle{};
        std::uint32_t LocalReferenceFormId{};
    };

    struct LocalActorActionTransaction
    {
        SkyrimTogetherVR::GameplayBridge::AdapterHandle TargetHandle{};
        SkyrimTogetherVR::GameplayBridge::ActorActionPayload Metadata{};
        SkyrimTogetherVR::AnimationGraphProtocol::SnapshotBuffer Snapshot{};
        std::array<std::array<char, SkyrimTogetherVR::GameplayBridge::kGameplayTextBytesPerChunk>,
                   SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks> TextChunks{};
        std::array<std::uint16_t, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks> TextLengths{};
        std::bitset<SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks> TextReceived{};
        std::uint32_t ActorLocalFormId{};
        std::uint16_t TextChunkCount{};
        std::uint64_t TextId{};
        std::uint64_t Order{};
        bool HasMetadata{false};
    };

    struct PendingRemoteActorAction
    {
        std::uint32_t ServerId{};
        ActionEvent Action{};
        std::uint8_t Attempts{};
    };

    struct RemoteActorActionTracking
    {
        std::uint32_t ServerId{};
        ActionEvent Action{};
        SkyrimTogetherVR::GameplayBridge::BridgeIdentity Identity{};
        SkyrimTogetherVR::GameplayBridge::AdapterHandle TargetHandle{};
        double ResultWaitElapsed{};
        std::uint8_t Attempts{};
    };

    struct CompletedRemoteActorAction
    {
        std::uint32_t ServerId{};
        ActionEvent Action{};
    };

    struct SpawnActionTracking
    {
        std::uint32_t ServerId{0};
        std::uint16_t RemainingResults{0};
        double ResultWaitElapsed{};
        SkyrimTogetherVR::GameplayBridge::BridgeIdentity Identity{};
        SkyrimTogetherVR::GameplayBridge::AdapterHandle TargetHandle{};
        SkyrimTogetherVR::GameplayBridge::GameplayDomain Domain{};
        SkyrimTogetherVR::GameplayBridge::GameplayAction Action{};
    };

    struct PendingEquipmentEntry
    {
        std::uint32_t LocalFormId{};
        std::int32_t Count{};
        std::uint32_t Flags{};
    };

    struct PendingEquipmentApplication
    {
        std::uint64_t TransactionId{};
        std::uint32_t LeftSpell{};
        std::uint32_t RightSpell{};
        std::uint32_t Shout{};
        std::vector<PendingEquipmentEntry> Entries{};
        std::uint64_t ActionId{};
        AcceptanceToken Acceptance{};
        std::uint16_t ExpectedResults{};
        std::uint16_t NextResultIndex{};
        std::uint8_t ResultFailures{};
        double ResultWaitElapsed{};
        bool AwaitingResult{};
        bool AcceptanceCommitted{};
        bool Terminal{};
    };

    struct PendingCanonicalResync
    {
        std::uint32_t RequestId{};
        std::uint64_t KnownRevision{};
        double RetryElapsed{};
        std::uint8_t Attempts{};
        std::uint8_t ExhaustedRounds{};
    };

    struct EquipmentActionTracking
    {
        std::uint32_t ServerId{};
        std::uint64_t TransactionId{};
        SkyrimTogetherVR::GameplayBridge::GameplayAction Action{};
        std::uint16_t ResultIndex{};
        SkyrimTogetherVR::GameplayBridge::BridgeIdentity Identity{};
        SkyrimTogetherVR::GameplayBridge::AdapterHandle TargetHandle{};
    };

    struct PendingInventoryTransaction
    {
        std::uint32_t ServerId{};
        std::uint64_t AdmissionOrder{};
        std::vector<Inventory::Entry> Entries{};
        std::vector<std::uint8_t> Drops{};
        std::vector<SkyrimTogetherVR::GameplayBridge::GameplayAction> ExpectedActions{};
        std::vector<std::uint8_t> ResultStates{};
        SkyrimTogetherVR::GameplayBridge::BridgeIdentity Identity{};
        SkyrimTogetherVR::GameplayBridge::AdapterHandle TargetHandle{};
        std::uint32_t TargetLocalFormId{};
        std::uint64_t FirstActionId{};
        std::uint64_t EndActionId{};
        AcceptanceToken Acceptance{};
        double AdmissionAgeElapsed{};
        double RetryWaitElapsed{};
        double ResultWaitElapsed{};
        bool Reset{};
        bool SpawnInventory{};
        bool HasAcceptance{};
        bool AwaitingResults{};
        bool Admitted{};
        bool AcceptanceCommitted{};
        bool HadFailure{};
        bool Terminal{};
    };

    struct PendingAppearanceApplication
    {
        VRAppearance Appearance{};
        AcceptanceToken Acceptance{};
        std::vector<std::uint64_t> ActionIds{};
        std::uint32_t TransactionSequence{};
        std::uint16_t RemainingResults{};
        std::uint8_t ResultFailures{};
        std::uint8_t SubmissionFailures{};
        double ResultWaitElapsed{};
        double RetryWaitElapsed{};
        bool AwaitingResult{};
        bool HadFailure{};
        bool HadRetryableFailure{};
        bool HadPermanentFailure{};
    };

    struct AppearanceActionTracking
    {
        std::uint64_t TargetKey{};
        std::uint32_t Sequence{};
        std::uint16_t RemainingResults{};
        SkyrimTogetherVR::GameplayBridge::GameplayDomain Domain{};
        SkyrimTogetherVR::GameplayBridge::GameplayAction Action{};
        SkyrimTogetherVR::GameplayBridge::BridgeIdentity Identity{};
        SkyrimTogetherVR::GameplayBridge::AdapterHandle TargetHandle{};
    };

    enum class MagicEffectSubmitResult : std::uint8_t
    {
        Submitted,
        AwaitingActor,
        Rejected,
    };

    enum class SpellCastSubmitResult : std::uint8_t
    {
        Submitted,
        AwaitingActor,
        Rejected,
    };

    void OnCharacterSpawn(const CharacterSpawnRequest& acMessage) noexcept;
    [[nodiscard]] bool TryStageCharacterSpawn(const CharacterSpawnRequest& acMessage) noexcept;
    [[nodiscard]] bool BuildSpawnEntityIdentity(
        std::uint32_t aServerId,
        SkyrimTogetherVR::GameplayBridge::BridgeIdentity& arIdentity) const noexcept;
    void OnReferencesMove(const ServerReferencesMoveRequest& acMessage) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDrawWeapon(const NotifyDrawWeapon& acMessage) noexcept;
    void OnEquipment(const NotifyEquipmentChanges& acMessage) noexcept;
    void OnFactionsChanges(const NotifyFactionsChanges& acMessage) noexcept;
    void OnInventory(const NotifyInventoryChanges& acMessage) noexcept;
    void OnActorValues(const NotifyActorValueChanges& acMessage) noexcept;
    void OnActorMaximums(const NotifyActorMaxValueChanges& acMessage) noexcept;
    void OnActorResync(const NotifyActorResync& acMessage) noexcept;
    void OnHealthChangeBroadcast(const NotifyHealthChangeBroadcast& acMessage) noexcept;
    void OnDeath(const NotifyDeathStateChange& acMessage) noexcept;
    void OnRespawn(const NotifyRespawn& acMessage) noexcept;
    void OnMount(const NotifyMount& acMessage) noexcept;
    void OnMountResync(const NotifyMountResync& acMessage) noexcept;
    void OnProjectile(const NotifyProjectileLaunch& acMessage) noexcept;
    void OnSpawnData(const NotifySpawnData& acMessage) noexcept;
    void OnSpellCast(const NotifySpellCast& acMessage) noexcept;
    void OnInterruptCast(const NotifyInterruptCast& acMessage) noexcept;
    void OnNotifyRemoveSpell(const NotifyRemoveSpell& acMessage) noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept;
    void OnNotifyAddTarget(const NotifyAddTarget& acMessage) noexcept;
    void OnVrEquipment(const NotifyVREquipmentUpdate& acMessage) noexcept;
    void OnVrCombat(const NotifyVRCombatHitEvent& acMessage) noexcept;
    void OnVrMagic(const NotifyVRMagicEffectEvent& acMessage) noexcept;
    void OnVrProjectile(const NotifyVRProjectileEvent& acMessage) noexcept;
    void OnVrPose(const NotifyVRPoseUpdate& acMessage) noexcept;
    void OnVrHiggs(const NotifyVRHiggsState& acMessage) noexcept;
    void OnVrPlanckPhysics(const NotifyVRPlanckPhysicsEvent& acMessage) noexcept;
    void OnVrAppearance(const NotifyVRAppearance& acMessage) noexcept;
    void OnVrGrab(const NotifyVRGrabEvent& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnPlayerLevel(const NotifyPlayerLevel& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void DrainLocalPlanckPhysics() noexcept;
    void RebaseLocalPlanckEvents(std::uint64_t aLifecycleGeneration) noexcept;
    void RetryRemotePlanckPhysics(double aDeltaSeconds) noexcept;
    void RetryRemotePlanckSessionClears(double aDeltaSeconds) noexcept;
    void ClearRemotePlanckSession(std::uint32_t aPlayerId, std::uint64_t aProducerEpoch) noexcept;
    void PurgeRemotePlanckSender(std::uint32_t aPlayerId) noexcept;
    void ClearAllRemotePlanckState() noexcept;
    void OnGameplayResult(const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept;
    void OnLocalGameplay(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept;

    [[nodiscard]] AcceptanceToken PrepareAccept(std::uint32_t aPlayerId,
                                                 SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                                 std::uint32_t aSequence, std::uint64_t aSignature,
                                                 std::uint8_t aChannel = 0) const noexcept;
    [[nodiscard]] bool CanCommitAccept(const AcceptanceToken& acToken) const noexcept;
    [[nodiscard]] bool CommitAccept(const AcceptanceToken& acToken) noexcept;
    [[nodiscard]] bool IsSameAcceptance(const AcceptanceToken& acLeft,
                                        const AcceptanceToken& acRight) const noexcept;
    [[nodiscard]] bool HasSemanticTombstone(const AcceptanceToken& acToken) const noexcept;
    [[nodiscard]] bool RememberSemanticTombstone(const AcceptanceToken& acToken,
                                                  bool aAcceptanceCommitted) noexcept;
    void ForgetSemanticTombstones(std::uint32_t aPlayerId) noexcept;
    [[nodiscard]] HiggsEventLedger* GetHiggsEventLedger(std::uint32_t aPlayerId) noexcept;
    [[nodiscard]] bool EnqueueHiggsMutationTail(std::uint32_t aPlayerId,
                                                 const VRHiggsState& acState) noexcept;
    [[nodiscard]] bool EnqueueHiggsHeldTransform(std::uint32_t aPlayerId,
                                                  const VRHiggsState& acState) noexcept;
    void CancelPendingHiggsObservationWork(std::uint32_t aPlayerId) noexcept;
    void RebaseHiggsProducer(std::uint32_t aPlayerId, std::uint64_t aProducerEpoch) noexcept;
    void TrySubmitNextHiggsMutation(std::uint32_t aPlayerId,
                                    double aResolutionDelta = 0.0) noexcept;
    void CommitHiggsMutationAdmission(const PendingGameplayWork& acWork) noexcept;
    void SkipUnresolvableHiggsMutation(std::uint32_t aPlayerId,
                                       const PendingHiggsMutation& acMutation) noexcept;
    [[nodiscard]] static bool IsHiggsMutationWork(const PendingGameplayWork& acWork) noexcept;
    [[nodiscard]] bool HasPendingHiggsMutationWork(std::uint32_t aPlayerId) const noexcept;
    void RequestSemanticTombstoneRebase() noexcept;
    [[nodiscard]] bool QueueReliableForServer(const AcceptanceToken& acAcceptance, std::uint32_t aServerId,
                                              SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                              SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                              const SkyrimTogetherVR::GameplayBridge::GameplayActionPayload& acPayload,
                                              std::uint64_t* apWorkId = nullptr) noexcept;
    [[nodiscard]] bool QueueReliableForPlayer(const AcceptanceToken& acAcceptance, std::uint32_t aPlayerId,
                                              SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                              SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                              const SkyrimTogetherVR::GameplayBridge::GameplayActionPayload& acPayload) noexcept;
    [[nodiscard]] bool QueueReliableBatchForServer(
        const AcceptanceToken& acAcceptance, std::uint32_t aServerId,
        SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
        std::vector<SkyrimTogetherVR::GameplayBridge::GameplayAction> aActions,
        std::vector<SkyrimTogetherVR::GameplayBridge::GameplayActionPayload> aPayloads) noexcept;
    [[nodiscard]] bool QueueReliableBatchForPlayer(
        const AcceptanceToken& acAcceptance, std::uint32_t aPlayerId,
        SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
        std::vector<SkyrimTogetherVR::GameplayBridge::GameplayAction> aActions,
        std::vector<SkyrimTogetherVR::GameplayBridge::GameplayActionPayload> aPayloads) noexcept;
    [[nodiscard]] bool QueueReliableProjectile(
        const AcceptanceToken& acAcceptance, std::uint32_t aServerId,
        const SkyrimTogetherVR::GameplayBridge::ApplyProjectileLaunchPayload& acPayload) noexcept;
    [[nodiscard]] bool QueueReliableTextForServer(
        const AcceptanceToken& acAcceptance, std::uint32_t aServerId,
        SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
        SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
        std::uint64_t aTextId, std::string_view acText) noexcept;
    [[nodiscard]] bool QueueReliableGameplayWork(PendingGameplayWork&& arWork) noexcept;
    [[nodiscard]] bool TrySubmitReliableGameplayWork(std::size_t aIndex) noexcept;
    [[nodiscard]] bool IsAdmissionHead(const AcceptanceToken& acAcceptance,
                                       std::uint64_t aAdmissionOrder) const noexcept;
    [[nodiscard]] static bool IsVrBodyPoseWork(const PendingGameplayWork& acWork) noexcept;
    [[nodiscard]] bool HasAdmittedVrBodyPoseWork(std::uint32_t aPlayerId,
                                                 std::uint64_t aExceptWorkId = 0) const noexcept;
    [[nodiscard]] bool HasVrBodyPoseWorkCapacity() const noexcept;
    [[nodiscard]] std::size_t GetVrBodyPoseResultOwnerCount() const noexcept;
    void TrySubmitLatestVrBodyPoseWork(std::uint32_t aPlayerId) noexcept;
    void ForgetReliableGameplayWork(std::uint64_t aWorkId) noexcept;
    void RetireReliableGameplayWork(std::uint64_t aWorkId) noexcept;
    void ForgetReliableGameplayWorkForPlayer(std::uint32_t aPlayerId) noexcept;
    void ForgetReliableGameplayWorkForServer(std::uint32_t aServerId) noexcept;
    [[nodiscard]] bool ApplyForPlayer(std::uint32_t aPlayerId, SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                      SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                      const SkyrimTogetherVR::GameplayBridge::GameplayActionPayload& acPayload,
                                      std::uint64_t* apActionId = nullptr) noexcept;
    [[nodiscard]] bool ApplyForServer(std::uint32_t aServerId, SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                      SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                      const SkyrimTogetherVR::GameplayBridge::GameplayActionPayload& acPayload) noexcept;
    [[nodiscard]] bool ApplyForTarget(std::uint32_t aServerId, SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                      SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                      const SkyrimTogetherVR::GameplayBridge::GameplayActionPayload& acPayload) noexcept;
    [[nodiscard]] bool BuildGameplayCommandForServerActor(
        std::uint32_t aServerId, SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
        SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
        SkyrimTogetherVR::GameplayBridge::CommandRecord& arCommand) const noexcept;
    [[nodiscard]] bool ApplyTextForPlayer(std::uint32_t aPlayerId,
                                          SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                          SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                          std::uint64_t aTextId, std::string_view acText,
                                          SkyrimTogetherVR::GameplayBridge::BridgeIdentity* apIdentity = nullptr,
                                          SkyrimTogetherVR::GameplayBridge::AdapterHandle* apTargetHandle = nullptr,
                                          std::uint16_t* apResultCount = nullptr) noexcept;
    [[nodiscard]] bool ApplyTextForServer(std::uint32_t aServerId,
                                          SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                          SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                          std::uint64_t aTextId, std::string_view acText) noexcept;
    [[nodiscard]] std::uint32_t PlayerForServer(std::uint32_t aServerId) const noexcept;
    [[nodiscard]] bool SubmitSpawn(const CharacterSpawnRequest& acMessage) noexcept;
    [[nodiscard]] bool ApplyVRAppearance(std::uint64_t aTargetKey, const VRAppearance& acAppearance) noexcept;
    void QueueVRAppearance(std::uint64_t aTargetKey, const VRAppearance& acAppearance,
                           const AcceptanceToken* apAcceptance = nullptr) noexcept;
    void QueueNpcVRAppearance(std::uint32_t aServerId, const VRAppearance& acAppearance) noexcept;
    void ForgetAppearanceApplication(std::uint64_t aTargetKey) noexcept;
    [[nodiscard]] MagicEffectSubmitResult SubmitMagicEffect(const NotifyAddTarget& acMessage,
                                                             const AcceptanceToken& acAcceptance) noexcept;
    [[nodiscard]] SpellCastSubmitResult SubmitSpellCast(const NotifySpellCast& acMessage,
                                                        const AcceptanceToken& acAcceptance) noexcept;
    [[nodiscard]] bool TryResolveMagicActor(std::uint32_t aServerId, MagicActorReference& arReference) const noexcept;
    [[nodiscard]] bool TryApplyMount(std::uint32_t aRiderServerId, std::uint32_t aMountServerId,
                                     const AcceptanceToken& acAcceptance,
                                     std::uint64_t aCanonicalRevision = 0) noexcept;
    void SupersedeMountWork(std::uint32_t aRiderServerId) noexcept;
    void ResolveMountWork(std::uint64_t aWorkId, bool aNativeSuccess) noexcept;
    void RequestMountSnapshotResync(std::uint32_t aRiderServerId) noexcept;
    [[nodiscard]] bool SendMountResyncRequest(std::uint32_t aRiderServerId,
                                              PendingMountResync& arPending) noexcept;
    void RetryMountResyncs(double aDelta) noexcept;
    [[nodiscard]] bool IsCurrentMountResync(const PendingMountResync& acPending) const noexcept;
    void CompleteMountResync(std::uint32_t aRiderServerId, std::uint64_t aRevision) noexcept;
    void ResetMountCanonicalRecovery() noexcept;
    void RecordMountResyncDiagnostic(const char* apReason) noexcept;
    [[nodiscard]] bool HasExactActorActionCapability() const noexcept;
    [[nodiscard]] bool IsCurrentActorActionRecord(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) const noexcept;
    [[nodiscard]] LocalActorActionTransaction* GetOrCreateLocalActorAction(std::uint64_t aActionId) noexcept;
    [[nodiscard]] bool BuildLocalActorAction(const LocalActorActionTransaction& acTransaction,
                                             ActionEvent& arAction) const noexcept;
    [[nodiscard]] bool TryCommitLocalActorAction(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    [[nodiscard]] bool HasHumanoidActorActionVariables(const ActionEvent& acAction) const noexcept;
    [[nodiscard]] bool SubmitRemoteActorAction(std::uint32_t aServerId, const ActionEvent& acAction,
                                               std::uint8_t aAttempts = 0) noexcept;
    [[nodiscard]] bool SubmitLegacyRemoteActorAction(std::uint32_t aServerId, const ActionEvent& acAction) noexcept;
    [[nodiscard]] bool TrySubmitEquipmentApplication(std::uint32_t aServerId,
                                                     PendingEquipmentApplication& arPending) noexcept;
    void ForgetEquipmentApplication(std::uint32_t aServerId) noexcept;
    [[nodiscard]] bool RetryEquipmentApplication(std::uint32_t aServerId,
                                                  SkyrimTogetherVR::ActorReplicationRecovery::Disposition aDisposition) noexcept;
    void TerminalizeEquipmentApplication(std::uint32_t aServerId) noexcept;
    void RequestActorSnapshotResync(std::uint32_t aServerId) noexcept;
    void RequestEquipmentSnapshotResync(std::uint32_t aServerId) noexcept;
    [[nodiscard]] bool SendCanonicalResyncRequest(
        std::uint32_t aServerId, std::uint8_t aScope,
        PendingCanonicalResync& arPending) noexcept;
    [[nodiscard]] bool HasInventoryTransactionCapability() const noexcept;
    [[nodiscard]] bool QueueInventoryTransaction(std::uint32_t aServerId,
                                                  const std::vector<Inventory::Entry>& acEntries,
                                                  const std::vector<std::uint8_t>& acDrops,
                                                  bool aReset, bool aSpawnInventory,
                                                  const AcceptanceToken* apAcceptance = nullptr) noexcept;
    [[nodiscard]] bool TrySubmitInventoryTransaction(std::size_t aIndex) noexcept;
    void CompleteInventoryTransaction(std::size_t aIndex, bool aSucceeded) noexcept;
    void TerminalizeInventoryTransaction(std::size_t aIndex) noexcept;
    void ForgetInventoryTransactions(std::uint32_t aServerId) noexcept;
    [[nodiscard]] bool HasPendingSpawnInventoryTransaction(std::uint32_t aServerId) const noexcept;
    [[nodiscard]] std::uint64_t NextRemoteActorActionId() noexcept;
    [[nodiscard]] std::uint32_t NextAppearanceTransactionSequence() noexcept;
    void QueueRemoteActorAction(std::uint32_t aServerId, const ActionEvent& acAction,
                                std::uint8_t aAttempts = 0) noexcept;
    [[nodiscard]] bool IsKnownRemoteActorAction(std::uint32_t aServerId,
                                                const ActionEvent& acAction) const noexcept;
    void RememberCompletedRemoteActorAction(std::uint32_t aServerId,
                                            const ActionEvent& acAction) noexcept;
    void ForgetRemoteActorActions(std::uint32_t aServerId) noexcept;
    [[nodiscard]] bool ScheduleSpawnRecovery(
        std::uint32_t aServerId,
        SkyrimTogetherVR::ActorReplicationRecovery::Disposition aDisposition) noexcept;
    void TerminalizeSpawn(std::uint32_t aServerId) noexcept;
    void ForgetSpawnActionIds(std::uint32_t aServerId) noexcept;
    [[nodiscard]] bool HasSpawnActionIds(std::uint32_t aServerId) const noexcept;
    void ForgetPlayer(std::uint32_t aPlayerId) noexcept;
    void ForgetServer(std::uint32_t aServerId) noexcept;

    World& m_world;
    TransportService& m_transport;
    VRAvatarService& m_avatars;
    VRNpcOwnershipService& m_npcOwnership;
    std::unordered_map<std::uint32_t, std::uint32_t> m_serverPlayers{};
    std::unordered_map<std::uint32_t, CharacterSpawnRequest> m_pendingSpawns{};
    std::unordered_map<std::uint32_t, CharacterSpawnRequest> m_spawnSnapshots{};
    std::unordered_map<std::uint32_t, SkyrimTogetherVR::GameplayBridge::BridgeIdentity>
        m_spawnEntityIdentities{};
    std::unordered_map<std::uint64_t, VRAppearance> m_latestAppearances{};
    std::unordered_map<std::uint64_t, std::uint32_t> m_appliedAppearanceSequences{};
    std::unordered_map<std::uint64_t, std::uint32_t> m_failedAppearanceSequences{};
    std::unordered_map<std::uint64_t, PendingAppearanceApplication> m_pendingAppearanceApplications{};
    std::unordered_map<std::uint64_t, AppearanceActionTracking> m_appearanceActionOwners{};
    std::unordered_map<std::uint32_t, PendingMount> m_pendingMounts{};
    std::unordered_map<std::uint32_t, PendingMountResync> m_pendingMountResyncs{};
    std::unordered_map<std::uint32_t, std::uint64_t> m_lastMountSnapshotRevisionByRider{};
    std::unordered_map<std::uint64_t, MountWorkTracking> m_mountWork{};
    std::unordered_map<std::uint32_t, std::uint64_t> m_mountWorkGenerationByRider{};
    std::unordered_map<std::uint32_t, SkyrimTogetherVR::ActorReplicationRecovery::SpawnRecoveryState>
        m_resyncAttempts{};
    std::unordered_map<std::uint32_t, DomainLedgers> m_ledgers{};
    std::unordered_map<std::uint32_t, HiggsEventLedger> m_higgsEventLedgers{};
    struct PlanckEventLedger
    {
        std::uint64_t ServerInstanceNonce{0};
        std::uint64_t ConnectionGeneration{0};
        std::uint64_t ProducerEpoch{0};
        std::uint64_t LastEventId{0};
        bool HasEpoch{false};
        bool HasEvent{false};
    };

    struct PendingRemotePlanckEvent
    {
        VRPlanckPhysicsEvent Event{};
        std::uint64_t ServerInstanceNonce{0};
        std::uint64_t ConnectionGeneration{0};
        double RetryElapsedSeconds{0.0};
        double TotalElapsedSeconds{0.0};
        std::uint8_t Attempts{0};
    };

    struct PlanckRemoteSessionKey
    {
        std::uint64_t ServerInstanceNonce{0};
        std::uint64_t ConnectionGeneration{0};
        std::uint32_t PlayerId{0};
        std::uint64_t ProducerEpoch{0};

        [[nodiscard]] bool operator==(const PlanckRemoteSessionKey& acRhs) const noexcept
        {
            return ServerInstanceNonce == acRhs.ServerInstanceNonce &&
                   ConnectionGeneration == acRhs.ConnectionGeneration &&
                   PlayerId == acRhs.PlayerId && ProducerEpoch == acRhs.ProducerEpoch;
        }
    };

    struct PlanckRemoteSessionKeyHash
    {
        [[nodiscard]] std::size_t operator()(const PlanckRemoteSessionKey& acKey) const noexcept;
    };

    struct PendingRemotePlanckClear
    {
        PlanckRemoteSessionKey Key{};
        std::uint64_t Token{0};
        std::uint64_t EventId{0};
        double RetryElapsedSeconds{0.0};
    };

    struct PendingLocalPlanckEvent
    {
        VRPlanckPhysicsEvent Event{};
        std::uint64_t LifecycleGeneration{0};
    };

    enum class PlanckRemoteSubmitResult : std::uint8_t
    {
        Applied,
        Retry,
        Terminal,
    };

    [[nodiscard]] PlanckRemoteSubmitResult SubmitRemotePlanckEvent(
        std::uint32_t aPlayerId, const VRPlanckPhysicsEvent& acEvent) noexcept;
    void CommitRemotePlanckEvent(std::uint32_t aPlayerId,
                                 const VRPlanckPhysicsEvent& acEvent) noexcept;
    void EnqueueRemotePlanckRetry(std::uint32_t aPlayerId,
                                  const VRPlanckPhysicsEvent& acEvent) noexcept;
    [[nodiscard]] bool MakePlanckRemoteSessionKey(std::uint32_t aPlayerId,
                                                   std::uint64_t aProducerEpoch,
                                                   PlanckRemoteSessionKey& arKey) const noexcept;
    [[nodiscard]] std::uint64_t GetOrAllocatePlanckRemoteSessionToken(
        const PlanckRemoteSessionKey& acKey) noexcept;
    void RequestRemotePlanckClear(const PlanckRemoteSessionKey& acKey) noexcept;
    [[nodiscard]] bool HasPendingRemotePlanckClear(const PlanckRemoteSessionKey& acKey) const noexcept;
    [[nodiscard]] bool HasPendingRemotePlanckReplacementClear(const PlanckRemoteSessionKey& acKey) const noexcept;
    void EnterPlanckAdmissionFailClosed() noexcept;
    void PruneCompletedPlanckCancellationKeys() noexcept;
    void RefreshPlanckWireProducerIdentity(std::uint64_t aServerInstanceNonce,
                                           std::uint64_t aConnectionGeneration,
                                           std::uint64_t aLifecycleGeneration) noexcept;

    std::unordered_map<std::uint32_t, PlanckEventLedger> m_planckEventLedgers{};
    std::deque<PendingLocalPlanckEvent> m_pendingLocalPlanckEvents{};
    std::unordered_map<std::uint32_t, std::deque<PendingRemotePlanckEvent>> m_pendingRemotePlanckEvents{};
    std::unordered_map<PlanckRemoteSessionKey, std::uint64_t, PlanckRemoteSessionKeyHash>
        m_planckRemoteSessionTokens{};
    std::unordered_set<PlanckRemoteSessionKey, PlanckRemoteSessionKeyHash> m_cancelledPlanckRemoteSessions{};
    std::deque<PendingRemotePlanckClear> m_pendingRemotePlanckClears{};
    std::unordered_map<std::uint32_t, std::deque<PendingHiggsMutation>> m_pendingHiggsMutations{};
    std::unordered_set<SemanticTombstone, SemanticTombstoneHash> m_semanticTombstones{};
    std::vector<PendingGameplayWork> m_pendingGameplayWork{};
    std::unordered_map<std::uint64_t, GameplayResultOwner> m_gameplayResultOwners{};
    std::unordered_map<std::uint32_t, std::uint64_t> m_lastEquipmentTransactionByServer{};
    std::unordered_map<std::uint32_t, std::uint64_t> m_lastActorSnapshotRevisionByServer{};
    std::unordered_map<std::uint32_t, PendingCanonicalResync> m_pendingActorSnapshotResyncs{};
    std::unordered_map<std::uint32_t, PendingCanonicalResync> m_pendingEquipmentSnapshotResyncs{};
    std::unordered_map<std::uint32_t, PendingEquipmentApplication> m_pendingEquipmentApplications{};
    std::unordered_map<std::uint64_t, EquipmentActionTracking> m_equipmentActionOwners{};
    std::vector<PendingInventoryTransaction> m_pendingInventoryTransactions{};
    std::unordered_set<std::uint32_t> m_completedSpawnInventoryTransactions{};
    std::unordered_set<std::uint32_t> m_failedSpawnInventoryTransactions{};
    std::unordered_set<std::uint32_t> m_quarantinedSpawns{};
    std::vector<PendingMagicEffect> m_pendingMagicEffects{};
    std::vector<PendingSpellCast> m_pendingSpellCasts{};
    std::unordered_map<std::uint64_t, LocalActorActionTransaction> m_localActorActions{};
    std::vector<PendingRemoteActorAction> m_pendingRemoteActorActions{};
    std::unordered_map<std::uint64_t, RemoteActorActionTracking> m_remoteActorActionOwners{};
    std::vector<CompletedRemoteActorAction> m_completedRemoteActorActions{};
    std::unordered_map<std::uint64_t, SpawnActionTracking> m_spawnActionOwners{};
    std::uint32_t m_recordingSpawnServerId{0};
    std::uint32_t m_localServerId{0};
    std::uint64_t m_observedLifecycleEpoch{0};
    std::uint64_t m_nextLocalActorActionOrder{1};
    std::uint64_t m_nextRemoteActorActionId{1};
    std::uint32_t m_nextAppearanceTransactionSequence{1};
    std::uint64_t m_nextGameplayWorkId{1};
    std::uint64_t m_nextAdmissionOrder{1};
    std::uint64_t m_nextPlanckClearEventId{1};
    std::uint64_t m_nextPlanckRemoteSessionToken{1};
    std::uint64_t m_nextPlanckWireProducerEpoch{1};
    std::uint64_t m_planckLocalLifecycleGeneration{0};
    std::uint64_t m_planckWireProducerEpoch{0};
    std::uint64_t m_planckWireProducerServerNonce{0};
    std::uint64_t m_planckWireProducerConnectionGeneration{0};
    std::uint64_t m_planckLocalQueueOverflowCount{0};
    std::uint64_t m_planckLocalRejectedCount{0};
    std::uint64_t m_planckRemoteRejectedCount{0};
    std::uint64_t m_planckRemoteUnavailableCount{0};
    std::uint64_t m_planckRemoteDeferredCount{0};
    std::uint64_t m_planckRemoteRetryOverflowCount{0};
    std::uint64_t m_planckRemoteRetryExpiredCount{0};
    std::uint64_t m_planckRemoteOrderQuarantineCount{0};
    std::uint64_t m_planckCancellationCapacityCount{0};
    bool m_planckAdmissionFailClosed{false};
    std::uint32_t m_nextCanonicalResyncRequestId{1};
    std::uint32_t m_nextMountResyncRequestId{1};
    std::uint32_t m_mountResyncDiagnosticCount{};
    std::uint64_t m_observedServerInstanceNonce{};
    std::uint64_t m_observedConnectionGeneration{};
    std::uint64_t m_semanticTombstoneRebaseEpoch{0};
    double m_semanticTombstoneRebaseElapsed{0.0};
    bool m_replayAfterLifecycleBoundary{false};
    bool m_semanticTombstoneRebaseRequested{false};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_characterSpawnConnection;
    entt::scoped_connection m_referencesMoveConnection;
    entt::scoped_connection m_drawWeaponConnection;
    entt::scoped_connection m_equipmentConnection;
    entt::scoped_connection m_factionsConnection;
    entt::scoped_connection m_inventoryConnection;
    entt::scoped_connection m_actorValuesConnection;
    entt::scoped_connection m_actorMaximumsConnection;
    entt::scoped_connection m_actorResyncConnection;
    entt::scoped_connection m_healthChangeConnection;
    entt::scoped_connection m_deathConnection;
    entt::scoped_connection m_respawnConnection;
    entt::scoped_connection m_mountConnection;
    entt::scoped_connection m_mountResyncConnection;
    entt::scoped_connection m_projectileConnection;
    entt::scoped_connection m_spawnDataConnection;
    entt::scoped_connection m_spellCastConnection;
    entt::scoped_connection m_interruptCastConnection;
    entt::scoped_connection m_removeSpellConnection;
    entt::scoped_connection m_removeCharacterConnection;
    entt::scoped_connection m_addTargetConnection;
    entt::scoped_connection m_vrEquipmentConnection;
    entt::scoped_connection m_vrCombatConnection;
    entt::scoped_connection m_vrMagicConnection;
    entt::scoped_connection m_vrProjectileConnection;
    entt::scoped_connection m_vrPoseConnection;
    entt::scoped_connection m_vrHiggsConnection;
    entt::scoped_connection m_vrPlanckPhysicsConnection;
    entt::scoped_connection m_vrAppearanceConnection;
    entt::scoped_connection m_vrGrabConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_playerLevelConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_gameplayResultConnection;
    entt::scoped_connection m_localGameplayConnection;
};
