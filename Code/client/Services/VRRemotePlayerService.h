#pragma once

#include <cstddef>
#include <filesystem>

#include <Structs/GameId.h>
#include <TiltedCore/Stl.hpp>

struct DisconnectedEvent;
struct NotifyPlayerCellChanged;
struct NotifyPlayerJoined;
struct NotifyPlayerList;
struct NotifyPlayerLeft;
struct TransportService;
struct UpdateEvent;
struct World;

struct VRRemotePlayerInfo
{
    uint32_t PlayerId{0};
    String Username{};
    GameId WorldSpaceId{};
    GameId CellId{};
    uint16_t Level{0};
    double AgeSeconds{0.0};
};

struct VRRemotePlayerService
{
    VRRemotePlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRRemotePlayerService() noexcept = default;

    TP_NOCOPYMOVE(VRRemotePlayerService);

    [[nodiscard]] size_t GetRemotePlayerCount() const noexcept { return m_players.size(); }
    [[nodiscard]] const TiltedPhoques::Map<uint32_t, VRRemotePlayerInfo>& GetRemotePlayers() const noexcept { return m_players; }

private:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept;
    void OnPlayerList(const NotifyPlayerList& acMessage) noexcept;
    void OnPlayerCellChanged(const NotifyPlayerCellChanged& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void WriteRemotePlayerStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_remotePlayerStatusPath;
    TiltedPhoques::Map<uint32_t, VRRemotePlayerInfo> m_players{};
    double m_statusTimer{0.0};
    bool m_statusDirty{true};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_playerJoinedConnection;
    entt::scoped_connection m_playerListConnection;
    entt::scoped_connection m_playerCellChangedConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_disconnectedConnection;
};
