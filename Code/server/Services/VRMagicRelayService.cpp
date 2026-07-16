#include <Services/VRMagicRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRMagicEffectEvent.h>
#include <Messages/RequestVRMagicEffectEvent.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VRMagicEffectEvent.h>

namespace
{
constexpr uint64_t kMinMagicRelayIntervalMs = 45;

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasMagicEffectContext(const VRMagicEffectEvent& acMagicEffect) noexcept
{
    return static_cast<bool>(acMagicEffect.EffectId) &&
           (acMagicEffect.CasterIsPlayer || acMagicEffect.TargetIsPlayer ||
            static_cast<bool>(acMagicEffect.CasterId) || static_cast<bool>(acMagicEffect.TargetId));
}
}

VRMagicRelayService::VRMagicRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrMagicEffectEventConnection(aDispatcher.sink<PacketEvent<RequestVRMagicEffectEvent>>().connect<&VRMagicRelayService::OnVRMagicEffectEvent>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRMagicRelayService::OnPlayerLeave>(this))
{
}

void VRMagicRelayService::OnVRMagicEffectEvent(const PacketEvent<RequestVRMagicEffectEvent>& acMessage) noexcept
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR magic-effect relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    if (!SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRMagicRelay))
        return;

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayMagicEffect(playerId, acMessage.Packet))
        return;

    NotifyVRMagicEffectEvent notify{};
    notify.PlayerId = playerId;
    notify.MagicEffect = acMessage.Packet.MagicEffect;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
            notify, *character,
            SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VRMagicRelay),
            acMessage.pPlayer))
        spdlog::warn("VR relay dropped because sender has no routable character");
}

void VRMagicRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_playerMagicRelayState.erase(acEvent.pPlayer->GetId());
}

bool VRMagicRelayService::ShouldRelayMagicEffect(
    uint32_t aPlayerId, const RequestVRMagicEffectEvent& acRequest) noexcept
{
    const auto& magicEffect = acRequest.MagicEffect;
    if (magicEffect.Sequence == 0 || !HasMagicEffectContext(magicEffect))
        return false;

    auto& state = m_playerMagicRelayState[aPlayerId];
    if (state.HasSequence && !IsNewerSequence(magicEffect.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick && now - state.LastRelayTick < kMinMagicRelayIntervalMs)
        return false;

    state.LastSequence = magicEffect.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    return true;
}
