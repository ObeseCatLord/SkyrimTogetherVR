#pragma once

#include <atomic>
#include <memory>
#include <thread>

struct World;
struct UpdateEvent;
struct PlayerJoinEvent;
struct PlayerLeaveEvent;

/**
 * @brief Dispatches the current player list to the clients.
 */
struct ServerListService
{
    ServerListService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~ServerListService() noexcept;

    TP_NOCOPYMOVE(ServerListService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;

private:
    struct AnnouncementState
    {
        std::atomic_bool m_inFlight{false};
        std::atomic_bool m_serverRejected{false};
        std::atomic_bool m_stopRequested{false};
    };

    struct AnnouncementRequest
    {
        String m_name;
        String m_desc;
        String m_iconUrl;
        uint16_t m_port;
        uint16_t m_tickRate;
        uint16_t m_playerCount;
        uint16_t m_playerMaxCount;
        String m_tagList;
        bool m_isPublic;
        bool m_isPasswordProtected;
        int32 m_flags;
    };

    void Announce() noexcept;
    bool ProcessAnnouncementResult() noexcept;

    static bool PostAnnouncement(const AnnouncementState& acState, const AnnouncementRequest& acRequest);

    World& m_world;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_playerJoinConnection;
    entt::scoped_connection m_playerLeaveConnection;
    mutable std::chrono::steady_clock::time_point m_nextAnnounce;

    std::shared_ptr<AnnouncementState> m_announcementState;
    std::thread m_announcementThread;

    int32 m_flags = 0;

    enum
    {
        kHasPassword = 1 << 0,
        kIsPublic = 1 << 1
    };
};
