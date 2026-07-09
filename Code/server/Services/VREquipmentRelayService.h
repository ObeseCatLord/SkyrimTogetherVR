#pragma once

#include <cstdint>
#include <Events/PacketEvent.h>
#include <TiltedCore/Stl.hpp>

struct RequestVREquipmentUpdate;
struct World;
struct PlayerLeaveEvent;

struct VREquipmentRelayService
{
    VREquipmentRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VREquipmentRelayService() noexcept = default;

    TP_NOCOPYMOVE(VREquipmentRelayService);

private:
    struct PlayerEquipmentRelayState
    {
        uint32_t LastSequence{0};
        uint64_t LastRelayTick{0};
        bool HasSequence{false};
    };

    void OnVREquipmentUpdate(const PacketEvent<RequestVREquipmentUpdate>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    [[nodiscard]] bool ShouldRelayEquipment(uint32_t aPlayerId, const RequestVREquipmentUpdate& acRequest) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerEquipmentRelayState> m_playerEquipmentRelayState{};
    entt::scoped_connection m_vrEquipmentUpdateConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
