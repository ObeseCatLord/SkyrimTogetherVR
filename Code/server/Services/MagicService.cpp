#include <Services/MagicService.h>

#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Events/CharacterRemoveEvent.h>

#include <Messages/SpellCastRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/AddTargetRequest.h>

#include <Messages/NotifySpellCast.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyRemoveSpell.h>

#include <atomic>
#include <cmath>

namespace
{
constexpr std::size_t kMaximumMagicEventSources = 4096;
constexpr std::uint64_t kLedgerExhaustionLogInterval = 128;

std::atomic<std::uint64_t> g_magicEventLedgerExhaustions{};

void LogMagicEventLedgerExhaustion() noexcept
{
    const auto aggregate = g_magicEventLedgerExhaustions.fetch_add(1, std::memory_order_relaxed) + 1;
    if (aggregate == 0 || (aggregate != 1 && aggregate % kLedgerExhaustionLogInterval != 0))
        return;
    try
    {
        spdlog::warn("Magic event ID ledger capacity exhausted; rejecting new source (capacity={}, aggregate={})",
                     kMaximumMagicEventSources, aggregate);
    }
    catch (...)
    {
    }
}

[[nodiscard]] bool IsOwnedCharacter(
    World& aWorld,
    const std::uint32_t aServerId,
    const Player* apPlayer) noexcept
{
    if (aServerId == 0 || !apPlayer)
        return false;
    const auto entity = static_cast<entt::entity>(aServerId);
    return aWorld.all_of<CharacterComponent, OwnerComponent>(entity) &&
           aWorld.get<OwnerComponent>(entity).GetOwner() == apPlayer;
}

}

std::uint32_t MagicService::NextMagicEventId(const std::uint32_t aActorId) noexcept
{
    if (aActorId == 0)
        return 0;
    auto it = m_magicEventIds.find(aActorId);
    if (it == m_magicEventIds.end()) {
        if (m_magicEventIds.size() >= kMaximumMagicEventSources) {
            LogMagicEventLedgerExhaustion();
            return 0;
        }
        it = m_magicEventIds.emplace(aActorId, 0).first;
    }
    ++it->second;
    if (it->second == 0)
        ++it->second;
    return it->second;
}

MagicService::MagicService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_spellCastConnection = aDispatcher.sink<PacketEvent<SpellCastRequest>>().connect<&MagicService::OnSpellCastRequest>(this);
    m_interruptCastConnection = aDispatcher.sink<PacketEvent<InterruptCastRequest>>().connect<&MagicService::OnInterruptCastRequest>(this);
    m_addTargetConnection = aDispatcher.sink<PacketEvent<AddTargetRequest>>().connect<&MagicService::OnAddTargetRequest>(this);
    m_removeSpellConnection = aDispatcher.sink<PacketEvent<RemoveSpellRequest>>().connect<&MagicService::OnRemoveSpellRequest>(this);
    m_characterRemoveConnection = aDispatcher.sink<CharacterRemoveEvent>().connect<&MagicService::OnCharacterRemove>(this);
}

void MagicService::OnSpellCastRequest(const PacketEvent<SpellCastRequest>& acMessage) noexcept
{
    auto& message = acMessage.Packet;
    if (!IsOwnedCharacter(m_world, message.CasterId, acMessage.pPlayer) ||
        message.CastingSource < 0 || message.CastingSource > 3 || !message.SpellFormId)
        return;

    NotifySpellCast notify;
    notify.CasterId = message.CasterId;
    notify.EventId = NextMagicEventId(message.CasterId);
    if (notify.EventId == 0)
        return;
    notify.SpellFormId = message.SpellFormId;
    notify.CastingSource = message.CastingSource;
    notify.IsDualCasting = message.IsDualCasting;
    notify.DesiredTarget = message.DesiredTarget;

    const auto entity = static_cast<entt::entity>(message.CasterId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnInterruptCastRequest(const PacketEvent<InterruptCastRequest>& acMessage) noexcept
{
    auto& message = acMessage.Packet;
    if (!IsOwnedCharacter(m_world, message.CasterId, acMessage.pPlayer) ||
        message.CastingSource < 0 || message.CastingSource > 3)
        return;

    NotifyInterruptCast notify;
    notify.CasterId = message.CasterId;
    notify.CastingSource = message.CastingSource;

    const auto entity = static_cast<entt::entity>(message.CasterId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnAddTargetRequest(const PacketEvent<AddTargetRequest>& acMessage) noexcept
{
    auto& message = acMessage.Packet;
    const bool ownsSource = message.CasterId != 0 ?
        IsOwnedCharacter(m_world, message.CasterId, acMessage.pPlayer) :
        IsOwnedCharacter(m_world, message.TargetId, acMessage.pPlayer);
    const auto target = static_cast<entt::entity>(message.TargetId);
    if (!ownsSource || message.TargetId == 0 ||
        !m_world.all_of<CharacterComponent>(target) || !message.SpellId || !message.EffectId ||
        !std::isfinite(message.Magnitude) || message.Magnitude < 0.0F || message.Magnitude > 1'000'000.0F)
        return;

    NotifyAddTarget notify;
    notify.TargetId = message.TargetId;
    notify.EventId = NextMagicEventId(message.TargetId);
    if (notify.EventId == 0)
        return;
    notify.CasterId = message.CasterId;
    notify.SpellId = message.SpellId;
    notify.EffectId = message.EffectId;
    notify.Magnitude = message.Magnitude;
    notify.IsDualCasting = message.IsDualCasting;
    notify.ApplyHealPerkBonus = message.ApplyHealPerkBonus;
    notify.ApplyStaminaPerkBonus = message.ApplyStaminaPerkBonus;

    const auto entity = static_cast<entt::entity>(message.TargetId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnRemoveSpellRequest(const PacketEvent<RemoveSpellRequest>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;
    if (!IsOwnedCharacter(m_world, message.TargetId, acMessage.pPlayer) || !message.SpellId)
        return;

    NotifyRemoveSpell notify;
    notify.TargetId = message.TargetId;
    notify.SpellId = message.SpellId;

    //spdlog::info(__FUNCTION__ ": TargetId: {}, Spell baseId: {}", notify.TargetId, notify.SpellId.BaseId);

    const auto entity = static_cast<entt::entity>(message.TargetId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnCharacterRemove(const CharacterRemoveEvent& acEvent) noexcept
{
    if (acEvent.ServerId != 0)
        m_magicEventIds.erase(acEvent.ServerId);
}
