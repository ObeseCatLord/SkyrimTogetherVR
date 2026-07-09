#include <Services/VRHiggsRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRHiggsState.h>
#include <Messages/RequestVRHiggsState.h>
#include <Structs/VRHiggsState.h>

namespace
{
constexpr uint64_t kMinHiggsRelayIntervalMs = 200;

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasHiggsObservation(const VRHiggsState& acState) noexcept
{
    return acState.BridgeLoaded || acState.Detected || acState.InterfaceAvailable || acState.CallbacksRegistered ||
           acState.SnapshotAvailable || acState.Left.Valid || acState.Right.Valid || acState.LastEventValid;
}
}

VRHiggsRelayService::VRHiggsRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrHiggsStateConnection(aDispatcher.sink<PacketEvent<RequestVRHiggsState>>().connect<&VRHiggsRelayService::OnVRHiggsState>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRHiggsRelayService::OnPlayerLeave>(this))
{
}

void VRHiggsRelayService::OnVRHiggsState(const PacketEvent<RequestVRHiggsState>& acMessage) noexcept
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR HIGGS relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayHiggsState(playerId, acMessage.Packet))
        return;

    NotifyVRHiggsState notify{};
    notify.PlayerId = playerId;
    notify.State = acMessage.Packet.State;

    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

void VRHiggsRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_playerHiggsRelayState.erase(acEvent.pPlayer->GetId());
}

bool VRHiggsRelayService::ShouldRelayHiggsState(uint32_t aPlayerId, const RequestVRHiggsState& acRequest) noexcept
{
    const auto& statePacket = acRequest.State;
    if (statePacket.Sequence == 0 || !HasHiggsObservation(statePacket))
        return false;

    auto& state = m_playerHiggsRelayState[aPlayerId];
    if (state.HasSequence && !IsNewerSequence(statePacket.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick && now - state.LastRelayTick < kMinHiggsRelayIntervalMs)
        return false;

    state.LastSequence = statePacket.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    return true;
}
