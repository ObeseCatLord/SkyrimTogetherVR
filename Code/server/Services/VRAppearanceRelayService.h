#pragma once

#include <cstdint>

#include <Events/PacketEvent.h>
#include <Structs/VRAppearance.h>
#include <TiltedCore/Stl.hpp>

struct PlayerJoinEvent;
struct PlayerLeaveEvent;
struct PlayerCellChangedEvent;
struct CharacterSpawnedEvent;
struct Player;
struct RequestVRAppearance;
struct World;

struct VRAppearanceRelayService
{
    VRAppearanceRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRAppearanceRelayService() noexcept = default;

    TP_NOCOPYMOVE(VRAppearanceRelayService);

private:
    void OnVRAppearance(const PacketEvent<RequestVRAppearance>& acMessage) noexcept;
    void OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    void OnPlayerCellChanged(const PlayerCellChangedEvent& acEvent) noexcept;
    void OnCharacterSpawned(const CharacterSpawnedEvent& acEvent) noexcept;
    void ReplayRelevantAppearances(Player& aPlayer) noexcept;
    void ReplayOwnAppearance(Player& aPlayer) noexcept;
    [[nodiscard]] bool AcceptAppearance(std::uint32_t aPlayerId, const VRAppearance& acAppearance) noexcept;

    World& m_world;
    TiltedPhoques::Map<std::uint32_t, VRAppearance> m_latestAppearance{};
    entt::scoped_connection m_vrAppearanceConnection;
    entt::scoped_connection m_playerJoinConnection;
    entt::scoped_connection m_playerLeaveConnection;
    entt::scoped_connection m_playerCellChangedConnection;
    entt::scoped_connection m_characterSpawnedConnection;
};
