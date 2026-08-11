#include <Services/VRGrabRelayService.h>

#include <cstddef>
#include <utility>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyVRGrabEvent.h>
#include <Messages/RequestVRGrabEvent.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VRGrabEvent.h>
#include <Structs/VRInteractionValidation.h>

namespace
{
constexpr uint64_t kObjectAuthorityLeaseDurationMs = 1000;
constexpr size_t kMaxObjectAuthorityLeases = 1024;

TiltedPhoques::Map<GameId, VRObjectAuthority::Lease> s_objectAuthorityLeases{};

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasGrabObject(const VRGrabEvent& acGrab) noexcept
{
    return static_cast<bool>(acGrab.ObjectId);
}

bool IsLeaseExpired(const VRObjectAuthority::Lease& acLease, uint64_t aTick) noexcept
{
    return aTick >= acLease.ExpiryTick;
}

void ExpireLeases(TiltedPhoques::Map<GameId, VRObjectAuthority::Lease>& arLeases,
                  const uint64_t aTick) noexcept
{
    auto it = arLeases.begin();
    while (it != arLeases.end())
    {
        if (IsLeaseExpired(it->second, aTick))
            it = arLeases.erase(it);
        else
            ++it;
    }
}

bool ApplyOperation(TiltedPhoques::Map<GameId, VRObjectAuthority::Lease>& arLeases,
                    const VRObjectAuthority::Operation& acOperation,
                    const uint32_t aPlayerId, const uint64_t aTick)
{
    if (!acOperation.ObjectId || aPlayerId == 0)
        return false;

    const auto it = arLeases.find(acOperation.ObjectId);
    if (acOperation.Kind == VRObjectAuthority::OperationKind::Release)
    {
        if (it == arLeases.end() || it->second.OwnerPlayerId != aPlayerId)
            return false;
        arLeases.erase(it);
        return true;
    }

    if (acOperation.Kind == VRObjectAuthority::OperationKind::RenewExisting)
    {
        if (it == arLeases.end() || it->second.OwnerPlayerId != aPlayerId)
            return false;
        it->second.ExpiryTick = aTick + kObjectAuthorityLeaseDurationMs;
        return true;
    }

    if (it != arLeases.end())
    {
        if (it->second.OwnerPlayerId != aPlayerId)
            return false;
        it->second.ExpiryTick = aTick + kObjectAuthorityLeaseDurationMs;
        return true;
    }

    if (arLeases.size() >= kMaxObjectAuthorityLeases)
        return false;

    return arLeases.emplace(
        acOperation.ObjectId, VRObjectAuthority::Lease{aPlayerId, aTick + kObjectAuthorityLeaseDurationMs}).second;
}
}

bool VRObjectAuthority::PrepareBatch(Batch& arBatch, const Operation* const apOperations,
                                     const std::size_t aOperationCount, const uint32_t aPlayerId,
                                     const uint64_t aTick) noexcept
{
    if ((aOperationCount != 0 && !apOperations) || aPlayerId == 0)
        return false;

    if (!BeginBatch(arBatch, aTick))
        return false;

    for (std::size_t index = 0; index < aOperationCount; ++index)
    {
        if (!TryApplyOperation(arBatch, apOperations[index], aPlayerId, aTick))
        {
            arBatch = {};
            return false;
        }
    }
    return true;
}

bool VRObjectAuthority::BeginBatch(Batch& arBatch, const uint64_t aTick) noexcept
{
    try
    {
        auto shadow = s_objectAuthorityLeases;
        ExpireLeases(shadow, aTick);
        arBatch.Leases.swap(shadow);
        arBatch.Prepared = true;
        return true;
    }
    catch (...)
    {
        arBatch = {};
        return false;
    }
}

bool VRObjectAuthority::TryApplyOperation(Batch& arBatch, const Operation& acOperation,
                                           const uint32_t aPlayerId, const uint64_t aTick) noexcept
{
    if (!arBatch.Prepared)
        return false;

    try
    {
        return ApplyOperation(arBatch.Leases, acOperation, aPlayerId, aTick);
    }
    catch (...)
    {
        return false;
    }
}

bool VRObjectAuthority::CommitBatch(Batch&& arBatch) noexcept
{
    if (!arBatch.Prepared)
        return false;

    s_objectAuthorityLeases.swap(arBatch.Leases);
    arBatch.Prepared = false;
    return true;
}

bool VRObjectAuthority::AcquireOrRenew(const GameId& acObjectId, const uint32_t aPlayerId, const uint64_t aTick)
{
    const Operation operation{acObjectId, OperationKind::AcquireOrRenew};
    Batch batch{};
    return PrepareBatch(batch, &operation, 1, aPlayerId, aTick) && CommitBatch(std::move(batch));
}

bool VRObjectAuthority::Release(const GameId& acObjectId, uint32_t aPlayerId) noexcept
{
    const Operation operation{acObjectId, OperationKind::Release};
    Batch batch{};
    return PrepareBatch(batch, &operation, 1, aPlayerId, 0) && CommitBatch(std::move(batch));
}

void VRObjectAuthority::ReleasePlayer(uint32_t aPlayerId) noexcept
{
    auto it = s_objectAuthorityLeases.begin();
    while (it != s_objectAuthorityLeases.end())
    {
        if (it->second.OwnerPlayerId == aPlayerId)
            it = s_objectAuthorityLeases.erase(it);
        else
            ++it;
    }
}

void VRObjectAuthority::Expire(uint64_t aTick) noexcept
{
    ExpireLeases(s_objectAuthorityLeases, aTick);
}

void VRObjectAuthority::Reset() noexcept
{
    s_objectAuthorityLeases.clear();
}

VRGrabRelayService::VRGrabRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrGrabEventConnection(aDispatcher.sink<PacketEvent<RequestVRGrabEvent>>().connect<&VRGrabRelayService::OnVRGrabEvent>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRGrabRelayService::OnPlayerLeave>(this))
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&VRGrabRelayService::OnUpdate>(this))
{
    VRObjectAuthority::Reset();
}

VRGrabRelayService::~VRGrabRelayService() noexcept
{
    VRObjectAuthority::Reset();
}

void VRGrabRelayService::OnVRGrabEvent(const PacketEvent<RequestVRGrabEvent>& acMessage) noexcept try
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR grab relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    if (!SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRGrabRelay))
        return;

    const auto playerId = acMessage.pPlayer->GetId();
    const PlayerGrabRelayState emptyState{};
    const auto existingState = m_playerGrabRelayState.find(playerId);
    const auto& previousState = existingState != m_playerGrabRelayState.end() ?
                                    existingState->second : emptyState;
    RelayDecision decision{};
    VRObjectAuthority::Batch authorityBatch{};
    if (!PrepareRelayDecision(previousState, playerId, acMessage.Packet, decision, authorityBatch))
        return;

    NotifyVRGrabEvent notify{};
    notify.PlayerId = playerId;
    notify.Grab = acMessage.Packet.Grab;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
            notify, *character,
            SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VRGrabRelay),
            acMessage.pPlayer))
    {
        spdlog::warn("VR relay dropped because sender has no routable character");
        return;
    }

    // The state entry is created only after fanout. If that allocation fails,
    // the staged batch is discarded and the sender can retry the same packet.
    auto [relayState, inserted] = m_playerGrabRelayState.try_emplace(playerId);
    TP_UNUSED(inserted);
    if (!VRObjectAuthority::CommitBatch(std::move(authorityBatch)))
        return;
    CommitRelayDecision(relayState->second, decision);
}
catch (...)
{
    spdlog::error("VR grab relay rejected an update after an allocation or fanout failure");
}

void VRGrabRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
    {
        m_playerGrabRelayState.erase(acEvent.pPlayer->GetId());
        VRObjectAuthority::ReleasePlayer(acEvent.pPlayer->GetId());
    }
}

void VRGrabRelayService::OnUpdate(const UpdateEvent&) noexcept
{
    VRObjectAuthority::Expire(GameServer::Get()->GetTick());
}

bool VRGrabRelayService::PrepareRelayDecision(
    const PlayerGrabRelayState& acPrevious, const uint32_t aPlayerId,
    const RequestVRGrabEvent& acRequest, RelayDecision& arDecision,
    VRObjectAuthority::Batch& arAuthorityBatch) const noexcept
{
    const auto& grab = acRequest.Grab;
    if (grab.Sequence == 0 || !HasGrabObject(grab) || !SkyrimTogether::VR::IsVRGrabPositionValid(grab.Position))
        return false;

    if (acPrevious.HasSequence && !IsNewerSequence(grab.Sequence, acPrevious.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    const VRObjectAuthority::Operation operation{
        grab.ObjectId, grab.Grabbed ? VRObjectAuthority::OperationKind::AcquireOrRenew :
                                     VRObjectAuthority::OperationKind::Release};
    if (!VRObjectAuthority::PrepareBatch(arAuthorityBatch, &operation, 1, aPlayerId, now))
        return false;

    arDecision.Sequence = grab.Sequence;
    return true;
}

void VRGrabRelayService::CommitRelayDecision(
    PlayerGrabRelayState& arState, const RelayDecision& acDecision) noexcept
{
    arState.LastSequence = acDecision.Sequence;
    arState.HasSequence = true;
}
