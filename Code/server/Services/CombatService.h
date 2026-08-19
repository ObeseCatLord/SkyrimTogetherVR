#pragma once

#include <Events/PacketEvent.h>

#include <cstdint>
#include <unordered_map>

struct World;
struct ProjectileLaunchRequest;
struct CharacterRemoveEvent;

struct CombatService
{
    CombatService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~CombatService() noexcept = default;

    TP_NOCOPYMOVE(CombatService);

protected:
    void OnProjectileLaunchRequest(const PacketEvent<ProjectileLaunchRequest>& acMessage) noexcept;

    [[nodiscard]] std::uint32_t NextProjectileEventId(std::uint32_t aShooterId) noexcept;
    void OnCharacterRemove(const CharacterRemoveEvent& acEvent) noexcept;

private:
    World& m_world;
    // One sequence per source actor. A fixed cap prevents malformed actor IDs
    // from turning relay traffic into unbounded server state.
    std::unordered_map<std::uint32_t, std::uint32_t> m_projectileEventIds{};

    entt::scoped_connection m_projectileLaunchConnection;
    entt::scoped_connection m_characterRemoveConnection;
};
