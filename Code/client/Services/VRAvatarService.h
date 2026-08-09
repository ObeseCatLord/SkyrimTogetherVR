#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include <Services/AssignmentCookie.h>
#include <vr_common/VRGameplayBridge.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Structs/ActionEvent.h>

struct AssignCharacterResponse;
struct AnimationVariables;
struct ConnectedEvent;
struct DisconnectedEvent;
struct NotifyRemoveCharacter;
struct ServerReferencesMoveRequest;
struct TransportService;
struct UpdateEvent;
struct World;

/**
 * @brief Canonical VR avatar client path backed exclusively by GameplayBridge.
 *
 * This service owns no game objects or ECS character components.  Remote avatar
 * lifetime is represented only by canonical server IDs and adapter handles.
 */
struct VRAvatarService
{
    VRAvatarService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRAvatarService() noexcept = default;

    TP_NOCOPYMOVE(VRAvatarService);

    [[nodiscard]] bool BuildLocalGameplayCommand(
        SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
        SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
        SkyrimTogetherVR::GameplayBridge::CommandRecord& arCommand) const noexcept;
    [[nodiscard]] bool BuildRemoteGameplayCommand(
        std::uint32_t aPlayerId,
        SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
        SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
        SkyrimTogetherVR::GameplayBridge::CommandRecord& arCommand) const noexcept;
    [[nodiscard]] bool BuildRemoteGameplayCommandForServerId(
        std::uint32_t aServerId,
        SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
        SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
        SkyrimTogetherVR::GameplayBridge::CommandRecord& arCommand) const noexcept;
    [[nodiscard]] bool BuildLocalNativeGameplayCommandForServerId(
        std::uint32_t aServerId, std::uint32_t aLocalReferenceFormId,
        SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
        SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
        SkyrimTogetherVR::GameplayBridge::CommandRecord& arCommand) const noexcept;
    [[nodiscard]] SkyrimTogetherVR::GameplayBridge::AdapterHandle GetRemoteAvatarHandleForServerId(
        std::uint32_t aServerId) const noexcept;
    [[nodiscard]] std::uint32_t GetRemoteServerIdForHandle(
        SkyrimTogetherVR::GameplayBridge::AdapterHandle aHandle) const noexcept;
    [[nodiscard]] std::uint32_t GetPersistentLocalReferenceForServerId(
        std::uint32_t aServerId) const noexcept;
    [[nodiscard]] std::uint32_t GetRemoteServerIdForLocalReference(
        std::uint32_t aLocalReferenceFormId) const noexcept;
    [[nodiscard]] std::uint32_t GetLocalServerId() const noexcept { return m_localServerId.value_or(0); }
    [[nodiscard]] bool QueueLocalAnimationEvent(std::uint32_t aEventId) noexcept;

private:
    using AnimationSnapshot = SkyrimTogetherVR::AnimationGraphProtocol::SnapshotBuffer;

    struct RemoteAvatar
    {
        std::uint32_t PlayerId{0};
        std::uint32_t LocalActorBaseFormId{0};
        std::uint32_t LocalReferenceFormId{0};
        std::uint32_t RuntimeActorReferenceFormId{0};
        SkyrimTogetherVR::GameplayBridge::AdapterHandle Handle{0};
        SkyrimTogetherVR::GameplayBridge::RootTransform CurrentRoot{};
        SkyrimTogetherVR::GameplayBridge::RootTransform TargetRoot{};
        bool HasTarget{false};
        bool CreatePending{false};
        bool DestroyPending{false};
        bool RemovalRequested{false};
        bool RespawnRequested{false};
        std::uint8_t CreateAttempts{0};
        double CreatePendingElapsed{0.0};
        double DestroyPendingElapsed{0.0};
        double SpatialTransferPendingElapsed{0.0};
        std::uint64_t PendingCreateActionId{0};
        std::uint64_t PendingDestroyActionId{0};
        std::uint64_t LastSubmittedSequenceId{0};
        std::uint64_t LastSubmittedAnimationSequenceId{0};
        std::uint64_t LastAcceptedServerTick{0};
        std::uint64_t NextAnimationSnapshotId{0};
        std::uint64_t LastAcknowledgedAnimationSnapshotId{0};
        std::uint64_t PendingSpatialTransferSequenceId{0};
        std::uint32_t PendingSpatialTargetCellFormId{0};
        std::uint32_t PendingSpatialTargetWorldspaceFormId{0};
        std::uint32_t TargetCellFormId{0};
        std::uint32_t TargetWorldspaceFormId{0};
        std::uint32_t AppliedCellFormId{0};
        std::uint32_t AppliedWorldspaceFormId{0};
        AnimationSnapshot PendingAnimation{};
        bool HasAcceptedServerTick{false};
        bool HasPendingAnimation{false};
        bool AnimationFaulted{false};
        bool SpatialTransferPending{false};
        bool IsPlayer{false};
    };

    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept;
    void OnCharacterSpawn(const CharacterSpawnRequest& acMessage) noexcept;
    void OnReferencesMoveRequest(const ServerReferencesMoveRequest& acMessage) noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept;

    void ConsumeBridgeEvents() noexcept;
    void HandleBridgeLifecycle(const SkyrimTogetherVR::GameplayBridge::EventRecord& acEvent) noexcept;
    void HandleBridgeLocalPlayerState(const SkyrimTogetherVR::GameplayBridge::EventRecord& acEvent) noexcept;
    void HandleBridgeRemoteAvatarState(const SkyrimTogetherVR::GameplayBridge::EventRecord& acEvent) noexcept;
    void HandleBridgeLocalAnimationGraphChunk(const SkyrimTogetherVR::GameplayBridge::EventRecord& acEvent) noexcept;
    void HandleBridgeRemoteAnimationGraphState(const SkyrimTogetherVR::GameplayBridge::EventRecord& acEvent) noexcept;
    void HandleBridgeRemoteSpatialTransferState(const SkyrimTogetherVR::GameplayBridge::EventRecord& acEvent) noexcept;
    void HandleBridgeRemoteGameplayActionState(const SkyrimTogetherVR::GameplayBridge::EventRecord& acEvent) noexcept;
    void HandleBridgeAssignmentBootstrapRecord(const SkyrimTogetherVR::GameplayBridge::EventRecord& acEvent) noexcept;
    void HandleBridgeAssignmentBootstrapText(const SkyrimTogetherVR::GameplayBridge::EventRecord& acEvent) noexcept;

    void ResetSessionState() noexcept;
    void ResetLifecycleState() noexcept;
    void ResetAssignmentBootstrap() noexcept;
    void ScheduleAssignmentBootstrapRetry() noexcept;
    void TryRequestAssignmentBootstrap() noexcept;
    void TryRequestLocalAssignment() noexcept;
    void SendLocalMovement() noexcept;
    void UpdateRemoteAvatars(double aDelta) noexcept;
    void SubmitCreateRemoteAvatar(std::uint32_t aServerId, RemoteAvatar& arAvatar) noexcept;
    void SubmitDestroyRemoteAvatar(std::uint32_t aServerId, RemoteAvatar& arAvatar) noexcept;
    void SubmitRemoteAnimationSnapshot(std::uint32_t aServerId, RemoteAvatar& arAvatar) noexcept;
    void CachePendingSpawn(const CharacterSpawnRequest& acMessage) noexcept;
    void ProcessPendingSpawns() noexcept;
    [[nodiscard]] bool StageRemoteAnimationSnapshot(RemoteAvatar& arAvatar, const AnimationVariables& acVariables,
                                                    float aDirection) noexcept;
    void RetireAvatarLifecycle(const char* apReason) noexcept;
    void ResetStatusCounters() noexcept;
    void WriteStatus() noexcept;

    [[nodiscard]] bool HasValidLocalSnapshot() const noexcept;
    [[nodiscard]] bool HasAvatarCapabilities() const noexcept;
    [[nodiscard]] bool HasAnimationCapabilities() const noexcept;
    [[nodiscard]] bool IsLocalAnimationGraphReady() const noexcept;
    [[nodiscard]] bool CanSubmitAvatarCommands() noexcept;
    [[nodiscard]] bool BuildLocalLocation(struct GameId& arCellId, struct GameId& arWorldspaceId) const noexcept;
    [[nodiscard]] bool BuildCommand(SkyrimTogetherVR::GameplayBridge::CommandKind aKind, std::uint32_t aServerId,
                                    SkyrimTogetherVR::GameplayBridge::CommandRecord& arCommand) const noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    SkyrimTogetherVR::GameplayBridge::LocalPlayerStatePayload m_localSnapshot{};
    AnimationSnapshot m_localAnimationSnapshot{};
    AnimationSnapshot m_pendingLocalAnimationSnapshot{};
    std::unordered_map<std::uint32_t, RemoteAvatar> m_remoteAvatars{};
    std::unordered_map<std::uint32_t, CharacterSpawnRequest> m_pendingSpawns{};
    std::vector<ActionEvent> m_pendingLocalAnimationEvents{};
    std::uint32_t m_localPlayerId{0};
    std::optional<std::uint32_t> m_localServerId{};
    std::uint32_t m_assignmentCookie{0};
    std::uint32_t m_nextAssignmentCookie{SkyrimTogetherVR::AssignmentCookie::kFirstLocalPlayer};
    AssignCharacterRequest m_assignmentBaseline{};
    struct AssignmentTintTextAssembly
    {
        std::uint64_t TextId{};
        std::uint16_t ChunkCount{};
        std::uint16_t ReceivedMask{};
        std::array<std::uint16_t, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks> Lengths{};
        std::array<char, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks *
                             SkyrimTogetherVR::GameplayBridge::kGameplayTextBytesPerChunk> Bytes{};
        bool Complete{};
    };
    std::uint64_t m_assignmentBootstrapRequestId{0};
    std::uint64_t m_assignmentBootstrapActionId{0};
    std::uint64_t m_nextAssignmentBootstrapRequestId{1};
    std::uint32_t m_assignmentBootstrapExpectedRecords{0};
    std::uint32_t m_assignmentBootstrapNextOrdinal{0};
    std::uint32_t m_assignmentBootstrapInventoryRecords{0};
    std::uint32_t m_assignmentBootstrapQuestRecords{0};
    std::uint32_t m_assignmentBootstrapNpcFactionRecords{0};
    std::uint32_t m_assignmentBootstrapExtraFactionRecords{0};
    std::size_t m_assignmentBootstrapOpenInventoryIndex{0};
    std::uint32_t m_assignmentBootstrapInventoryEffectsRemaining{0};
    double m_assignmentElapsed{0.0};
    double m_assignmentBootstrapElapsed{0.0};
    double m_localMovementElapsed{0.0};
    double m_statusElapsed{0.0};
    std::uint64_t m_createSubmittedCount{0};
    std::uint64_t m_createSucceededCount{0};
    std::uint64_t m_updateSubmittedCount{0};
    std::uint64_t m_destroySubmittedCount{0};
    std::uint64_t m_destroySucceededCount{0};
    std::uint64_t m_invalidTransformCount{0};
    std::uint64_t m_remoteMovementAcceptedCount{0};
    std::uint64_t m_staleMovementRejectedCount{0};
    std::uint64_t m_spatialTransferSubmittedCount{0};
    std::uint64_t m_spatialTransferSucceededCount{0};
    std::uint64_t m_spatialTransferRejectedCount{0};
    std::uint64_t m_animationSnapshotSubmittedCount{0};
    std::uint64_t m_animationSnapshotAppliedCount{0};
    std::uint64_t m_animationSnapshotRejectedCount{0};
    std::uint64_t m_sameSpaceCount{0};
    std::uint64_t m_rejectedCommandBaseline{0};
    std::uint64_t m_eventRingDropBaseline{0};
    std::uint64_t m_commandRingDropBaseline{0};
    bool m_connected{false};
    bool m_hasLocalSnapshot{false};
    bool m_assignmentPending{false};
    bool m_assignmentRejected{false};
    bool m_assignmentBootstrapPending{false};
    bool m_assignmentBootstrapRetryScheduled{false};
    bool m_assignmentBootstrapActive{false};
    bool m_assignmentBootstrapReady{false};
    bool m_assignmentBootstrapPermanentFailure{false};
    bool m_assignmentBootstrapHasActorState{false};
    bool m_assignmentBootstrapHasMagicEquipment{false};
    bool m_assignmentBootstrapHasOpenInventory{false};
    bool m_assignmentBootstrapHasInventoryExtra{false};
    bool m_assignmentBootstrapSkipOpenInventory{false};
    std::array<bool, SkyrimTogetherVR::GameplayBridge::kSkyrimActorValueCount>
        m_assignmentBootstrapActorValues{};
    std::array<bool, SkyrimTogetherVR::GameplayBridge::kMaximumAppearanceTints>
        m_assignmentBootstrapTints{};
    std::array<bool, SkyrimTogetherVR::GameplayBridge::kMaximumAppearanceTints>
        m_assignmentBootstrapTintPathsRequired{};
    std::array<AssignmentTintTextAssembly, SkyrimTogetherVR::GameplayBridge::kMaximumAppearanceTints>
        m_assignmentBootstrapTintText{};
    AssignmentTintTextAssembly m_assignmentBootstrapNameText{};
    std::array<bool, SkyrimTogetherVR::GameplayBridge::kFaceMorphCount>
        m_assignmentBootstrapFaceMorphs{};
    std::array<bool, SkyrimTogetherVR::GameplayBridge::kFacePartCount>
        m_assignmentBootstrapFaceParts{};
    std::array<bool, VRAppearance::kMaximumHeadParts>
        m_assignmentBootstrapHeadParts{};
    bool m_assignmentBootstrapHasAppearanceCore{false};
    bool m_capabilityWarningLogged{false};
    bool m_statusDirty{true};
    std::filesystem::path m_statusPath{};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_assignCharacterConnection;
    entt::scoped_connection m_characterSpawnConnection;
    entt::scoped_connection m_referencesMoveConnection;
    entt::scoped_connection m_removeCharacterConnection;
};
