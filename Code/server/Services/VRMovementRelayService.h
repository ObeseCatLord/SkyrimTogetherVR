#pragma once

#include <cstdint>
#include <Events/PacketEvent.h>
#include <TiltedCore/Stl.hpp>

struct RequestVRMovementUpdate;
struct World;
struct PlayerLeaveEvent;

struct VRMovementRelayService
{
    VRMovementRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRMovementRelayService() noexcept = default;

    TP_NOCOPYMOVE(VRMovementRelayService);

private:
    struct PlayerMovementRelayState
    {
        uint32_t LastSequence{0};
        uint64_t LastRelayTick{0};
        bool HasSequence{false};
    };

    void OnVRMovementUpdate(const PacketEvent<RequestVRMovementUpdate>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    [[nodiscard]] bool ShouldRelayMovement(uint32_t aPlayerId, const RequestVRMovementUpdate& acRequest) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerMovementRelayState> m_playerMovementRelayState{};
    entt::scoped_connection m_vrMovementUpdateConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
