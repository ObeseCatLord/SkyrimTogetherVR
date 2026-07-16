#pragma once

#include "Events/ConnectedEvent.h"
#include "Events/DisconnectedEvent.h"

#include <atomic>
#include <Client.hpp>
#include <deque>
#include <thread>
#include <vector>

struct ImguiService;
struct UpdateEvent;
struct ClientMessage;
struct AuthenticationResponse;
struct NotifySettingsChange;

namespace SkyrimTogetherVR::GameplayBridge
{
enum class EpochRetireReason : std::uint32_t;
}

struct World;

using TiltedPhoques::Client;

/**
 * @brief Handles communication with the server.
 */
struct TransportService : Client
{
    TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~TransportService() noexcept = default;

    TP_NOCOPYMOVE(TransportService);

    // Success means the serialized packet was accepted into this service's
    // bounded owner-thread queue. TiltedConnect's void send API cannot report
    // the later Steam Networking Sockets result.
    bool Send(const ClientMessage& acMessage) noexcept;

    void OnConsume(const void* apData, uint32_t aSize) override;
    void OnConnected() override;
    void OnDisconnected(EDisconnectReason aReason) override;
    void OnUpdate() override;

    [[nodiscard]] bool IsOnline() const noexcept { return m_connected; }
    void SetServerPassword(const std::string& acPassword) noexcept { m_serverPassword = acPassword; }
    const uint32_t& GetLocalPlayerId() const noexcept { return m_localPlayerId; }
    [[nodiscard]] uint64_t GetSessionId() const noexcept { return m_sessionId; }
    [[nodiscard]] uint64_t GetConnectionGeneration() const noexcept { return m_connectionGeneration; }
    [[nodiscard]] uint64_t GetServerInstanceNonce() const noexcept { return m_serverInstanceNonce; }
    [[nodiscard]] uint64_t GetNegotiatedGameplayCapabilities() const noexcept { return m_negotiatedGameplayCapabilities; }
    [[nodiscard]] uint64_t GetRequestedGameplayCapabilities() const noexcept { return m_requestedGameplayCapabilities; }
    [[nodiscard]] bool IsGameplayCleanupRequired() const noexcept { return m_gameplayCleanupRequired; }
    [[nodiscard]] bool RetireGameplaySession(SkyrimTogetherVR::GameplayBridge::EpochRetireReason aReason) noexcept;

protected:
    // Event handlers
    void HandleUpdate(const UpdateEvent& acEvent) noexcept;
    void HandleConnected(const ConnectedEvent& acEvent) noexcept;
    void HandleDisconnected(const DisconnectedEvent& acEvent) noexcept;

    // Packet handlers
    void HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept;
    void HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept;

private:
    struct PendingOutboundPacket
    {
        std::vector<std::uint8_t> Data{};
        TiltedPhoques::EPacketFlags Flags{TiltedPhoques::kReliable};
        std::uint64_t ConnectionAttempt{};
        std::uint64_t ConnectionGeneration{};
    };

    void DrainOutboundQueue() noexcept;
    void ClearOutboundQueue() noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    bool m_connected = false;
    String m_serverPassword{};
    uint32_t m_localPlayerId = 0;
    uint64_t m_sessionId = 0;
    uint64_t m_connectionGeneration = 0;
    uint64_t m_connectionAttemptGeneration = 0;
    uint64_t m_serverInstanceNonce = 0;
    uint64_t m_negotiatedGameplayCapabilities = 0;
    uint64_t m_requestedGameplayCapabilities = 0;
    bool m_gameplayCleanupRequired = false;
    std::deque<PendingOutboundPacket> m_outboundQueue{};
    std::size_t m_outboundQueueBytes{};
    bool m_drainingOutboundQueue{};
    const std::thread::id m_ownerThreadId;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_sendServerMessageConnection;
    entt::scoped_connection m_settingsChangeConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    std::function<void(UniquePtr<ServerMessage>&)> m_messageHandlers[kServerOpcodeMax];
};
