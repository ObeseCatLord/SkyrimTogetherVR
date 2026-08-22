#include <Services/VRPlanckPhysicsRelayService.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include <Components.h>
#include <Game/Player.h>
#include <GameServer.h>
#include <World.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyVRPlanckPhysicsEvent.h>
#include <Messages/RequestVRPlanckPhysicsEvent.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VRPlanckPhysicsEvent.h>

namespace
{
constexpr uint64_t kRateWindowMs = 1000;
constexpr uint64_t kGripLeaseCapMs = 5000;
constexpr uint32_t kMaximumEventsPerWindow = 64;
constexpr uint32_t kMaximumGripsPerPlayer = 8;
constexpr uint32_t kMaximumPlayerStates = 1024;
constexpr uint32_t kMaximumActorGripLeases = kMaximumPlayerStates * kMaximumGripsPerPlayer;
constexpr double kMaximumPhysicalInteractionDistance = 512.0;

constexpr std::array<const char*, 8> kRejectionNames{
    "unauthenticated producer", "invalid payload", "invalid physical target", "producer transition",
    "replayed event",           "rate limited",    "invalid grip lease",      "internal admission failure",
};

[[nodiscard]] bool IsGrip(const VRPlanckPhysicsEvent::Kind aKind) noexcept
{
    return aKind >= VRPlanckPhysicsEvent::Kind::GripBegin;
}

[[nodiscard]] bool IsGripBegin(const VRPlanckPhysicsEvent::Kind aKind) noexcept
{
    return aKind == VRPlanckPhysicsEvent::Kind::GripBegin;
}

[[nodiscard]] bool IsGripEnd(const VRPlanckPhysicsEvent::Kind aKind) noexcept
{
    return aKind == VRPlanckPhysicsEvent::Kind::GripEnd;
}

[[nodiscard]] bool IsPowerOfTwo(const uint64_t aValue) noexcept
{
    return aValue != 0 && (aValue & (aValue - 1)) == 0;
}

[[nodiscard]] bool IsFinitePosition(const glm::vec3& acPosition) noexcept
{
    return std::isfinite(acPosition.x) && std::isfinite(acPosition.y) && std::isfinite(acPosition.z);
}

[[nodiscard]] bool IsWithinPhysicalInteractionDistance(const MovementComponent& acSender, const MovementComponent& acTarget) noexcept
{
    if (!IsFinitePosition(acSender.Position) || !IsFinitePosition(acTarget.Position))
        return false;

    const auto x = static_cast<double>(acSender.Position.x) - static_cast<double>(acTarget.Position.x);
    const auto y = static_cast<double>(acSender.Position.y) - static_cast<double>(acTarget.Position.y);
    const auto z = static_cast<double>(acSender.Position.z) - static_cast<double>(acTarget.Position.z);
    const auto distanceSquared = x * x + y * y + z * z;
    return std::isfinite(distanceSquared) && distanceSquared <= kMaximumPhysicalInteractionDistance * kMaximumPhysicalInteractionDistance;
}

[[nodiscard]] bool ResolveCanonicalPhysicalTarget(World& arWorld, const Player& acPlayer, const GameId& acId, entt::entity& arEntity) noexcept
{
    const auto sender = acPlayer.GetCharacter();
    if (!sender || !arWorld.valid(*sender) || !arWorld.all_of<OwnerComponent, CharacterComponent, CellIdComponent, MovementComponent>(*sender) ||
        arWorld.get<OwnerComponent>(*sender).GetOwner() != &acPlayer || !arWorld.get<CharacterComponent>(*sender).IsPlayer())
        return false;

    const auto actors = arWorld.view<FormIdComponent, CharacterComponent, CellIdComponent, MovementComponent>();
    const auto actor = std::find_if(actors.begin(), actors.end(), [&actors, &acId](const entt::entity aEntity) { return actors.get<FormIdComponent>(aEntity).Id == acId; });
    if (actor == actors.end() || *actor == *sender)
        return false;

    arEntity = *actor;
    const auto& senderCell = arWorld.get<CellIdComponent>(*sender);
    const auto& targetCell = actors.get<CellIdComponent>(arEntity);
    const auto& targetCharacter = actors.get<CharacterComponent>(arEntity);
    if (!acPlayer.GetCellComponent().IsInRange(senderCell, false) || !acPlayer.GetCellComponent().IsInRange(targetCell, targetCharacter.IsDragon()) ||
        !senderCell.IsInRange(targetCell, targetCharacter.IsDragon()) ||
        (targetCharacter.IsPlayer() && !SkyrimTogether::PlanckPhysicsPolicy::CanRoutePlayerTarget(GameServer::Get()->IsPvpEnabled())))
        return false;

    return IsWithinPhysicalInteractionDistance(arWorld.get<MovementComponent>(*sender), actors.get<MovementComponent>(arEntity));
}

} // namespace

VRPlanckPhysicsRelayService::VRPlanckPhysicsRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_eventConnection(aDispatcher.sink<PacketEvent<RequestVRPlanckPhysicsEvent>>().connect<&VRPlanckPhysicsRelayService::OnEvent>(this))
    , m_leaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRPlanckPhysicsRelayService::OnPlayerLeave>(this))
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&VRPlanckPhysicsRelayService::OnUpdate>(this))
{
    Reset();
}

VRPlanckPhysicsRelayService::~VRPlanckPhysicsRelayService() noexcept
{
    Reset();
}

// Keep the comparison beside the relay state rather than treating a client
// epoch as an authority transition. Player identity, transport connection,
// authenticated generation, and producer session must all remain unchanged.
bool VRPlanckPhysicsRelayService::MatchesLease(const GripLease& acLease, const Player& acPlayer, const uint64_t aProducerEpoch, const uint64_t aGripId) noexcept
{
    return acLease.OwnerPlayerId == acPlayer.GetId() && acLease.OwnerConnection == acPlayer.GetConnectionId() &&
           acLease.OwnerConnectionGeneration == acPlayer.GetConnectionGeneration() && acLease.ProducerEpoch == aProducerEpoch &&
           acLease.ProducerSession == acPlayer.GetClientSessionNonce() && acLease.GripId == aGripId;
}

void VRPlanckPhysicsRelayService::Reject(const RejectionReason aReason) noexcept
{
    const auto index = static_cast<std::size_t>(aReason);
    if (index >= m_rejectionCounts.size() || m_rejectionCounts[index] == (std::numeric_limits<uint64_t>::max)())
        return;

    const auto count = ++m_rejectionCounts[index];
    if (!IsPowerOfTwo(count))
        return;

    try
    {
        spdlog::warn("PLANCK physics relay rejected {} packets: {}", count, kRejectionNames[index]);
    }
    catch (...)
    {
    }
}

void VRPlanckPhysicsRelayService::Expire(const uint64_t aNow) noexcept
{
    while (!m_gripExpiryHeap.empty() && m_gripExpiryHeap.front().ExpiryTick <= aNow)
    {
        std::pop_heap(m_gripExpiryHeap.begin(), m_gripExpiryHeap.end(), IsLaterExpiry);
        const auto expired = m_gripExpiryHeap.back();
        m_gripExpiryHeap.pop_back();

        const auto lease = m_actorGrips.find(expired.Target);
        if (lease == m_actorGrips.end() || lease->second.ExpiryRevision != expired.Revision || lease->second.ExpiryTick > aNow)
            continue;

        auto state = m_states.find(lease->second.OwnerPlayerId);
        if (state != m_states.end())
        {
            state.value().GripTargets.erase(lease->second.GripId);
            if (state.value().ActiveGripCount != 0)
                --state.value().ActiveGripCount;
        }
        m_actorGrips.erase(lease);
    }
}

void VRPlanckPhysicsRelayService::ReserveExpiryRecord()
{
    if (m_gripExpiryHeap.size() < m_gripExpiryHeap.capacity())
        return;

    const auto maximum = m_gripExpiryHeap.max_size();
    if (m_gripExpiryHeap.size() == maximum)
        throw std::length_error("PLANCK grip expiry heap capacity exhausted");
    const auto doubled = m_gripExpiryHeap.capacity() <= maximum / 2 ? m_gripExpiryHeap.capacity() * 2 : maximum;
    m_gripExpiryHeap.reserve(std::max<std::size_t>(1024, doubled));
}

void VRPlanckPhysicsRelayService::CommitExpiryRecord(const GameId& acTarget, const GripLease& acLease) noexcept
{
    m_gripExpiryHeap.push_back({acTarget, acLease.ExpiryTick, acLease.ExpiryRevision});
    std::push_heap(m_gripExpiryHeap.begin(), m_gripExpiryHeap.end(), IsLaterExpiry);
}

bool VRPlanckPhysicsRelayService::IsLaterExpiry(const GripExpiryRecord& acLeft, const GripExpiryRecord& acRight) noexcept
{
    return acLeft.ExpiryTick > acRight.ExpiryTick;
}

uint64_t VRPlanckPhysicsRelayService::NextLeaseRevision(const uint64_t aRevision) noexcept
{
    return aRevision == (std::numeric_limits<uint64_t>::max)() ? 1 : aRevision + 1;
}

VRPlanckPhysicsRelayService::ProvisionalGripLeaseRollback::ProvisionalGripLeaseRollback(VRPlanckPhysicsRelayService& arService) noexcept
    : Service(arService)
{
}

VRPlanckPhysicsRelayService::ProvisionalGripLeaseRollback::~ProvisionalGripLeaseRollback() noexcept
{
    if (Armed)
        Service.m_actorGrips.erase(Target);
}

void VRPlanckPhysicsRelayService::ProvisionalGripLeaseRollback::Arm(const GameId& acTarget) noexcept
{
    Target = acTarget;
    Armed = true;
}

void VRPlanckPhysicsRelayService::ProvisionalGripLeaseRollback::Disarm() noexcept
{
    Armed = false;
}

void VRPlanckPhysicsRelayService::ReleasePlayerLeases(const uint32_t aPlayerId) noexcept
{
    for (auto it = m_actorGrips.begin(); it != m_actorGrips.end();)
    {
        if (it->second.OwnerPlayerId == aPlayerId)
            it = m_actorGrips.erase(it);
        else
            ++it;
    }
    if (auto state = m_states.find(aPlayerId); state != m_states.end())
    {
        state.value().GripTargets.clear();
        state.value().ActiveGripCount = 0;
    }
}

void VRPlanckPhysicsRelayService::Reset() noexcept
{
    m_states.clear();
    m_actorGrips.clear();
    m_gripExpiryHeap.clear();
    m_nextGripLeaseRevision = 1;
}

void VRPlanckPhysicsRelayService::OnEvent(const PacketEvent<RequestVRPlanckPhysicsEvent>& acMessage) noexcept
try
{
    if (!acMessage.pPlayer ||
        !SkyrimTogether::Protocol::HasCapability(acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::PlanckPhysicsInterface002))
    {
        Reject(RejectionReason::UnauthenticatedProducer);
        return;
    }

    const auto& event = acMessage.Packet.Event;
    if (!event.IsValid())
    {
        Reject(RejectionReason::InvalidPayload);
        return;
    }

    const auto playerId = acMessage.pPlayer->GetId();
    const auto connectionGeneration = acMessage.pPlayer->GetConnectionGeneration();
    const auto clientSessionNonce = acMessage.pPlayer->GetClientSessionNonce();
    if (playerId == 0 || connectionGeneration == 0 || clientSessionNonce == 0)
    {
        Reject(RejectionReason::UnauthenticatedProducer);
        return;
    }

    entt::entity target{};
    if (!ResolveCanonicalPhysicalTarget(m_world, *acMessage.pPlayer, event.TargetActorId, target))
    {
        Reject(RejectionReason::InvalidPhysicalTarget);
        return;
    }

    const auto now = GameServer::Get()->GetTick();
    // Expiry follows server time and is independent of recipient fanout.
    Expire(now);

    auto stateIt = m_states.find(playerId);
    if (stateIt == m_states.end())
    {
        if (m_states.size() >= kMaximumPlayerStates)
        {
            Reject(RejectionReason::UnauthenticatedProducer);
            return;
        }
        // Reserve storage before fanout so the eventual semantic commit is a
        // trivial assignment. The default slot has no bound producer, replay
        // cursor, or rate accounting and therefore does not consume the event.
        stateIt = m_states.try_emplace(playerId).first;
    }

    if (stateIt->second.HasProducer &&
        (stateIt->second.OwnerConnection != acMessage.pPlayer->GetConnectionId() ||
         stateIt->second.ConnectionGeneration != connectionGeneration ||
         stateIt->second.ClientSessionNonce != clientSessionNonce))
    {
        // A server-authenticated lifecycle transition retires all producer
        // state. Do not let the first packet of a replacement connection
        // inherit a predecessor's rate or grip authority.
        ReleasePlayerLeases(playerId);
        m_states.erase(stateIt);
        // The replacement packet is authenticated by the current Player and
        // must become the first event of its new producer state.  Returning
        // here silently lost that valid first event after clearing stale data.
        stateIt = m_states.try_emplace(playerId).first;
    }
    auto& state = stateIt.value();
    if (state.HasProducer && state.ProducerEpoch != event.ProducerEpoch)
    {
        Reject(RejectionReason::ProducerTransition);
        return;
    }

    auto nextState = state;
    if (!nextState.HasProducer)
    {
        nextState.ProducerEpoch = event.ProducerEpoch;
        nextState.OwnerConnection = acMessage.pPlayer->GetConnectionId();
        nextState.ConnectionGeneration = connectionGeneration;
        nextState.ClientSessionNonce = clientSessionNonce;
        nextState.HasProducer = true;
    }
    if (!SkyrimTogether::PlanckPhysicsPolicy::IsStrictlyNewEvent(event.EventId, nextState.LastEventId, nextState.HasEvent))
    {
        Reject(RejectionReason::ReplayedEvent);
        return;
    }
    if (nextState.RateWindowTick == 0 || now < nextState.RateWindowTick || now - nextState.RateWindowTick >= kRateWindowMs)
    {
        nextState.RateWindowTick = now;
        nextState.RateCount = 0;
    }
    if (nextState.RateCount >= kMaximumEventsPerWindow)
    {
        Reject(RejectionReason::RateLimited);
        return;
    }

    enum class LeaseMutation : uint8_t
    {
        None,
        Begin,
        Update,
        Release,
    };
    LeaseMutation leaseMutation{LeaseMutation::None};
    uint64_t stagedExpiry{};
    uint64_t stagedRevision{};
    ProvisionalGripLeaseRollback provisionalLease(*this);

    if (IsGrip(event.EventKind))
    {
        const auto existing = m_actorGrips.find(event.TargetActorId);
        if (IsGripBegin(event.EventKind))
        {
            if (nextState.GripTargets.contains(event.GripId) || existing != m_actorGrips.end() ||
                nextState.ActiveGripCount >= kMaximumGripsPerPlayer || m_actorGrips.size() >= kMaximumActorGripLeases)
            {
                Reject(RejectionReason::GripLease);
                return;
            }
            ReserveExpiryRecord();
            stagedExpiry = now + std::min<uint64_t>(kGripLeaseCapMs, static_cast<uint64_t>(event.TtlSeconds * 1000.0F));
            stagedRevision = m_nextGripLeaseRevision;
            nextState.GripTargets.emplace(event.GripId, event.TargetActorId);
            const auto [_, inserted] = m_actorGrips.emplace(
                event.TargetActorId,
                GripLease{
                    playerId, acMessage.pPlayer->GetConnectionId(), connectionGeneration, event.ProducerEpoch, clientSessionNonce, event.GripId, stagedExpiry, stagedRevision});
            if (!inserted)
            {
                Reject(RejectionReason::GripLease);
                return;
            }
            provisionalLease.Arm(event.TargetActorId);
            leaseMutation = LeaseMutation::Begin;
        }
        else if (const auto indexedTarget = nextState.GripTargets.find(event.GripId);
                 indexedTarget == nextState.GripTargets.end() || indexedTarget->second != event.TargetActorId ||
                 existing == m_actorGrips.end() ||
                 !MatchesLease(existing->second, *acMessage.pPlayer, event.ProducerEpoch, event.GripId))
        {
            Reject(RejectionReason::GripLease);
            return;
        }
        else if (IsGripEnd(event.EventKind))
        {
            leaseMutation = LeaseMutation::Release;
        }
        else
        {
            ReserveExpiryRecord();
            stagedExpiry = now + std::min<uint64_t>(kGripLeaseCapMs, static_cast<uint64_t>(event.TtlSeconds * 1000.0F));
            stagedRevision = m_nextGripLeaseRevision;
            leaseMutation = LeaseMutation::Update;
        }
    }
    else if (event.EventKind == VRPlanckPhysicsEvent::Kind::RagdollExit)
    {
        const auto existing = m_actorGrips.find(event.TargetActorId);
        if (existing != m_actorGrips.end() &&
            nextState.GripTargets.find(existing->second.GripId) != nextState.GripTargets.end() &&
            MatchesLease(existing->second, *acMessage.pPlayer, event.ProducerEpoch, existing->second.GripId))
            leaseMutation = LeaseMutation::Release;
    }

    // Replay and rate mutations remain local until routing admits the event.
    nextState.LastEventId = event.EventId;
    nextState.HasEvent = true;
    ++nextState.RateCount;

    NotifyVRPlanckPhysicsEvent notify{};
    notify.PlayerId = playerId;
    notify.Event = event;
    const auto sender = acMessage.pPlayer->GetCharacter();
    if (!GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
            notify, *sender, SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::PlanckPhysicsInterface002), acMessage.pPlayer))
    {
        if (m_noRoutableCharacterCount != (std::numeric_limits<uint64_t>::max)())
            ++m_noRoutableCharacterCount;
        if (IsPowerOfTwo(m_noRoutableCharacterCount))
        {
            spdlog::warn("PLANCK physics relay dropped {} packets without an eligible recipient in sender range", m_noRoutableCharacterCount);
        }
        return;
    }

    // The dispatcher owns relay events on one server thread. Every allocation
    // was reserved before fanout; these targeted commits cannot interleave with
    // a competing producer or consume EventId after an allocation failure.
    if (leaseMutation == LeaseMutation::Begin)
    {
        const auto lease = m_actorGrips.find(event.TargetActorId);
        CommitExpiryRecord(event.TargetActorId, lease->second);
        ++nextState.ActiveGripCount;
        m_nextGripLeaseRevision = NextLeaseRevision(stagedRevision);
    }
    else if (leaseMutation == LeaseMutation::Update)
    {
        auto& lease = m_actorGrips.find(event.TargetActorId).value();
        lease.ExpiryTick = stagedExpiry;
        lease.ExpiryRevision = stagedRevision;
        CommitExpiryRecord(event.TargetActorId, lease);
        m_nextGripLeaseRevision = NextLeaseRevision(stagedRevision);
    }
    else if (leaseMutation == LeaseMutation::Release)
    {
        const auto gripId = m_actorGrips.find(event.TargetActorId)->second.GripId;
        m_actorGrips.erase(event.TargetActorId);
        nextState.GripTargets.erase(gripId);
        if (nextState.ActiveGripCount != 0)
            --nextState.ActiveGripCount;
    }
    state = std::move(nextState);
    provisionalLease.Disarm();
}
catch (...)
{
    Reject(RejectionReason::InternalFailure);
}

void VRPlanckPhysicsRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
    {
        ReleasePlayerLeases(acEvent.pPlayer->GetId());
        m_states.erase(acEvent.pPlayer->GetId());
    }
}

void VRPlanckPhysicsRelayService::OnUpdate(const UpdateEvent&) noexcept
{
    Expire(GameServer::Get()->GetTick());
}
