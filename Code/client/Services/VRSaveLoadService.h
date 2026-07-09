#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <Events/EventDispatcher.h>
#include <Games/Events.h>

struct ConnectedEvent;
struct ConnectionErrorEvent;
struct DisconnectedEvent;
struct TESLoadGameEvent;
struct TransportService;
struct UpdateEvent;
struct World;

struct VRSaveLoadService final : BSTEventSink<TESLoadGameEvent>
{
    VRSaveLoadService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRSaveLoadService() noexcept = default;

    TP_NOCOPYMOVE(VRSaveLoadService);

    [[nodiscard]] bool HasLoadGameEvent() const noexcept { return m_hasLoadGameEvent; }
    [[nodiscard]] uint32_t GetLoadGameCount() const noexcept { return m_loadGameCount; }
    [[nodiscard]] bool IsPlayerReady() const noexcept { return m_playerReady; }
    [[nodiscard]] bool IsReadyAfterLastLoad() const noexcept { return m_readyAfterLastLoad; }

private:
    BSTEventResult OnEvent(const TESLoadGameEvent* apEvent, const EventDispatcher<TESLoadGameEvent>* apSender) override;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnConnectionError(const ConnectionErrorEvent& acEvent) noexcept;
    void WriteSaveLoadStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_saveLoadStatusPath;
    std::string m_connectionState{"offline"};
    std::string m_lastConnectionError;
    bool m_hasLoadGameEvent{false};
    bool m_playerReady{false};
    bool m_readyAfterLastLoad{false};
    bool m_waitingForReadyAfterLoad{false};
    bool m_statusDirty{true};
    uint32_t m_loadGameCount{0};
    double m_secondsSinceLastLoad{0.0};
    double m_statusTimer{0.0};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_connectionErrorConnection;
};
