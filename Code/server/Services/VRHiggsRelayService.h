#pragma once

#include <cstdint>
#include <Events/PacketEvent.h>
#include <TiltedCore/Stl.hpp>

struct RequestVRHiggsState;
struct World;
struct PlayerLeaveEvent;

struct VRHiggsRelayService
{
    VRHiggsRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRHiggsRelayService() noexcept = default;

    TP_NOCOPYMOVE(VRHiggsRelayService);

private:
    struct PlayerHiggsRelayState
    {
        uint32_t LastSequence{0};
        uint64_t LastRelayTick{0};
        bool HasSequence{false};
    };

    void OnVRHiggsState(const PacketEvent<RequestVRHiggsState>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    [[nodiscard]] bool ShouldRelayHiggsState(uint32_t aPlayerId, const RequestVRHiggsState& acRequest) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerHiggsRelayState> m_playerHiggsRelayState{};
    entt::scoped_connection m_vrHiggsStateConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
