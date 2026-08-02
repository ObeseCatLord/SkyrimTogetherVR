#pragma once

#include <array>
#include <bitset>
#include <cstdint>
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

struct DisconnectedEvent;
struct NotifyActorMaxValueChanges;
struct NotifyActorValueChanges;
struct NotifyDeathStateChange;
struct NotifyHealthChangeBroadcast;
struct NotifyDrawWeapon;
struct NotifyEquipmentChanges;
struct NotifyFactionsChanges;
struct NotifyInventoryChanges;
struct NotifyMount;
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
        std::uint32_t ServerId{};
        std::uint32_t PlayerId{};
        SkyrimTogetherVR::GameplayBridge::GameplayDomain Domain{};
        std::vector<SkyrimTogetherVR::GameplayBridge::GameplayAction> Actions{};
        std::vector<SkyrimTogetherVR::GameplayBridge::GameplayActionPayload> Payloads{};
        SkyrimTogetherVR::GameplayBridge::ApplyProjectileLaunchPayload Projectile{};
        std::string Text{};
        std::uint64_t TextId{};
        AcceptanceToken Acceptance{};
        double AdmissionWaitElapsed{};
        double ResultWaitElapsed{};
        std::uint8_t Attempts{};
        std::uint16_t NextResultIndex{};
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
        std::uint8_t Attempts{};
        double RetryElapsed{};
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
        std::uint16_t RemainingResults{};
        std::uint8_t ResultFailures{};
        std::uint8_t SubmissionFailures{};
        double ResultWaitElapsed{};
        double RetryWaitElapsed{};
        bool AwaitingResult{};
        bool HadFailure{};
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
    void OnReferencesMove(const ServerReferencesMoveRequest& acMessage) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDrawWeapon(const NotifyDrawWeapon& acMessage) noexcept;
    void OnEquipment(const NotifyEquipmentChanges& acMessage) noexcept;
    void OnFactionsChanges(const NotifyFactionsChanges& acMessage) noexcept;
    void OnInventory(const NotifyInventoryChanges& acMessage) noexcept;
    void OnActorValues(const NotifyActorValueChanges& acMessage) noexcept;
    void OnActorMaximums(const NotifyActorMaxValueChanges& acMessage) noexcept;
    void OnHealthChangeBroadcast(const NotifyHealthChangeBroadcast& acMessage) noexcept;
    void OnDeath(const NotifyDeathStateChange& acMessage) noexcept;
    void OnRespawn(const NotifyRespawn& acMessage) noexcept;
    void OnMount(const NotifyMount& acMessage) noexcept;
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
    void OnVrAppearance(const NotifyVRAppearance& acMessage) noexcept;
    void OnVrGrab(const NotifyVRGrabEvent& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnPlayerLevel(const NotifyPlayerLevel& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
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
    void RequestSemanticTombstoneRebase() noexcept;
    [[nodiscard]] bool QueueReliableForServer(const AcceptanceToken& acAcceptance, std::uint32_t aServerId,
                                              SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                              SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                              const SkyrimTogetherVR::GameplayBridge::GameplayActionPayload& acPayload) noexcept;
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
                                     const AcceptanceToken& acAcceptance) noexcept;
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
    void TerminalizeEquipmentApplication(std::uint32_t aServerId) noexcept;
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
    void QueueRemoteActorAction(std::uint32_t aServerId, const ActionEvent& acAction,
                                std::uint8_t aAttempts = 0) noexcept;
    [[nodiscard]] bool IsKnownRemoteActorAction(std::uint32_t aServerId,
                                                const ActionEvent& acAction) const noexcept;
    void RememberCompletedRemoteActorAction(std::uint32_t aServerId,
                                            const ActionEvent& acAction) noexcept;
    void ForgetRemoteActorActions(std::uint32_t aServerId) noexcept;
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
    std::unordered_map<std::uint64_t, VRAppearance> m_latestAppearances{};
    std::unordered_map<std::uint64_t, std::uint32_t> m_appliedAppearanceSequences{};
    std::unordered_map<std::uint64_t, std::uint32_t> m_failedAppearanceSequences{};
    std::unordered_map<std::uint64_t, PendingAppearanceApplication> m_pendingAppearanceApplications{};
    std::unordered_map<std::uint64_t, AppearanceActionTracking> m_appearanceActionOwners{};
    std::unordered_map<std::uint32_t, PendingMount> m_pendingMounts{};
    std::unordered_map<std::uint32_t, std::uint8_t> m_resyncAttempts{};
    std::unordered_map<std::uint32_t, DomainLedgers> m_ledgers{};
    std::unordered_set<SemanticTombstone, SemanticTombstoneHash> m_semanticTombstones{};
    std::vector<PendingGameplayWork> m_pendingGameplayWork{};
    std::unordered_map<std::uint64_t, GameplayResultOwner> m_gameplayResultOwners{};
    std::unordered_map<std::uint32_t, std::uint64_t> m_lastEquipmentTransactionByServer{};
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
    std::uint64_t m_nextGameplayWorkId{1};
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
    entt::scoped_connection m_healthChangeConnection;
    entt::scoped_connection m_deathConnection;
    entt::scoped_connection m_respawnConnection;
    entt::scoped_connection m_mountConnection;
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
    entt::scoped_connection m_vrAppearanceConnection;
    entt::scoped_connection m_vrGrabConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_playerLevelConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_gameplayResultConnection;
    entt::scoped_connection m_localGameplayConnection;
};
