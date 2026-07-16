#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <entt/entt.hpp>

struct DisconnectedEvent;
struct NotifyPlayerRespawn;
struct TransportService;
struct UpdateEvent;
struct VRAvatarService;
struct VRLocalGameplayService;
struct World;

namespace SkyrimTogetherVR
{
struct LocalGameplayBridgeEvent;
struct RemoteGameplayBridgeResultEvent;
}

/**
 * Owns the VR local-player death-to-respawn flow without using legacy game
 * wrappers. Bridge observations drive the timer and bridge commands perform
 * the local respawn and authoritative gold removal.
 */
struct VRDeathRespawnService
{
    VRDeathRespawnService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                          VRAvatarService& aAvatars, VRLocalGameplayService& aLocalGameplay) noexcept;
    ~VRDeathRespawnService() noexcept = default;

    TP_NOCOPYMOVE(VRDeathRespawnService);

private:
    void OnLocalGameplayBridgeEvent(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept;
    void OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) noexcept;
    void OnGameplayResult(const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

    [[nodiscard]] bool IsValidLocalDeathState(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) const noexcept;
    void CancelRespawnTimer() noexcept;
    void ResetSessionState() noexcept;
    void SubmitRespawn() noexcept;
    void SubmitServerRespawnRequest() noexcept;
    void SubmitGoldLoss() noexcept;
    void SubmitHudMessage(std::string_view acMessage) noexcept;
    void SubmitPendingHudMessage() noexcept;

    World& m_world;
    TransportService& m_transport;
    VRAvatarService& m_avatars;
    VRLocalGameplayService& m_localGameplay;
    std::uint32_t m_sessionServerId{0};
    std::uint32_t m_pendingServerId{0};
    std::uint64_t m_sessionLifecycleEpoch{0};
    std::uint64_t m_lastDeathActionId{0};
    std::uint64_t m_respawnActionId{0};
    std::uint64_t m_goldActionId{0};
    std::uint64_t m_nextHudTextId{1};
    double m_respawnRemaining{0.0};
    double m_goldRetryRemaining{0.0};
    double m_hudRetryRemaining{0.0};
    std::int32_t m_pendingGoldLoss{0};
    std::int32_t m_totalGoldLoss{0};
    std::int32_t m_pendingGoldChunk{0};
    std::uint8_t m_respawnAttempts{0};
    std::uint8_t m_goldAttempts{0};
    std::uint8_t m_hudAttempts{0};
    bool m_hasDeathActionId{false};
    bool m_deathObserved{false};
    bool m_serverRespawnPending{false};
    std::string m_pendingHudMessage{};
    entt::scoped_connection m_localGameplayConnection;
    entt::scoped_connection m_notifyRespawnConnection;
    entt::scoped_connection m_gameplayResultConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
};
