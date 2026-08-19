#include <Components.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/RequestDeathStateChange.h>
#include <Services/ActorValueService.h>
#include <server/Services/HealthChangePolicy.h>
#include <server/Services/ServerAuthorityPolicy.h>
#include <World.h>
#include <GameServer.h>
#include <Events/CharacterRemoveEvent.h>
#include <Events/PlayerLeaveEvent.h>
#include <Game/Player.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Structs/GameplayCapabilities.h>

#include <array>
#include <atomic>
#include <cmath>

namespace
{
constexpr std::uint32_t kHealthActorValue = 24;
constexpr std::uint32_t kDragonSoulsActorValue = 133;
constexpr std::uint32_t kActorValueCount = 164;
constexpr float kMaximumActorValueMagnitude = 1'000'000.0F;
constexpr std::size_t kMaximumTrackedVRHealthSenders = 1024;
constexpr std::size_t kMaximumHealthEventSources = 4096;
constexpr std::uint64_t kHealthChangeRejectionLogInterval = 128;
constexpr std::uint64_t kLedgerExhaustionLogInterval = 128;

enum class HealthChangeRejection : std::size_t
{
    MissingSender,
    InvalidTarget,
    InvalidLegacyShape,
    InvalidVRShape,
    InvalidVRActorAuthority,
    VRTargetNotNpc,
    VRTargetOutOfRange,
    InvalidDelta,
    MissingCanonicalHealth,
    ReplayedAction,
    ReplayLedgerFull,
    Count,
};

constexpr std::array<const char*, static_cast<std::size_t>(HealthChangeRejection::Count)> kHealthChangeRejectionNames{
    "missing sender",
    "invalid target",
    "invalid desktop legacy shape",
    "invalid VR request shape",
    "unowned or invalid VR attacker",
    "VR target is a player",
    "VR target is out of attacker range",
    "invalid health delta",
    "missing canonical health",
    "replayed VR health action",
    "VR health replay ledger is full",
};

std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(HealthChangeRejection::Count)> g_healthChangeRejections{};
std::atomic<std::uint64_t> g_healthEventLedgerExhaustions{};
std::atomic<std::uint64_t> g_healthEventLedgerReservationFailures{};
std::atomic<std::uint64_t> g_healthChangeFailures{};

void LogHealthChangeRejection(const HealthChangeRejection aReason) noexcept
{
    const auto index = static_cast<std::size_t>(aReason);
    const auto aggregate = g_healthChangeRejections[index].fetch_add(1, std::memory_order_relaxed) + 1;
    if (aggregate == 1 || aggregate % kHealthChangeRejectionLogInterval == 0)
    {
        try
        {
            spdlog::warn("Health-change request rejected: {} (aggregate={})", kHealthChangeRejectionNames[index], aggregate);
        }
        catch (...)
        {
        }
    }
}

void LogHealthEventLedgerExhaustion() noexcept
{
    const auto aggregate = g_healthEventLedgerExhaustions.fetch_add(1, std::memory_order_relaxed) + 1;
    if (aggregate == 0 || (aggregate != 1 && aggregate % kLedgerExhaustionLogInterval != 0))
        return;
    try
    {
        spdlog::warn("Health event ID ledger capacity exhausted; rejecting new source (capacity={}, aggregate={})",
                     kMaximumHealthEventSources, aggregate);
    }
    catch (...)
    {
    }
}

void LogHealthEventLedgerReservationFailure() noexcept
{
    const auto aggregate = g_healthEventLedgerReservationFailures.fetch_add(1, std::memory_order_relaxed) + 1;
    if (aggregate == 0 || (aggregate != 1 && aggregate % kLedgerExhaustionLogInterval != 0))
        return;
    try
    {
        spdlog::warn("Health event ID ledger reservation failed; rejecting mutation (aggregate={})", aggregate);
    }
    catch (...)
    {
    }
}

void LogHealthChangeFailure() noexcept
{
    const auto aggregate = g_healthChangeFailures.fetch_add(1, std::memory_order_relaxed) + 1;
    if (aggregate == 0 || (aggregate != 1 && aggregate % kHealthChangeRejectionLogInterval != 0))
        return;
    try
    {
        spdlog::warn("Health-change request rejected after an allocation or fanout failure (aggregate={})", aggregate);
    }
    catch (...)
    {
    }
}

[[nodiscard]] bool IsValidActorValue(const std::uint32_t aId, const float aValue) noexcept
{
    return aId < kActorValueCount && aId != kDragonSoulsActorValue && std::isfinite(aValue) &&
           std::abs(aValue) <= kMaximumActorValueMagnitude;
}

template <class TMap>
[[nodiscard]] TMap FilterActorValues(const TMap& acValues)
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
    m_playerLeaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&ActorValueService::OnPlayerLeave>(this);
    m_characterRemoveConnection = aDispatcher.sink<CharacterRemoveEvent>().connect<&ActorValueService::OnCharacterRemove>(this);
}

std::uint32_t ActorValueService::NextHealthEventId(const std::uint32_t aActorId) noexcept
{
    try
    {
        if (aActorId == 0)
            return 0;
        auto it = m_healthEventIds.find(aActorId);
        if (it == m_healthEventIds.end()) {
            if (m_healthEventIds.size() >= kMaximumHealthEventSources) {
                LogHealthEventLedgerExhaustion();
                return 0;
            }
            it = m_healthEventIds.emplace(aActorId, 0).first;
        }
        ++it->second;
        if (it->second == 0)
            ++it->second;
        return it->second;
    }
    catch (...)
    {
        LogHealthEventLedgerReservationFailure();
        return 0;
    }
}

void ActorValueService::OnActorValueChanges(const PacketEvent<RequestActorValueChanges>& acMessage) const noexcept try
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
catch (...)
{
    spdlog::error("Actor-value update rejected after an allocation or fanout failure");
}

void ActorValueService::OnActorMaxValueChanges(const PacketEvent<RequestActorMaxValueChanges>& acMessage) const noexcept try
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
catch (...)
{
    spdlog::error("Actor max-value update rejected after an allocation or fanout failure");
}

void ActorValueService::OnHealthChangeBroadcast(const PacketEvent<RequestHealthChangeBroadcast>& acMessage) noexcept try
{
    auto* const sender = acMessage.pPlayer;
    if (!sender)
    {
        LogHealthChangeRejection(HealthChangeRejection::MissingSender);
        return;
    }

    auto& message = acMessage.Packet;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent, CharacterComponent, CellIdComponent>();

    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    if (it == actorValuesView.end())
    {
        LogHealthChangeRejection(HealthChangeRejection::InvalidTarget);
        return;
    }

    const auto& owner = actorValuesView.get<OwnerComponent>(*it);
    const auto& targetCharacter = actorValuesView.get<CharacterComponent>(*it);
    const auto& targetCell = actorValuesView.get<CellIdComponent>(*it);
    const bool senderIsVR = SkyrimTogether::Protocol::IsVrClient(sender->GetGameplayCapabilities());
    if (!senderIsVR)
    {
        // Desktop producers retain the legacy target/delta request shape and
        // existing target-owner-or-interest-range authority policy.
        if (message.AttackerId != 0 || message.ActionNonce != 0)
        {
            LogHealthChangeRejection(HealthChangeRejection::InvalidLegacyShape);
            return;
        }
        if (owner.GetOwner() != sender && !sender->GetCellComponent().IsInRange(targetCell, targetCharacter.IsDragon()))
        {
            LogHealthChangeRejection(HealthChangeRejection::InvalidTarget);
            return;
        }
        if (!std::isfinite(message.DeltaHealth) || message.DeltaHealth == 0.0F ||
            std::abs(message.DeltaHealth) > kMaximumActorValueMagnitude)
        {
            LogHealthChangeRejection(HealthChangeRejection::InvalidDelta);
            return;
        }
    }
    else
    {
        const auto lane = SkyrimTogether::HealthChangePolicy::ClassifyVrRequest(
            SkyrimTogether::Protocol::IsVrGameplayClient(sender->GetGameplayCapabilities()),
            owner.GetOwner() == sender,
            message.AttackerId,
            message.ActionNonce);
        if (lane == SkyrimTogether::HealthChangePolicy::VrRequestLane::Reject)
        {
            LogHealthChangeRejection(HealthChangeRejection::InvalidVRShape);
            return;
        }

        if (!std::isfinite(message.DeltaHealth) || message.DeltaHealth == 0.0F ||
            std::abs(message.DeltaHealth) > kMaximumActorValueMagnitude)
        {
            LogHealthChangeRejection(HealthChangeRejection::InvalidDelta);
            return;
        }

        // Ordinary local health capture is authoritative for the sender's
        // own player entity and deliberately carries no attacker fields.
        // Keep it separate from the non-owner physical-damage lane below.
        if (lane == SkyrimTogether::HealthChangePolicy::VrRequestLane::OwnerState)
        {
            auto& actorValues = actorValuesView.get<ActorValuesComponent>(*it).CurrentActorValues.ActorValuesList;
            const auto healthIt = actorValues.find(kHealthActorValue);
            const auto currentHealth = healthIt != actorValues.end() ? healthIt->second : 0.0F;
            const auto newHealth = currentHealth + message.DeltaHealth;
            if (!std::isfinite(newHealth) || std::abs(newHealth) > kMaximumActorValueMagnitude)
            {
                LogHealthChangeRejection(HealthChangeRejection::InvalidDelta);
                return;
            }
            const auto eventId = NextHealthEventId(message.Id);
            if (eventId == 0)
                return;
            if (healthIt == actorValues.end())
                actorValues.emplace(kHealthActorValue, newHealth);
            else
                healthIt.value() = newHealth;

            NotifyHealthChangeBroadcast notify;
            notify.Id = message.Id;
            notify.EventId = eventId;
            notify.DeltaHealth = message.DeltaHealth;
            if (!GameServer::Get()->SendToPlayersInRange(notify, static_cast<entt::entity>(message.Id), sender))
                spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
            return;
        }

        // Physical damage against a non-owned NPC requires an authenticated
        // sender-owned player and a process-monotonic bridge action nonce.
        const auto attackers = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent>();
        const auto attacker = attackers.find(static_cast<entt::entity>(message.AttackerId));
        if (attacker == attackers.end() || attackers.get<OwnerComponent>(*attacker).GetOwner() != sender ||
            !attackers.get<CharacterComponent>(*attacker).IsPlayer())
        {
            LogHealthChangeRejection(HealthChangeRejection::InvalidVRActorAuthority);
            return;
        }
        if (targetCharacter.IsPlayer())
        {
            // This exact DoDamage publication route cannot target players, so
            // server PvP policy is not applicable to this request type.
            LogHealthChangeRejection(HealthChangeRejection::VRTargetNotNpc);
            return;
        }
        if (!attackers.get<CellIdComponent>(*attacker).IsInRange(targetCell, targetCharacter.IsDragon()))
        {
            LogHealthChangeRejection(HealthChangeRejection::VRTargetOutOfRange);
            return;
        }
        if (message.DeltaHealth >= 0.0F)
        {
            LogHealthChangeRejection(HealthChangeRejection::InvalidDelta);
            return;
        }

        const auto playerId = sender->GetId();
        const auto sessionNonce = sender->GetClientSessionNonce();
        const auto connectionGeneration = sender->GetConnectionGeneration();
        if (playerId == 0 || sessionNonce == 0 || connectionGeneration == 0)
        {
            LogHealthChangeRejection(HealthChangeRejection::InvalidVRActorAuthority);
            return;
        }

        auto nonceIt = m_vrHealthActionNonces.find(playerId);
        const bool resetNonceDomain = nonceIt != m_vrHealthActionNonces.end() &&
                                      (nonceIt->second.ClientSessionNonce != sessionNonce ||
                                       nonceIt->second.ConnectionGeneration != connectionGeneration);
        if (nonceIt != m_vrHealthActionNonces.end() && !resetNonceDomain &&
            message.ActionNonce <= nonceIt->second.LastActionNonce)
        {
            LogHealthChangeRejection(HealthChangeRejection::ReplayedAction);
            return;
        }
        if (nonceIt == m_vrHealthActionNonces.end())
        {
            if (m_vrHealthActionNonces.size() >= kMaximumTrackedVRHealthSenders)
            {
                LogHealthChangeRejection(HealthChangeRejection::ReplayLedgerFull);
                return;
            }
        }

        // The assigned actor-value map may not yet contain health. Validate
        // before changing it. Reserve the event before mutating either the
        // canonical health or the replay ledger.
        auto& actorValues = actorValuesView.get<ActorValuesComponent>(*it).CurrentActorValues.ActorValuesList;
        auto healthIt = actorValues.find(kHealthActorValue);
        if (healthIt == actorValues.end())
        {
            // A delta has no safe base without a canonical owner snapshot.
            // Do not manufacture zero or poison the replay ledger; the owner
            // must resynchronize its authoritative actor state first.
            LogHealthChangeRejection(HealthChangeRejection::MissingCanonicalHealth);
            return;
        }
        const auto currentHealth = healthIt->second;
        const auto newHealth = currentHealth + message.DeltaHealth;
        if (!std::isfinite(newHealth) || std::abs(newHealth) > kMaximumActorValueMagnitude)
        {
            LogHealthChangeRejection(HealthChangeRejection::InvalidDelta);
            return;
        }
        const auto eventId = NextHealthEventId(message.Id);
        if (eventId == 0)
            return;
        if (nonceIt == m_vrHealthActionNonces.end())
            nonceIt = m_vrHealthActionNonces.emplace(
                playerId, HealthActionNonceState{sessionNonce, connectionGeneration, 0}).first;
        healthIt.value() = newHealth;
        if (resetNonceDomain)
            nonceIt->second = {sessionNonce, connectionGeneration, 0};
        nonceIt->second.LastActionNonce = message.ActionNonce;

        NotifyHealthChangeBroadcast notify;
        notify.Id = message.Id;
        notify.EventId = eventId;
        notify.DeltaHealth = message.DeltaHealth;

        const entt::entity cEntity = static_cast<entt::entity>(message.Id);
        if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, sender))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
        return;
    }

    auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
    auto& actorValues = actorValuesComponent.CurrentActorValues.ActorValuesList;
    const auto healthIt = actorValues.find(kHealthActorValue);
    const auto currentHealth = healthIt != actorValues.end() ? healthIt->second : 0.0F;
    const auto newHealth = currentHealth + message.DeltaHealth;
    if (!std::isfinite(newHealth) || std::abs(newHealth) > kMaximumActorValueMagnitude)
        return;

    NotifyHealthChangeBroadcast notify;
    notify.Id = message.Id;
    notify.EventId = NextHealthEventId(message.Id);
    if (!SkyrimTogether::ServerAuthorityPolicy::CanCommitEventMutation(notify.EventId))
        return;
    notify.DeltaHealth = message.DeltaHealth;

    // Reserve the protocol event before creating or updating canonical health.
    actorValues[kHealthActorValue] = newHealth;

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
catch (...)
{
    LogHealthChangeFailure();
}

void ActorValueService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_vrHealthActionNonces.erase(acEvent.pPlayer->GetId());
}

void ActorValueService::OnCharacterRemove(const CharacterRemoveEvent& acEvent) noexcept
{
    if (acEvent.ServerId != 0)
        m_healthEventIds.erase(acEvent.ServerId);
}

void ActorValueService::OnDeathStateChange(const PacketEvent<RequestDeathStateChange>& acMessage) const noexcept try
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
catch (...)
{
    spdlog::error("Death-state update rejected after an allocation or fanout failure");
}
