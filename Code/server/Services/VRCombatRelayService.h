#pragma once

#include <array>
#include <cstdint>
#include <Events/PacketEvent.h>
#include <TiltedCore/Stl.hpp>

struct RequestVRCombatHitEvent;
struct World;
struct PlayerLeaveEvent;

struct VRCombatRelayService
{
    VRCombatRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRCombatRelayService() noexcept = default;

    TP_NOCOPYMOVE(VRCombatRelayService);

private:
    struct PlayerCombatRelayState
    {
        struct SemanticSignature
        {
            uint64_t Value{0};
            uint64_t Tick{0};
            bool Valid{false};
        };

        uint32_t LastSequence{0};
        uint64_t LastRelayTick{0};
        bool HasSequence{false};
        std::array<SemanticSignature, 32> RecentSignatures{};
        uint8_t NextSignature{0};
    };

    void OnVRCombatHitEvent(const PacketEvent<RequestVRCombatHitEvent>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    [[nodiscard]] bool ShouldRelayCombatHit(uint32_t aPlayerId, const RequestVRCombatHitEvent& acRequest) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerCombatRelayState> m_playerCombatRelayState{};
    entt::scoped_connection m_vrCombatHitEventConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
