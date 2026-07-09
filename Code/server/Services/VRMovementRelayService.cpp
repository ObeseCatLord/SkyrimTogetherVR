#include <Services/VRMovementRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRMovementUpdate.h>
#include <Messages/RequestVRMovementUpdate.h>
#include <Structs/VRMovementUpdate.h>

namespace
{
constexpr uint64_t kMinMovementRelayIntervalMs = 45;

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}
}

VRMovementRelayService::VRMovementRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrMovementUpdateConnection(aDispatcher.sink<PacketEvent<RequestVRMovementUpdate>>().connect<&VRMovementRelayService::OnVRMovementUpdate>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRMovementRelayService::OnPlayerLeave>(this))
{
}

void VRMovementRelayService::OnVRMovementUpdate(const PacketEvent<RequestVRMovementUpdate>& acMessage) noexcept
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR movement relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayMovement(playerId, acMessage.Packet))
        return;

    NotifyVRMovementUpdate notify{};
    notify.PlayerId = playerId;
    notify.Movement = acMessage.Packet.Movement;

    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

void VRMovementRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_playerMovementRelayState.erase(acEvent.pPlayer->GetId());
}

bool VRMovementRelayService::ShouldRelayMovement(uint32_t aPlayerId, const RequestVRMovementUpdate& acRequest) noexcept
{
    const auto& movement = acRequest.Movement;
    if (movement.Sequence == 0)
        return false;

    auto& state = m_playerMovementRelayState[aPlayerId];
    if (state.HasSequence && !IsNewerSequence(movement.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick &&
        now - state.LastRelayTick < kMinMovementRelayIntervalMs)
        return false;

    state.LastSequence = movement.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    return true;
}
