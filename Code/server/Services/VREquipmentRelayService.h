#pragma once

#include <cstdint>
#include <Events/PacketEvent.h>
#include <TiltedCore/Stl.hpp>
#include <Structs/VREquipmentUpdate.h>

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
        VREquipmentUpdate LastEquipment{};
        bool HasSequence{false};
        bool HasEquipment{false};
    };

    void OnVREquipmentUpdate(const PacketEvent<RequestVREquipmentUpdate>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    [[nodiscard]] bool ShouldRelayEquipment(uint32_t aPlayerId, const RequestVREquipmentUpdate& acRequest) noexcept;
    void RelayLegacyEquipment(std::uint32_t aServerId, entt::entity aOrigin,
                              const VREquipmentUpdate* apPrevious,
                              const VREquipmentUpdate& acCurrent, const Player* apSender) const noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerEquipmentRelayState> m_playerEquipmentRelayState{};
    entt::scoped_connection m_vrEquipmentUpdateConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
