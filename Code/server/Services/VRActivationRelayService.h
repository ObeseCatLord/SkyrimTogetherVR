#pragma once

#include <cstdint>
#include <Events/PacketEvent.h>
#include <TiltedCore/Stl.hpp>

struct RequestVRActivationEvent;
struct World;
struct PlayerLeaveEvent;

struct VRActivationRelayService
{
    VRActivationRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRActivationRelayService() noexcept = default;

    TP_NOCOPYMOVE(VRActivationRelayService);

private:
    struct PlayerActivationRelayState
    {
        uint32_t LastSequence{0};
        uint64_t LastRelayTick{0};
        bool HasSequence{false};
    };

    void OnVRActivationEvent(const PacketEvent<RequestVRActivationEvent>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    [[nodiscard]] bool ShouldRelayActivation(uint32_t aPlayerId, const RequestVRActivationEvent& acRequest) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerActivationRelayState> m_playerActivationRelayState{};
    entt::scoped_connection m_vrActivationEventConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
