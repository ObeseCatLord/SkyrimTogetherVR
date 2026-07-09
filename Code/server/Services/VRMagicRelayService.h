#pragma once

#include <cstdint>
#include <Events/PacketEvent.h>
#include <TiltedCore/Stl.hpp>

struct RequestVRMagicEffectEvent;
struct World;
struct PlayerLeaveEvent;

struct VRMagicRelayService
{
    VRMagicRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRMagicRelayService() noexcept = default;

    TP_NOCOPYMOVE(VRMagicRelayService);

private:
    struct PlayerMagicRelayState
    {
        uint32_t LastSequence{0};
        uint64_t LastRelayTick{0};
        bool HasSequence{false};
    };

    void OnVRMagicEffectEvent(const PacketEvent<RequestVRMagicEffectEvent>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    [[nodiscard]] bool ShouldRelayMagicEffect(uint32_t aPlayerId, const RequestVRMagicEffectEvent& acRequest) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerMagicRelayState> m_playerMagicRelayState{};
    entt::scoped_connection m_vrMagicEffectEventConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
