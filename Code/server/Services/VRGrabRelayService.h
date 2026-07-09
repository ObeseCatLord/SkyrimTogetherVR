#pragma once

#include <cstdint>
#include <Events/PacketEvent.h>
#include <TiltedCore/Stl.hpp>

struct RequestVRGrabEvent;
struct World;
struct PlayerLeaveEvent;

struct VRGrabRelayService
{
    VRGrabRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRGrabRelayService() noexcept = default;

    TP_NOCOPYMOVE(VRGrabRelayService);

private:
    struct PlayerGrabRelayState
    {
        uint32_t LastSequence{0};
        uint64_t LastRelayTick{0};
        bool HasSequence{false};
    };

    void OnVRGrabEvent(const PacketEvent<RequestVRGrabEvent>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    [[nodiscard]] bool ShouldRelayGrab(uint32_t aPlayerId, const RequestVRGrabEvent& acRequest) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerGrabRelayState> m_playerGrabRelayState{};
    entt::scoped_connection m_vrGrabEventConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
