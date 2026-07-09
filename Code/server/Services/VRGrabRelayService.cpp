#include <Services/VRGrabRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRGrabEvent.h>
#include <Messages/RequestVRGrabEvent.h>
#include <Structs/VRGrabEvent.h>

namespace
{
constexpr uint64_t kMinGrabRelayIntervalMs = 45;

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasGrabObject(const VRGrabEvent& acGrab) noexcept
{
    return static_cast<bool>(acGrab.ObjectId);
}
}

VRGrabRelayService::VRGrabRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrGrabEventConnection(aDispatcher.sink<PacketEvent<RequestVRGrabEvent>>().connect<&VRGrabRelayService::OnVRGrabEvent>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRGrabRelayService::OnPlayerLeave>(this))
{
}

void VRGrabRelayService::OnVRGrabEvent(const PacketEvent<RequestVRGrabEvent>& acMessage) noexcept
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR grab relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayGrab(playerId, acMessage.Packet))
        return;

    NotifyVRGrabEvent notify{};
    notify.PlayerId = playerId;
    notify.Grab = acMessage.Packet.Grab;

    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

void VRGrabRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_playerGrabRelayState.erase(acEvent.pPlayer->GetId());
}

bool VRGrabRelayService::ShouldRelayGrab(uint32_t aPlayerId, const RequestVRGrabEvent& acRequest) noexcept
{
    const auto& grab = acRequest.Grab;
    if (grab.Sequence == 0 || !HasGrabObject(grab))
        return false;

    auto& state = m_playerGrabRelayState[aPlayerId];
    if (state.HasSequence && !IsNewerSequence(grab.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick && now - state.LastRelayTick < kMinGrabRelayIntervalMs)
        return false;

    state.LastSequence = grab.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    return true;
}
