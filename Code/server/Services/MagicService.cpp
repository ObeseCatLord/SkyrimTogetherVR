#include <Services/MagicService.h>

#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Messages/SpellCastRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/AddTargetRequest.h>

#include <Messages/NotifySpellCast.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyRemoveSpell.h>

#include <cmath>

namespace
{
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

MagicService::MagicService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_spellCastConnection = aDispatcher.sink<PacketEvent<SpellCastRequest>>().connect<&MagicService::OnSpellCastRequest>(this);
    m_interruptCastConnection = aDispatcher.sink<PacketEvent<InterruptCastRequest>>().connect<&MagicService::OnInterruptCastRequest>(this);
    m_addTargetConnection = aDispatcher.sink<PacketEvent<AddTargetRequest>>().connect<&MagicService::OnAddTargetRequest>(this);
    m_removeSpellConnection = aDispatcher.sink<PacketEvent<RemoveSpellRequest>>().connect<&MagicService::OnRemoveSpellRequest>(this);
}

void MagicService::OnSpellCastRequest(const PacketEvent<SpellCastRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;
    if (!IsOwnedCharacter(m_world, message.CasterId, acMessage.pPlayer) ||
        message.CastingSource < 0 || message.CastingSource > 3 || !message.SpellFormId)
        return;

    NotifySpellCast notify;
    notify.CasterId = message.CasterId;
    notify.SpellFormId = message.SpellFormId;
    notify.CastingSource = message.CastingSource;
    notify.IsDualCasting = message.IsDualCasting;
    notify.DesiredTarget = message.DesiredTarget;

    const auto entity = static_cast<entt::entity>(message.CasterId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnInterruptCastRequest(const PacketEvent<InterruptCastRequest>& acMessage) const noexcept
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

void MagicService::OnAddTargetRequest(const PacketEvent<AddTargetRequest>& acMessage) const noexcept
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

void MagicService::OnRemoveSpellRequest(const PacketEvent<RemoveSpellRequest>& acMessage) const noexcept
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
