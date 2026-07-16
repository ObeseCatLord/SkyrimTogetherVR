#include <Components.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/RequestDeathStateChange.h>
#include <Services/ActorValueService.h>
#include <World.h>
#include <GameServer.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyDeathStateChange.h>

#include <cmath>

namespace
{
constexpr std::uint32_t kHealthActorValue = 24;
constexpr std::uint32_t kDragonSoulsActorValue = 133;
constexpr std::uint32_t kActorValueCount = 164;
constexpr float kMaximumActorValueMagnitude = 1'000'000.0F;

[[nodiscard]] bool IsValidActorValue(const std::uint32_t aId, const float aValue) noexcept
{
    return aId < kActorValueCount && aId != kDragonSoulsActorValue && std::isfinite(aValue) &&
           std::abs(aValue) <= kMaximumActorValueMagnitude;
}

template <class TMap>
[[nodiscard]] TMap FilterActorValues(const TMap& acValues) noexcept
{
    TMap values{};
    if (acValues.size() > kActorValueCount)
        return values;

    for (const auto& [id, value] : acValues)
    {
        if (IsValidActorValue(id, value))
            values.emplace(id, value);
    }
    return values;
}
} // namespace

ActorValueService::ActorValueService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_updateHealthConnection = aDispatcher.sink<PacketEvent<RequestActorValueChanges>>().connect<&ActorValueService::OnActorValueChanges>(this);
    m_updateMaxValueConnection = aDispatcher.sink<PacketEvent<RequestActorMaxValueChanges>>().connect<&ActorValueService::OnActorMaxValueChanges>(this);
    m_updateDeltaHealthConnection = aDispatcher.sink<PacketEvent<RequestHealthChangeBroadcast>>().connect<&ActorValueService::OnHealthChangeBroadcast>(this);
    m_deathStateConnection = aDispatcher.sink<PacketEvent<RequestDeathStateChange>>().connect<&ActorValueService::OnDeathStateChange>(this);
}

void ActorValueService::OnActorValueChanges(const PacketEvent<RequestActorValueChanges>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    if (it == actorValuesView.end() || actorValuesView.get<OwnerComponent>(*it).GetOwner() != acMessage.pPlayer)
        return;

    auto values = FilterActorValues(message.Values);
    if (values.empty())
        return;

    auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
    for (const auto& [id, value] : values)
        actorValuesComponent.CurrentActorValues.ActorValuesList[id] = value;

    NotifyActorValueChanges notify;
    notify.Id = acMessage.Packet.Id;
    notify.Values = std::move(values);

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void ActorValueService::OnActorMaxValueChanges(const PacketEvent<RequestActorMaxValueChanges>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    if (it == actorValuesView.end() || actorValuesView.get<OwnerComponent>(*it).GetOwner() != acMessage.pPlayer)
        return;

    auto values = FilterActorValues(message.Values);
    if (values.empty())
        return;

    auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
    for (const auto& [id, value] : values)
        actorValuesComponent.CurrentActorValues.ActorMaxValuesList[id] = value;

    NotifyActorMaxValueChanges notify;
    notify.Id = message.Id;
    notify.Values = std::move(values);

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void ActorValueService::OnHealthChangeBroadcast(const PacketEvent<RequestHealthChangeBroadcast>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    // TODO(cosideci): should server side health not be updated?
    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    if (it == actorValuesView.end() || actorValuesView.get<OwnerComponent>(*it).GetOwner() != acMessage.pPlayer)
        return;

    if (!std::isfinite(message.DeltaHealth) || message.DeltaHealth == 0.0F ||
        std::abs(message.DeltaHealth) > kMaximumActorValueMagnitude)
        return;

    auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
    const auto currentHealth = actorValuesComponent.CurrentActorValues.ActorValuesList[kHealthActorValue];
    const auto newHealth = currentHealth + message.DeltaHealth;
    if (!std::isfinite(newHealth) || std::abs(newHealth) > kMaximumActorValueMagnitude)
        return;
    actorValuesComponent.CurrentActorValues.ActorValuesList[kHealthActorValue] = newHealth;

    NotifyHealthChangeBroadcast notify;
    notify.Id = message.Id;
    notify.DeltaHealth = message.DeltaHealth;

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void ActorValueService::OnDeathStateChange(const PacketEvent<RequestDeathStateChange>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto characterView = m_world.view<CharacterComponent, OwnerComponent>();

    const auto it = characterView.find(static_cast<entt::entity>(message.Id));

    if (it == characterView.end() || characterView.get<OwnerComponent>(*it).GetOwner() != acMessage.pPlayer)
        return;

    auto& characterComponent = characterView.get<CharacterComponent>(*it);
    characterComponent.SetDead(message.IsDead);
    spdlog::debug("Updating death state {:x}:{}", message.Id, message.IsDead);

    NotifyDeathStateChange notify;
    notify.Id = message.Id;
    notify.IsDead = message.IsDead;

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
