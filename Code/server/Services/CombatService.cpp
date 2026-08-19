#include <Services/CombatService.h>
#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Events/CharacterRemoveEvent.h>

#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/NotifyProjectileLaunch.h>

#include <atomic>

namespace
{
constexpr std::size_t kMaximumProjectileEventSources = 4096;
constexpr std::uint64_t kLedgerExhaustionLogInterval = 128;

std::atomic<std::uint64_t> g_projectileEventLedgerExhaustions{};

void LogProjectileEventLedgerExhaustion() noexcept
{
    const auto aggregate = g_projectileEventLedgerExhaustions.fetch_add(1, std::memory_order_relaxed) + 1;
    if (aggregate == 0 || (aggregate != 1 && aggregate % kLedgerExhaustionLogInterval != 0))
        return;
    try
    {
        spdlog::warn("Projectile event ID ledger capacity exhausted; rejecting new source (capacity={}, aggregate={})",
                     kMaximumProjectileEventSources, aggregate);
    }
    catch (...)
    {
    }
}
}

CombatService::CombatService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_projectileLaunchConnection = aDispatcher.sink<PacketEvent<ProjectileLaunchRequest>>().connect<&CombatService::OnProjectileLaunchRequest>(this);
    m_characterRemoveConnection = aDispatcher.sink<CharacterRemoveEvent>().connect<&CombatService::OnCharacterRemove>(this);
}

std::uint32_t CombatService::NextProjectileEventId(const std::uint32_t aShooterId) noexcept
{
    if (aShooterId == 0)
        return 0;
    auto it = m_projectileEventIds.find(aShooterId);
    if (it == m_projectileEventIds.end()) {
        if (m_projectileEventIds.size() >= kMaximumProjectileEventSources) {
            LogProjectileEventLedgerExhaustion();
            return 0;
        }
        it = m_projectileEventIds.emplace(aShooterId, 0).first;
    }
    ++it->second;
    if (it->second == 0)
        ++it->second;
    return it->second;
}

void CombatService::OnCharacterRemove(const CharacterRemoveEvent& acEvent) noexcept
{
    if (acEvent.ServerId != 0)
        m_projectileEventIds.erase(acEvent.ServerId);
}

void CombatService::OnProjectileLaunchRequest(const PacketEvent<ProjectileLaunchRequest>& acMessage) noexcept
{
    auto& packet = acMessage.Packet;
    const auto shooter = static_cast<entt::entity>(packet.ShooterID);
    if (packet.ShooterID == 0 ||
        !m_world.all_of<CharacterComponent, OwnerComponent>(shooter) ||
        m_world.get<OwnerComponent>(shooter).GetOwner() != acMessage.pPlayer)
        return;

    NotifyProjectileLaunch notify{};

    notify.ShooterID = packet.ShooterID;
    notify.EventId = NextProjectileEventId(packet.ShooterID);
    if (notify.EventId == 0)
        return;

    notify.OriginX = packet.OriginX;
    notify.OriginY = packet.OriginY;
    notify.OriginZ = packet.OriginZ;

    notify.ProjectileBaseID = packet.ProjectileBaseID;
    notify.WeaponID = packet.WeaponID;
    notify.AmmoID = packet.AmmoID;

    notify.ZAngle = packet.ZAngle;
    notify.XAngle = packet.XAngle;
    notify.YAngle = packet.YAngle;

    notify.ParentCellID = packet.ParentCellID;

    notify.SpellID = packet.SpellID;
    notify.CastingSource = packet.CastingSource;

    notify.Area = packet.Area;
    notify.Power = packet.Power;
    notify.Scale = packet.Scale;

    notify.AlwaysHit = packet.AlwaysHit;
    notify.NoDamageOutsideCombat = packet.NoDamageOutsideCombat;
    notify.AutoAim = packet.AutoAim;
    notify.DeferInitialization = packet.DeferInitialization;
    notify.ForceConeOfFire = packet.ForceConeOfFire;

    notify.UnkBool1 = packet.UnkBool1;
    notify.UnkBool2 = packet.UnkBool2;

    if (!GameServer::Get()->SendToPlayersInRange(notify, shooter, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
