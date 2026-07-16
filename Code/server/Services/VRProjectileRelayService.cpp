#include <Services/VRProjectileRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRProjectileEvent.h>
#include <Messages/RequestVRProjectileEvent.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VRProjectileEvent.h>

namespace
{
constexpr uint64_t kMinProjectileRelayIntervalMs = 45;

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasProjectileIntentContext(const VRProjectileEvent& acProjectile) noexcept
{
    return static_cast<bool>(acProjectile.SourceId) &&
           (acProjectile.OriginValid || acProjectile.DestinationValid || static_cast<bool>(acProjectile.WeaponId) ||
            static_cast<bool>(acProjectile.AmmoId) || static_cast<bool>(acProjectile.SpellId) ||
            acProjectile.Power > 0.0f || acProjectile.IsSunGazing);
}
}

VRProjectileRelayService::VRProjectileRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrProjectileEventConnection(aDispatcher.sink<PacketEvent<RequestVRProjectileEvent>>().connect<&VRProjectileRelayService::OnVRProjectileEvent>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRProjectileRelayService::OnPlayerLeave>(this))
{
}

void VRProjectileRelayService::OnVRProjectileEvent(const PacketEvent<RequestVRProjectileEvent>& acMessage) noexcept
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR projectile relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    if (!SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRProjectileRelay))
        return;

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayProjectile(playerId, acMessage.Packet))
        return;

    NotifyVRProjectileEvent notify{};
    notify.PlayerId = playerId;
    notify.Projectile = acMessage.Packet.Projectile;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
            notify, *character,
            SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VRProjectileRelay),
            acMessage.pPlayer))
        spdlog::warn("VR relay dropped because sender has no routable character");
}

void VRProjectileRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_playerProjectileRelayState.erase(acEvent.pPlayer->GetId());
}

bool VRProjectileRelayService::ShouldRelayProjectile(
    uint32_t aPlayerId, const RequestVRProjectileEvent& acRequest) noexcept
{
    const auto& projectile = acRequest.Projectile;
    if (projectile.Sequence == 0 || !HasProjectileIntentContext(projectile))
        return false;

    auto& state = m_playerProjectileRelayState[aPlayerId];
    if (state.HasSequence && !IsNewerSequence(projectile.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick &&
        now - state.LastRelayTick < kMinProjectileRelayIntervalMs)
        return false;

    state.LastSequence = projectile.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    return true;
}
