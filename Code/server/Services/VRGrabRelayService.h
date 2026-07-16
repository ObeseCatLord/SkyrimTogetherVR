#pragma once

#include <cstdint>
#include <Events/PacketEvent.h>
#include <Structs/GameId.h>
#include <TiltedCore/Stl.hpp>

struct RequestVRGrabEvent;
struct World;
struct PlayerLeaveEvent;
struct UpdateEvent;

namespace VRObjectAuthority
{
[[nodiscard]] bool AcquireOrRenew(const GameId& acObjectId, uint32_t aPlayerId, uint64_t aTick) noexcept;
[[nodiscard]] bool Release(const GameId& acObjectId, uint32_t aPlayerId) noexcept;
void ReleasePlayer(uint32_t aPlayerId) noexcept;
void Expire(uint64_t aTick) noexcept;
void Reset() noexcept;
} // namespace VRObjectAuthority

struct VRGrabRelayService
{
    VRGrabRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRGrabRelayService() noexcept;

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
    void OnUpdate(const UpdateEvent&) noexcept;
    [[nodiscard]] bool ShouldRelayGrab(uint32_t aPlayerId, const RequestVRGrabEvent& acRequest) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerGrabRelayState> m_playerGrabRelayState{};
    entt::scoped_connection m_vrGrabEventConnection;
    entt::scoped_connection m_playerLeaveConnection;
    entt::scoped_connection m_updateConnection;
};
