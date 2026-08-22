#pragma once

#include <Events/PacketEvent.h>

#include <cstdint>
#include <unordered_map>

struct World;
struct PlayerLeaveCellEvent;
struct ActivateRequest;
struct LockChangeRequest;
struct AssignObjectsRequest;
struct ScriptAnimationRequest;
struct RequestObjectResync;

/**
 * @brief Manages (interactive) objects and relays interactions with said objects.
 */
class ObjectService
{
public:
    ObjectService(World& aWorld, entt::dispatcher& aDispatcher);

private:
    void OnPlayerLeaveCellEvent(const PlayerLeaveCellEvent& acEvent) noexcept;
    void OnAssignObjectsRequest(const PacketEvent<AssignObjectsRequest>&) noexcept;
    void OnActivate(const PacketEvent<ActivateRequest>&) const noexcept;
    void OnLockChange(const PacketEvent<LockChangeRequest>&) const noexcept;
    void OnScriptAnimationRequest(const PacketEvent<ScriptAnimationRequest>&) noexcept;
    void OnObjectResyncRequest(const PacketEvent<RequestObjectResync>&) noexcept;
    void OnObjectDestroy(entt::registry&, entt::entity) noexcept;
    [[nodiscard]] std::uint64_t NextObjectSnapshotRevision(
        std::uint32_t aServerId, std::uint64_t aKnownRevision) noexcept;

    World& m_world;
    std::unordered_map<std::uint32_t, std::uint64_t> m_objectSnapshotRevisions{};

    entt::scoped_connection m_leaveCellConnection;
    entt::scoped_connection m_assignObjectConnection;
    entt::scoped_connection m_activateConnection;
    entt::scoped_connection m_lockChangeConnection;
    entt::scoped_connection m_scriptAnimationConnection;
    entt::scoped_connection m_objectResyncConnection;
    entt::scoped_connection m_objectDestroyConnection;
};
