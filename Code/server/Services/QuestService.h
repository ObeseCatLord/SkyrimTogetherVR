#pragma once

#include <Events/PacketEvent.h>
#include <Structs/GameId.h>

#include <cstdint>
#include <unordered_map>

struct World;
struct UpdateEvent;
struct RequestQuestUpdate;
struct RequestQuestResync;
struct PlayerLeaveEvent;

/**
 * @brief Dispatch quest sync messages.
 *
 * This service is currently not in use.
 */
class QuestService
{
public:
    QuestService(World& aWorld, entt::dispatcher& aDispatcher);

private:
    void OnQuestChanges(const PacketEvent<RequestQuestUpdate>& aChanges) noexcept;
    void OnQuestResyncRequest(const PacketEvent<RequestQuestResync>& aRequest) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& aEvent) noexcept;
    [[nodiscard]] std::uint64_t NextQuestRevision(
        std::uint32_t aPlayerId, std::uint64_t aKnownRevision) noexcept;

    World& m_world;
    // A single canonical sequence per quest-log owner orders both deltas and
    // snapshots, so a late snapshot cannot supersede a newer update.
    std::unordered_map<std::uint32_t, std::uint64_t> m_questRevisions{};

    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_joinConnection;
    entt::scoped_connection m_questResyncConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
