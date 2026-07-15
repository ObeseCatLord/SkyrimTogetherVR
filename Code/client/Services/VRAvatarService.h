#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>

#include <entt/entt.hpp>

#include <vr_common/VRGameplayBridge.h>

struct AssignCharacterResponse;
struct CharacterSpawnRequest;
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
    struct RemoteAvatar
    {
        std::uint32_t PlayerId{0};
        GameplayBridge::AdapterHandle Handle{0};
        GameplayBridge::RootTransform CurrentRoot{};
        GameplayBridge::RootTransform TargetRoot{};
        bool HasTarget{false};
        bool CreatePending{false};
        bool DestroyPending{false};
        bool RemovalRequested{false};
        std::uint8_t DestroyAttempts{0};
    };

    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept;
    void OnCharacterSpawn(const CharacterSpawnRequest& acMessage) noexcept;
    void OnReferencesMoveRequest(const ServerReferencesMoveRequest& acMessage) noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept;

    void ConsumeBridgeEvents() noexcept;
    void HandleBridgeLifecycle(const GameplayBridge::EventRecord& acEvent) noexcept;
    void HandleBridgeLocalPlayerState(const GameplayBridge::EventRecord& acEvent) noexcept;
    void HandleBridgeRemoteAvatarState(const GameplayBridge::EventRecord& acEvent) noexcept;

    void ResetSessionState() noexcept;
    void ResetLifecycleState() noexcept;
    void TryRequestLocalAssignment() noexcept;
    void SendLocalMovement() noexcept;
    void UpdateRemoteAvatars(double aDelta) noexcept;
    void SubmitCreateRemoteAvatar(std::uint32_t aServerId, RemoteAvatar& arAvatar) noexcept;
    void SubmitDestroyRemoteAvatar(std::uint32_t aServerId, RemoteAvatar& arAvatar) noexcept;
    void ResetStatusCounters() noexcept;
    void WriteStatus() noexcept;

    [[nodiscard]] bool HasValidLocalSnapshot() const noexcept;
    [[nodiscard]] bool HasAvatarCapabilities() const noexcept;
    [[nodiscard]] bool CanSubmitAvatarCommands() noexcept;
    [[nodiscard]] bool BuildLocalLocation(struct GameId& arCellId, struct GameId& arWorldspaceId) const noexcept;
    [[nodiscard]] bool BuildCommand(GameplayBridge::CommandKind aKind, std::uint32_t aServerId, GameplayBridge::CommandRecord& arCommand) const noexcept;
    [[nodiscard]] static std::uint64_t ToBridgeEntityId(std::uint32_t aServerId) noexcept;
    [[nodiscard]] static std::uint32_t ToBridgeEntityGeneration(std::uint32_t aServerId) noexcept;

    World& m_world;
    TransportService& m_transport;
    GameplayBridge::LocalPlayerStatePayload m_localSnapshot{};
    std::unordered_map<std::uint32_t, RemoteAvatar> m_remoteAvatars{};
    std::uint32_t m_localPlayerId{0};
    std::uint32_t m_localServerId{0};
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
    std::uint64_t m_sameSpaceCount{0};
    std::uint64_t m_rejectedCommandBaseline{0};
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
