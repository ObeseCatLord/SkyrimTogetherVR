#include <Services/VRHiggsRelayService.h>
#include <Services/VRGrabRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRHiggsState.h>
#include <Messages/RequestVRHiggsState.h>
#include <Structs/GameplayCapabilities.h>
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

bool IsLeaseAcquireEvent(VRHiggsEventSnapshot::Kind aKind) noexcept
{
    return aKind == VRHiggsEventSnapshot::Kind::kPulled || aKind == VRHiggsEventSnapshot::Kind::kGrabbed ||
           aKind == VRHiggsEventSnapshot::Kind::kStartTwoHanding;
}

bool IsLeaseReleaseEvent(VRHiggsEventSnapshot::Kind aKind) noexcept
{
    return aKind == VRHiggsEventSnapshot::Kind::kDropped || aKind == VRHiggsEventSnapshot::Kind::kStashed ||
           aKind == VRHiggsEventSnapshot::Kind::kConsumed || aKind == VRHiggsEventSnapshot::Kind::kStopTwoHanding;
}

bool HasHiggsObjectAuthority(const VRHiggsState& acState, uint32_t aPlayerId, uint64_t aTick,
                             bool aProcessLastEvent) noexcept
{
    const auto renewHandLease = [aPlayerId, aTick](const VRHiggsHandState& acHand) noexcept {
        return !acHand.HoldingObject || !acHand.GrabbedObject ||
               VRObjectAuthority::AcquireOrRenew(acHand.GrabbedObject, aPlayerId, aTick);
    };

    if (!renewHandLease(acState.Left) || !renewHandLease(acState.Right))
        return false;

    if (!aProcessLastEvent || !acState.LastEventValid || !acState.LastEvent.ObjectId)
        return true;

    if (IsLeaseAcquireEvent(acState.LastEvent.EventKind))
        return VRObjectAuthority::AcquireOrRenew(acState.LastEvent.ObjectId, aPlayerId, aTick);

    if (IsLeaseReleaseEvent(acState.LastEvent.EventKind))
        return VRObjectAuthority::Release(acState.LastEvent.ObjectId, aPlayerId);

    return true;
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

    if (!SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRHiggsRelay))
        return;

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayHiggsState(playerId, acMessage.Packet))
        return;

    NotifyVRHiggsState notify{};
    notify.PlayerId = playerId;
    notify.State = acMessage.Packet.State;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
            notify, *character,
            SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VRHiggsRelay),
            acMessage.pPlayer))
        spdlog::warn("VR relay dropped because sender has no routable character");
}

void VRHiggsRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
    {
        m_playerHiggsRelayState.erase(acEvent.pPlayer->GetId());
        VRObjectAuthority::ReleasePlayer(acEvent.pPlayer->GetId());
    }
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

    const bool hasNewAction = statePacket.LastEventValid && statePacket.LastEvent.Sequence != 0 &&
                              (!state.HasActionSequence || IsNewerSequence(statePacket.LastEvent.Sequence, state.LastActionSequence));
    if (!HasHiggsObjectAuthority(statePacket, aPlayerId, now, hasNewAction))
        return false;

    state.LastSequence = statePacket.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    if (hasNewAction)
    {
        state.LastActionSequence = statePacket.LastEvent.Sequence;
        state.HasActionSequence = true;
    }
    return true;
}
