#include <Services/VRCombatRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRCombatHitEvent.h>
#include <Messages/RequestVRCombatHitEvent.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VRCombatHitEvent.h>

namespace
{
constexpr uint64_t kMinCombatRelayIntervalMs = 45;
constexpr uint64_t kCombatSignatureLifetimeMs = 750;

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasCombatHitContext(const VRCombatHitEvent& acHit) noexcept
{
    return (acHit.HitterIsPlayer || acHit.HitteeIsPlayer) &&
           (static_cast<bool>(acHit.HitterId) || static_cast<bool>(acHit.HitteeId));
}

uint64_t MixSignature(uint64_t aSeed, uint64_t aValue) noexcept
{
    return aSeed ^ (aValue + 0x9e3779b97f4a7c15ull + (aSeed << 6) + (aSeed >> 2));
}

uint64_t BuildCombatSignature(const VRCombatHitEvent& acHit) noexcept
{
    uint64_t signature = 0;
    signature = MixSignature(signature, acHit.HitterId.LogFormat());
    signature = MixSignature(signature, acHit.HitteeId.LogFormat());
    signature = MixSignature(signature, acHit.SourceId.LogFormat());
    signature = MixSignature(signature, acHit.ProjectileId.LogFormat());
    signature = MixSignature(signature, acHit.HitterPosition.Pack());
    signature = MixSignature(signature, acHit.HitteePosition.Pack());
    signature = MixSignature(signature, acHit.RawHitFlags);
    signature = MixSignature(signature, acHit.HitterFormType);
    signature = MixSignature(signature, acHit.HitteeFormType);
    signature = MixSignature(signature, acHit.PlanckHit);
    signature = MixSignature(signature, acHit.HitterIsPlayer);
    return MixSignature(signature, acHit.HitteeIsPlayer);
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

    if (!SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRCombatPlanckRelay))
        return;

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayCombatHit(playerId, acMessage.Packet))
        return;

    NotifyVRCombatHitEvent notify{};
    notify.PlayerId = playerId;
    notify.Hit = acMessage.Packet.Hit;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
            notify, *character,
            SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VRCombatPlanckRelay),
            acMessage.pPlayer))
        spdlog::warn("VR relay dropped because sender has no routable character");
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

    const auto signature = BuildCombatSignature(hit);
    for (const auto& entry : state.RecentSignatures)
    {
        if (entry.Valid && entry.Value == signature && now >= entry.Tick && now - entry.Tick < kCombatSignatureLifetimeMs)
        {
            state.LastSequence = hit.Sequence;
            state.HasSequence = true;
            return false;
        }
    }

    state.LastSequence = hit.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    state.RecentSignatures[state.NextSignature] = {signature, now, true};
    state.NextSignature = static_cast<uint8_t>((state.NextSignature + 1) % state.RecentSignatures.size());
    return true;
}
