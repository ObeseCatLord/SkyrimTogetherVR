#pragma once

#include <cstdint>
#include <Events/PacketEvent.h>
#include <TiltedCore/Stl.hpp>

struct RequestVRProjectileEvent;
struct World;
struct PlayerLeaveEvent;

struct VRProjectileRelayService
{
    VRProjectileRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRProjectileRelayService() noexcept = default;

    TP_NOCOPYMOVE(VRProjectileRelayService);

private:
    struct PlayerProjectileRelayState
    {
        uint32_t LastSequence{0};
        uint64_t LastRelayTick{0};
        bool HasSequence{false};
    };

    void OnVRProjectileEvent(const PacketEvent<RequestVRProjectileEvent>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    [[nodiscard]] bool ShouldRelayProjectile(uint32_t aPlayerId, const RequestVRProjectileEvent& acRequest) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerProjectileRelayState> m_playerProjectileRelayState{};
    entt::scoped_connection m_vrProjectileEventConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
