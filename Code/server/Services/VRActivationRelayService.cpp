#include <Services/VRActivationRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRActivationEvent.h>
#include <Messages/RequestVRActivationEvent.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VRActivationEvent.h>

namespace
{
constexpr uint64_t kMinActivationRelayIntervalMs = 45;

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasActivationObject(const VRActivationEvent& acActivation) noexcept
{
    return static_cast<bool>(acActivation.ObjectId);
}
}

VRActivationRelayService::VRActivationRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrActivationEventConnection(aDispatcher.sink<PacketEvent<RequestVRActivationEvent>>().connect<&VRActivationRelayService::OnVRActivationEvent>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRActivationRelayService::OnPlayerLeave>(this))
{
}

void VRActivationRelayService::OnVRActivationEvent(const PacketEvent<RequestVRActivationEvent>& acMessage) noexcept
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR activation relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    if (!SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRActivationRelay))
        return;

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayActivation(playerId, acMessage.Packet))
        return;

    NotifyVRActivationEvent notify{};
    notify.PlayerId = playerId;
    notify.Activation = acMessage.Packet.Activation;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
            notify, *character,
            SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VRActivationRelay),
            acMessage.pPlayer))
        spdlog::warn("VR relay dropped because sender has no routable character");
}

void VRActivationRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_playerActivationRelayState.erase(acEvent.pPlayer->GetId());
}

bool VRActivationRelayService::ShouldRelayActivation(
    uint32_t aPlayerId, const RequestVRActivationEvent& acRequest) noexcept
{
    const auto& activation = acRequest.Activation;
    if (activation.Sequence == 0 || !HasActivationObject(activation))
        return false;

    auto& state = m_playerActivationRelayState[aPlayerId];
    if (state.HasSequence && !IsNewerSequence(activation.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick &&
        now - state.LastRelayTick < kMinActivationRelayIntervalMs)
        return false;

    state.LastSequence = activation.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    return true;
}
