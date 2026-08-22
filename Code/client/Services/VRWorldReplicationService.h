#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <Structs/Inventory.h>
#include <vr_common/VRGameplayBridge.h>

struct ConnectedEvent;
struct DisconnectedEvent;
struct AssignObjectsResponse;
struct NotifyActivate;
struct NotifyActorTeleport;
struct NotifyChatMessageBroadcast;
struct NotifyDialogue;
struct NotifyLockChange;
struct NotifyNewPackage;
struct NotifyObjectInventoryChanges;
struct NotifyObjectResync;
struct NotifyPlayerDialogue;
struct NotifyPartyInfo;
struct NotifyQuestUpdate;
struct NotifyQuestResync;
struct NotifyRemoveWaypoint;
struct NotifyScriptAnimation;
struct NotifySetWaypoint;
struct NotifySubtitle;
struct NotifyTeleport;
struct NotifyWeatherChange;
struct PartyJoinedEvent;
struct PartyLeftEvent;
struct PlayerDialogueEvent;
struct ServerSettings;
struct ServerTimeSettings;
struct TeleportCommandResponse;
struct TransportService;
struct VRAvatarService;
struct World;
struct UpdateEvent;
namespace SkyrimTogetherVR
{
struct LocalGameplayBridgeEvent;
struct RemoteGameplayBridgeResultEvent;
}

struct VRWorldReplicationService
{
    VRWorldReplicationService(World& aWorld, entt::dispatcher& aDispatcher,
                              TransportService& aTransport, VRAvatarService& aAvatars) noexcept;
    ~VRWorldReplicationService() noexcept = default;

    TP_NOCOPYMOVE(VRWorldReplicationService);

private:
    static constexpr std::size_t kMaximumPendingRemoteCommands = 128;
    static constexpr std::size_t kMaximumPendingWorldInventoryTransactions = 128;
    static constexpr std::size_t kMaximumPendingCanonicalResyncs = 64;

    enum class CanonicalRecoveryOperation : std::uint8_t
    {
        None,
        Activation,
        ActivationPostState,
        ObjectLock,
        ObjectOpenState,
        Quest,
    };

    struct RetainedState
    {
        std::uint64_t ServerInstanceNonce{0};
        std::uint64_t ConnectionGeneration{0};
        std::uint64_t Version{0};
        std::uint64_t SubmittedVersion{0};
        std::uint64_t InFlightActionId{0};
        std::uint32_t TargetLocalFormId{0};
        std::uint32_t LocalFormIdA{0};
        std::int32_t ValueA{0};
        std::int32_t ValueB{0};
        float ScalarA{0.0F};
        float ScalarB{0.0F};
        float ScalarC{0.0F};
        std::uint32_t ActionFlags{0};
        std::uint8_t Attempts{0};
        bool Valid{false};
        bool Dirty{false};
        bool InFlight{false};
    };

    struct PendingRemoteCommand
    {
        SkyrimTogetherVR::GameplayBridge::CommandRecord Command{};
        std::uint64_t CanonicalRevision{};
        std::uint8_t AuthoritativeOpenState{};
        std::uint32_t CanonicalServerId{};
        std::uint32_t CanonicalOwnerPlayerId{};
        GameId CanonicalQuestId{};
        CanonicalRecoveryOperation RecoveryOperation{CanonicalRecoveryOperation::None};
        double RetryDelay{0.0};
        double LifetimeRemaining{0.0};
        double ResultRemaining{0.0};
        std::uint8_t Attempts{0};
        bool AwaitingResult{false};
        bool Occupied{false};
    };

    struct PendingWorldInventoryTransaction
    {
        GameId TargetId{};
        std::vector<Inventory::Entry> Entries{};
        std::uint64_t ServerInstanceNonce{};
        std::uint64_t ConnectionGeneration{};
        std::uint64_t LifecycleEpoch{};
        std::uint64_t FirstActionId{};
        std::uint64_t EndActionId{};
        std::uint64_t NextResultActionId{};
        std::uint32_t TargetLocalFormId{};
        std::uint32_t CanonicalServerId{};
        std::uint64_t CanonicalRevision{};
        double RetryDelay{};
        double ResultRemaining{};
        std::uint8_t Attempts{};
        bool Reset{};
        bool AwaitingResult{};
        bool Terminal{};
        bool RecoveryCompletionReported{};
    };

    struct PendingCanonicalResync
    {
        std::uint64_t KnownRevision{};
        std::uint64_t ApplyingRevision{};
        double RetryElapsed{};
        std::uint32_t RequestId{};
        std::uint8_t Attempts{};
        std::uint8_t PendingApplyMask{};
        bool Applying{};
        bool RequestSent{};
    };

    struct QuestRecoveryEntry
    {
        GameId Id{};
        std::uint16_t Stage{};
        bool Stop{};
    };

    struct PendingQuestResync : PendingCanonicalResync
    {
        std::uint64_t ServerInstanceNonce{};
        std::uint64_t ConnectionGeneration{};
        std::uint64_t LifecycleEpoch{};
        std::uint32_t OwnerPlayerId{};
        std::vector<QuestRecoveryEntry> Entries{};
        std::unordered_map<GameId, std::uint16_t> SnapshotStages{};
        std::size_t NextEntry{};
    };

    void OnActivate(const NotifyActivate& acMessage) noexcept;
    void OnAssignObjects(const AssignObjectsResponse& acMessage) noexcept;
    void OnActorTeleport(const NotifyActorTeleport& acMessage) noexcept;
    void OnChatMessage(const NotifyChatMessageBroadcast& acMessage) noexcept;
    void OnDialogue(const NotifyDialogue& acMessage) noexcept;
    void OnLockChange(const NotifyLockChange& acMessage) noexcept;
    void OnNewPackage(const NotifyNewPackage& acMessage) noexcept;
    void OnObjectInventory(const NotifyObjectInventoryChanges& acMessage) noexcept;
    void OnPlayerDialogue(const NotifyPlayerDialogue& acMessage) noexcept;
    void OnQuestUpdate(const NotifyQuestUpdate& acMessage) noexcept;
    void OnObjectResync(const NotifyObjectResync& acMessage) noexcept;
    void OnQuestResync(const NotifyQuestResync& acMessage) noexcept;
    void OnRemoveWaypoint(const NotifyRemoveWaypoint& acMessage) noexcept;
    void OnScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept;
    void OnSetWaypoint(const NotifySetWaypoint& acMessage) noexcept;
    void OnSubtitle(const NotifySubtitle& acMessage) noexcept;
    void OnTeleport(const NotifyTeleport& acMessage) noexcept;
    void OnTeleportCommand(const TeleportCommandResponse& acMessage) noexcept;
    void OnTimeSettings(const ServerTimeSettings& acMessage) noexcept;
    void OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept;
    void OnServerSettings(const ServerSettings& acSettings) noexcept;
    void OnPartyJoined(const PartyJoinedEvent& acEvent) noexcept;
    void OnPartyLeft(const PartyLeftEvent& acEvent) noexcept;
    void OnPartyInfo(const NotifyPartyInfo& acMessage) noexcept;
    void OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) noexcept;
    void OnLocalGameplay(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept;
    void OnLocalGameplayText(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    void OnLocalSubtitleText(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnGameplayResult(const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept;

    [[nodiscard]] bool CaptureSession(RetainedState& arState) const noexcept;
    void RetainState(RetainedState& arState, std::uint32_t aTargetLocalFormId, std::uint32_t aLocalFormIdA,
                     std::int32_t aValueA, std::int32_t aValueB, float aScalarA, float aScalarB,
                     float aScalarC, std::uint32_t aActionFlags = 0) noexcept;
    void SubmitReleaseWeather() noexcept;
    void RetainServerSettings(const ServerSettings& acSettings) noexcept;
    void ObserveSession() noexcept;
    void Reconcile() noexcept;
    void ReconcileState(RetainedState& arState, std::uint16_t aDomain, std::uint16_t aAction) noexcept;
    void ResetRetainedState() noexcept;
    void DiscardRetainedStateForSession(std::uint64_t aServerInstanceNonce,
                                        std::uint64_t aConnectionGeneration) noexcept;
    void ResetInFlightState() noexcept;
    void ResetSubtitleTextState() noexcept;
    bool SubmitRemoteCommand(
        SkyrimTogetherVR::GameplayBridge::CommandRecord aCommand,
        CanonicalRecoveryOperation aRecoveryOperation = CanonicalRecoveryOperation::None,
        std::uint32_t aCanonicalServerId = 0, std::uint64_t aCanonicalRevision = 0,
        std::uint8_t aAuthoritativeOpenState = 0, std::uint32_t aCanonicalOwnerPlayerId = 0,
        const GameId& acCanonicalQuestId = {}) noexcept;
    void RetryPendingRemoteCommands(double aDelta) noexcept;
    void TrySubmitPendingRemoteCommand(PendingRemoteCommand& arPending) noexcept;
    [[nodiscard]] bool HandlePendingRemoteCommandResult(
        const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    [[nodiscard]] bool IsPendingRemoteCommandCurrent(const PendingRemoteCommand& acPending) const noexcept;
    [[nodiscard]] bool QueueWorldInventoryTransaction(const GameId& acTargetId, const Inventory& acInventory,
                                                       bool aReset, std::uint32_t aCanonicalServerId = 0,
                                                       std::uint64_t aCanonicalRevision = 0) noexcept;
    [[nodiscard]] bool BuildWorldInventoryTransactionCommands(
        const PendingWorldInventoryTransaction& acPending,
        std::vector<SkyrimTogetherVR::GameplayBridge::CommandRecord>& arCommands) const noexcept;
    void TrySubmitPendingWorldInventoryTransaction(PendingWorldInventoryTransaction& arPending) noexcept;
    void RetryPendingWorldInventoryTransactions(double aDelta) noexcept;
    void HandlePendingWorldInventoryTransactionResult(
        const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    [[nodiscard]] bool IsPendingWorldInventoryTransactionCurrent(
        const PendingWorldInventoryTransaction& acPending) const noexcept;
    void ClearPendingWorldInventoryTransactions() noexcept;
    [[nodiscard]] std::uint32_t FindObjectServerId(const GameId& acObjectId) const noexcept;
    void RequestObjectResync(std::uint32_t aServerId) noexcept;
    void RequestQuestResync(std::uint32_t aOwnerPlayerId) noexcept;
    void RetryCanonicalResyncs(double aDelta) noexcept;
    [[nodiscard]] bool SendObjectResyncRequest(std::uint32_t aServerId,
                                               PendingCanonicalResync& arPending) noexcept;
    [[nodiscard]] bool SendQuestResyncRequest(PendingQuestResync& arPending) noexcept;
    void CompleteObjectRecoveryInventory(std::uint32_t aServerId, std::uint64_t aRevision,
                                         bool aSucceeded) noexcept;
    void CompleteCanonicalRecoveryCommand(const PendingRemoteCommand& acPending,
                                          bool aSucceeded) noexcept;
    void TryApplyQuestRecovery() noexcept;
    void FailObjectRecovery(std::uint32_t aServerId, std::uint64_t aRevision) noexcept;
    void FailQuestRecovery(std::uint32_t aOwnerPlayerId, std::uint64_t aRevision) noexcept;
    [[nodiscard]] bool IsQuestRecoveryCurrent(const PendingQuestResync& acPending) const noexcept;
    void ResetCanonicalRecovery() noexcept;
    void RecordCanonicalRecoveryDiagnostic(const char* apReason) noexcept;
    void SubmitText(SkyrimTogetherVR::GameplayBridge::CommandRecord aBase,
                    std::uint64_t aTextId, std::string_view acText) noexcept;
    void RetryPendingText() noexcept;
    template <class T>
    [[nodiscard]] bool SendOutbound(T&& aRequest, std::size_t aDomainIndex,
                                    std::uint64_t aActionId) noexcept;
    void TrySendPendingOutbound() noexcept;

    World& m_world;
    TransportService& m_transport;
    VRAvatarService& m_avatars;
    std::uint64_t m_nextTextId{1};
    std::uint64_t m_observedServerInstanceNonce{0};
    std::uint64_t m_observedConnectionGeneration{0};
    std::uint64_t m_observedLifecycleEpoch{0};
    double m_reconcileTimer{0.0};
    double m_textRetryTimer{0.0};
    std::array<PendingRemoteCommand, kMaximumPendingRemoteCommands> m_pendingRemoteCommands{};
    std::unordered_map<GameId, std::deque<PendingWorldInventoryTransaction>> m_pendingWorldInventoryTransactions{};
    std::size_t m_pendingWorldInventoryTransactionCount{};
    std::unordered_map<GameId, std::uint32_t> m_objectServerIds{};
    std::unordered_map<std::uint32_t, std::uint64_t> m_lastObjectSnapshotRevisionByServer{};
    std::unordered_map<std::uint32_t, PendingCanonicalResync> m_pendingObjectResyncs{};
    std::unordered_map<std::uint32_t, PendingQuestResync> m_pendingQuestResyncs{};
    std::unordered_map<std::uint32_t, std::uint64_t> m_lastQuestSnapshotRevisionByOwner{};
    std::unordered_map<std::uint32_t, std::uint64_t> m_latestQuestRevisionByOwner{};
    std::unordered_map<std::uint32_t, std::unordered_map<GameId, std::uint16_t>> m_canonicalQuestStagesByOwner{};
    std::uint32_t m_nextCanonicalResyncRequestId{1};
    std::uint32_t m_canonicalRecoveryDiagnosticCount{};
    RetainedState m_calendarState{};
    RetainedState m_weatherState{};
    RetainedState m_settingsState{};
    RetainedState m_deathSystemState{};
    struct WaypointEchoSuppression
    {
        std::uint32_t LocalWorldspaceFormId{0};
        float PositionX{0.0F};
        float PositionY{0.0F};
        float PositionZ{0.0F};
        double Remaining{0.0};
        bool Remove{false};
        bool Valid{false};
    };
    WaypointEchoSuppression m_waypointEcho{};
    struct DialogueTextAssembly
    {
        std::uint64_t ActionId{0};
        std::uint64_t TextId{0};
        std::uint32_t TargetLocalFormId{0};
        std::uint16_t ChunkCount{0};
        std::uint16_t ReceivedMask{0};
        std::array<std::uint16_t, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks> Lengths{};
        std::array<char, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks *
                             SkyrimTogetherVR::GameplayBridge::kGameplayTextBytesPerChunk> Bytes{};
    };
    DialogueTextAssembly m_dialogueText{};
    struct SubtitleTextAssembly
    {
        std::uint64_t ServerInstanceNonce{0};
        std::uint64_t ConnectionGeneration{0};
        std::uint64_t LifecycleEpoch{0};
        std::uint64_t ActionId{0};
        std::uint64_t TextId{0};
        std::uint32_t SpeakerLocalFormId{0};
        std::uint32_t TopicLocalFormId{0};
        std::uint16_t ChunkCount{0};
        std::uint16_t ReceivedCount{0};
        std::uint32_t ReceivedMask{0};
        std::array<std::uint16_t, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks> Lengths{};
        std::array<char, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks *
                             SkyrimTogetherVR::GameplayBridge::kGameplayTextBytesPerChunk> Bytes{};
        double Remaining{0.0};
        bool Valid{false};
    };
    SubtitleTextAssembly m_subtitleText{};
    std::uint64_t m_lastSubtitleActionId{0};
    struct PendingTextTransaction
    {
        SkyrimTogetherVR::GameplayBridge::CommandRecord Base{};
        std::uint64_t TextId{0};
        std::string Text{};
        std::uint8_t Attempts{0};
    };
    std::deque<PendingTextTransaction> m_pendingText{};
    struct PendingOutbound
    {
        std::function<bool()> TrySend{};
    };
    std::deque<PendingOutbound> m_pendingOutbound{};
    std::uint64_t m_pendingOutboundServerInstanceNonce{};
    std::uint64_t m_pendingOutboundConnectionGeneration{};
    std::uint64_t m_pendingOutboundLifecycleEpoch{};
    bool m_partyRoleKnown{false};
    bool m_partyLeader{false};
    std::array<std::uint64_t, 18> m_lastLocalActionIdByDomain{};
    std::unordered_map<std::uint32_t, RetainedState> m_lockStates{};
    entt::scoped_connection m_activateConnection;
    entt::scoped_connection m_assignObjectsConnection;
    entt::scoped_connection m_actorTeleportConnection;
    entt::scoped_connection m_chatConnection;
    entt::scoped_connection m_dialogueConnection;
    entt::scoped_connection m_lockConnection;
    entt::scoped_connection m_packageConnection;
    entt::scoped_connection m_objectInventoryConnection;
    entt::scoped_connection m_objectResyncConnection;
    entt::scoped_connection m_playerDialogueConnection;
    entt::scoped_connection m_questConnection;
    entt::scoped_connection m_questResyncConnection;
    entt::scoped_connection m_removeWaypointConnection;
    entt::scoped_connection m_scriptAnimationConnection;
    entt::scoped_connection m_setWaypointConnection;
    entt::scoped_connection m_subtitleConnection;
    entt::scoped_connection m_teleportConnection;
    entt::scoped_connection m_teleportCommandConnection;
    entt::scoped_connection m_timeConnection;
    entt::scoped_connection m_weatherConnection;
    entt::scoped_connection m_settingsConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_partyInfoConnection;
    entt::scoped_connection m_playerDialogueEventConnection;
    entt::scoped_connection m_localGameplayConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_gameplayResultConnection;
};
