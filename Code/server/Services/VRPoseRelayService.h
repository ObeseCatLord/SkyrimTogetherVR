#pragma once

#include <cstdint>
#include <Events/PacketEvent.h>
#include <TiltedCore/Stl.hpp>

struct RequestVRPoseUpdate;
struct World;
struct PlayerLeaveEvent;

struct VRPoseRelayService
{
    VRPoseRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRPoseRelayService() noexcept = default;

    TP_NOCOPYMOVE(VRPoseRelayService);

private:
    struct PlayerPoseRelayState
    {
        uint32_t LastSequence{0};
        uint64_t LastRelayTick{0};
        bool HasSequence{false};
    };

    void OnVRPoseUpdate(const PacketEvent<RequestVRPoseUpdate>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    [[nodiscard]] bool ShouldRelayPose(uint32_t aPlayerId, const RequestVRPoseUpdate& acRequest) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerPoseRelayState> m_playerPoseRelayState{};
    entt::scoped_connection m_vrPoseUpdateConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
