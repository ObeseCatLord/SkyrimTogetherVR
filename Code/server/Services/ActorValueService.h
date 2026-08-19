#pragma once

#include <Events/PacketEvent.h>

#include <cstdint>
#include <unordered_map>

struct World;
struct UpdateEvent;
struct TransportService;
struct PlayerLeaveEvent;
struct CharacterRemoveEvent;
struct RequestActorValueChanges;
struct RequestActorMaxValueChanges;
struct RequestHealthChangeBroadcast;
struct RequestDeathStateChange;

/**
 * @brief Broadcasts changes in (max) actor values and updates them server side.
 */
struct ActorValueService
{
    ActorValueService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~ActorValueService() noexcept = default;

    TP_NOCOPYMOVE(ActorValueService);

private:
    struct HealthActionNonceState
    {
        std::uint64_t ClientSessionNonce{};
        std::uint64_t ConnectionGeneration{};
        std::uint64_t LastActionNonce{};
    };

    World& m_world;
    // One entry per active VR sender, removed synchronously on PlayerLeave.
    // The fixed admission cap prevents malformed traffic from growing this
    // replay ledger without bound.
    std::unordered_map<std::uint32_t, HealthActionNonceState> m_vrHealthActionNonces{};
    // Server-issued edge identities are separate from sender action nonces:
    // one source event may fan out to many recipients and retransmissions.
    std::unordered_map<std::uint32_t, std::uint32_t> m_healthEventIds{};

    [[nodiscard]] std::uint32_t NextHealthEventId(std::uint32_t aActorId) noexcept;
    void OnActorValueChanges(const PacketEvent<RequestActorValueChanges>& acMessage) const noexcept;
    void OnActorMaxValueChanges(const PacketEvent<RequestActorMaxValueChanges>& acMessage) const noexcept;
    void OnHealthChangeBroadcast(const PacketEvent<RequestHealthChangeBroadcast>& acMessage) noexcept;
    void OnDeathStateChange(const PacketEvent<RequestDeathStateChange>& acMessage) const noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    void OnCharacterRemove(const CharacterRemoveEvent& acEvent) noexcept;

    entt::scoped_connection m_updateHealthConnection;
    entt::scoped_connection m_updateMaxValueConnection;
    entt::scoped_connection m_updateDeltaHealthConnection;
    entt::scoped_connection m_deathStateConnection;
    entt::scoped_connection m_playerLeaveConnection;
    entt::scoped_connection m_characterRemoveConnection;
};
