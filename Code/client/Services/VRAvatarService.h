#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_map>

#include <entt/entt.hpp>

#include <vr_common/VRGameplayBridge.h>
#include <Messages/CharacterSpawnRequest.h>

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

private:
    using AnimationSnapshot = SkyrimTogetherVR::AnimationGraphProtocol::SnapshotBuffer;

    struct RemoteAvatar
    {
        std::uint32_t PlayerId{0};
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

    void ResetSessionState() noexcept;
    void ResetLifecycleState() noexcept;
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
    TransportService& m_transport;
    SkyrimTogetherVR::GameplayBridge::LocalPlayerStatePayload m_localSnapshot{};
    AnimationSnapshot m_localAnimationSnapshot{};
    AnimationSnapshot m_pendingLocalAnimationSnapshot{};
    std::unordered_map<std::uint32_t, RemoteAvatar> m_remoteAvatars{};
    std::unordered_map<std::uint32_t, CharacterSpawnRequest> m_pendingSpawns{};
    std::uint32_t m_localPlayerId{0};
    std::optional<std::uint32_t> m_localServerId{};
    std::uint32_t m_assignmentCookie{0};
    std::uint32_t m_nextAssignmentCookie{1};
    double m_assignmentElapsed{0.0};
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
