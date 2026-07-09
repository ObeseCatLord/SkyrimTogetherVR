#include <Services/VRPoseRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRPoseUpdate.h>
#include <Messages/RequestVRPoseUpdate.h>
#include <Structs/VRPoseUpdate.h>

namespace
{
constexpr uint64_t kMinPoseRelayIntervalMs = 45;

bool HasAnyPoseNode(const VRPoseUpdate& acPose) noexcept
{
    return acPose.Hmd.Valid || acPose.LeftHand.Valid || acPose.RightHand.Valid || acPose.SpellOrigin.Valid ||
           acPose.SpellDestination.Valid || acPose.ArrowOrigin.Valid || acPose.ArrowDestination.Valid ||
           acPose.BowAim.Valid || acPose.BowRotation.Valid || acPose.LeftWeaponOffset.Valid ||
           acPose.RightWeaponOffset.Valid || acPose.PrimaryMagicOffset.Valid || acPose.PrimaryMagicAim.Valid ||
           acPose.SecondaryMagicOffset.Valid || acPose.SecondaryMagicAim.Valid;
}

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}
}

VRPoseRelayService::VRPoseRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrPoseUpdateConnection(aDispatcher.sink<PacketEvent<RequestVRPoseUpdate>>().connect<&VRPoseRelayService::OnVRPoseUpdate>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRPoseRelayService::OnPlayerLeave>(this))
{
}

void VRPoseRelayService::OnVRPoseUpdate(const PacketEvent<RequestVRPoseUpdate>& acMessage) noexcept
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR pose relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayPose(playerId, acMessage.Packet))
        return;

    NotifyVRPoseUpdate notify{};
    notify.PlayerId = playerId;
    notify.Pose = acMessage.Packet.Pose;

    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

void VRPoseRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_playerPoseRelayState.erase(acEvent.pPlayer->GetId());
}

bool VRPoseRelayService::ShouldRelayPose(uint32_t aPlayerId, const RequestVRPoseUpdate& acRequest) noexcept
{
    const auto& pose = acRequest.Pose;
    if (pose.Sequence == 0 || !HasAnyPoseNode(pose))
        return false;

    auto& state = m_playerPoseRelayState[aPlayerId];
    if (state.HasSequence && !IsNewerSequence(pose.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick && now - state.LastRelayTick < kMinPoseRelayIntervalMs)
        return false;

    state.LastSequence = pose.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    return true;
}
