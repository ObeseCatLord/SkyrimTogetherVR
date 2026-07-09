#include <Services/VRCombatRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRCombatHitEvent.h>
#include <Messages/RequestVRCombatHitEvent.h>
#include <Structs/VRCombatHitEvent.h>

namespace
{
constexpr uint64_t kMinCombatRelayIntervalMs = 45;

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasCombatHitContext(const VRCombatHitEvent& acHit) noexcept
{
    return (acHit.HitterIsPlayer || acHit.HitteeIsPlayer) &&
           (static_cast<bool>(acHit.HitterId) || static_cast<bool>(acHit.HitteeId));
}
}

VRCombatRelayService::VRCombatRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrCombatHitEventConnection(aDispatcher.sink<PacketEvent<RequestVRCombatHitEvent>>().connect<&VRCombatRelayService::OnVRCombatHitEvent>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRCombatRelayService::OnPlayerLeave>(this))
{
}

void VRCombatRelayService::OnVRCombatHitEvent(const PacketEvent<RequestVRCombatHitEvent>& acMessage) noexcept
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR combat-hit relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayCombatHit(playerId, acMessage.Packet))
        return;

    NotifyVRCombatHitEvent notify{};
    notify.PlayerId = playerId;
    notify.Hit = acMessage.Packet.Hit;

    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

void VRCombatRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_playerCombatRelayState.erase(acEvent.pPlayer->GetId());
}

bool VRCombatRelayService::ShouldRelayCombatHit(
    uint32_t aPlayerId, const RequestVRCombatHitEvent& acRequest) noexcept
{
    const auto& hit = acRequest.Hit;
    if (hit.Sequence == 0 || !HasCombatHitContext(hit))
        return false;

    auto& state = m_playerCombatRelayState[aPlayerId];
    if (state.HasSequence && !IsNewerSequence(hit.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick && now - state.LastRelayTick < kMinCombatRelayIntervalMs)
        return false;

    state.LastSequence = hit.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    return true;
}
