#include <Services/VRPoseRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRPoseUpdate.h>
#include <Messages/RequestVRPoseUpdate.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VRPoseUpdate.h>

namespace
{
constexpr uint64_t kMinPoseRelayIntervalMs = 45;
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

    if (!SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRPoseRelay))
        return;

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayPose(playerId, acMessage.Packet))
        return;

    NotifyVRPoseUpdate notify{};
    notify.PlayerId = playerId;
    notify.Pose = acMessage.Packet.Pose;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
            notify, *character,
            SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VRPoseRelay),
            acMessage.pPlayer))
        spdlog::warn("VR relay dropped because sender has no routable character");
}

void VRPoseRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_playerPoseRelayState.erase(acEvent.pPlayer->GetId());
}

bool VRPoseRelayService::ShouldRelayPose(uint32_t aPlayerId, const RequestVRPoseUpdate& acRequest) noexcept
{
    const auto& pose = acRequest.Pose;
    if (pose.Sequence == 0 || !HasAnyVRPosePayload(pose))
        return false;

    if (!IsVRPoseUpdateSafe(pose))
        return false;

    const auto stateIt = m_playerPoseRelayState.find(aPlayerId);
    const auto& state = stateIt != m_playerPoseRelayState.end() ? stateIt->second : PlayerPoseRelayState{};
    if (state.HasSequence && !IsNewerSequence(pose.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick && now - state.LastRelayTick < kMinPoseRelayIntervalMs)
        return false;

    auto& mutableState = m_playerPoseRelayState[aPlayerId];
    mutableState.LastSequence = pose.Sequence;
    mutableState.LastRelayTick = now;
    mutableState.HasSequence = true;
    return true;
}
