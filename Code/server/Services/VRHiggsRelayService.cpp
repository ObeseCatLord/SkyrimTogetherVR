#include <Services/VRHiggsRelayService.h>
#include <Services/VRRelayLogPolicy.h>
#include <Services/VRGrabRelayService.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <utility>

#include <GameServer.h>
#include <World.h>
#include <Game/Player.h>
#include <Components.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRHiggsState.h>
#include <Messages/RequestVRHiggsState.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VRHiggsState.h>
#include <Structs/VRInteractionValidation.h>

namespace
{
constexpr uint64_t kMinHiggsRelayIntervalMs = 50;

bool RecordAggregate(uint64_t& arCount) noexcept
{
    if (arCount == std::numeric_limits<uint64_t>::max())
        return false;
    ++arCount;
    return (arCount & (arCount - 1)) == 0;
}

bool IsNewerSequence(const uint32_t aCandidate, const uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasHiggsObservation(const VRHiggsState& acState) noexcept
{
    return acState.BridgeLoaded || acState.Detected || acState.InterfaceAvailable || acState.CallbacksRegistered ||
           acState.SnapshotAvailable || acState.Left.Valid || acState.Right.Valid ||
           acState.MutationEventCount != 0;
}

bool IsMutationEvent(const VRHiggsEventSnapshot::Kind aKind) noexcept
{
    return aKind == VRHiggsEventSnapshot::Kind::kPulled ||
           aKind == VRHiggsEventSnapshot::Kind::kGrabbed ||
           aKind == VRHiggsEventSnapshot::Kind::kDropped ||
           aKind == VRHiggsEventSnapshot::Kind::kStashed ||
           aKind == VRHiggsEventSnapshot::Kind::kConsumed;
}

bool IsLeaseReleaseEvent(const VRHiggsEventSnapshot::Kind aKind) noexcept
{
    return aKind == VRHiggsEventSnapshot::Kind::kDropped || aKind == VRHiggsEventSnapshot::Kind::kStashed ||
           aKind == VRHiggsEventSnapshot::Kind::kConsumed || aKind == VRHiggsEventSnapshot::Kind::kStopTwoHanding;
}

bool IsHeldObject(const VRHiggsHandState& acHand) noexcept
{
    return acHand.HoldingObject && static_cast<bool>(acHand.GrabbedObject);
}

void ClearHeldObject(VRHiggsHandState& arHand) noexcept
{
    arHand.HoldingObject = false;
    arHand.GrabbedObject = {};
    arHand.GrabbedNodeName = {};
    arHand.GrabbedNodeNameLength = 0;
    arHand.GrabTransform = {};
}

bool IsActorReference(World& arWorld, const GameId& acObjectId) noexcept
{
    const auto actors = arWorld.view<FormIdComponent, CharacterComponent>();
    return std::any_of(actors.begin(), actors.end(), [&actors, &acObjectId](const entt::entity entity) {
        return actors.get<FormIdComponent>(entity).Id == acObjectId;
    });
}

bool IsLeaseAcquisitionInSenderRange(World& arWorld, const Player& acPlayer, const GameId& acObjectId) noexcept
{
    const auto character = acPlayer.GetCharacter();
    if (!character || !arWorld.valid(*character) ||
        !arWorld.all_of<CharacterComponent, CellIdComponent>(*character))
        return false;
    const auto objects = arWorld.view<FormIdComponent, ObjectComponent, CellIdComponent>();
    const auto object = std::find_if(objects.begin(), objects.end(), [&objects, &acObjectId](const entt::entity entity) {
        return objects.get<FormIdComponent>(entity).Id == acObjectId;
    });
    if (object == objects.end())
        return false;
    const auto& senderCell = acPlayer.GetCellComponent();
    const auto& characterCell = arWorld.get<CellIdComponent>(*character);
    const auto& objectCell = objects.get<CellIdComponent>(*object);
    return senderCell.IsInRange(characterCell, false) && senderCell.IsInRange(objectCell, false) &&
           characterCell.IsInRange(objectCell, false);
}
} // namespace

VRHiggsRelayService::VRHiggsRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrHiggsStateConnection(aDispatcher.sink<PacketEvent<RequestVRHiggsState>>().connect<&VRHiggsRelayService::OnVRHiggsState>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRHiggsRelayService::OnPlayerLeave>(this))
{
}

void VRHiggsRelayService::OnVRHiggsState(const PacketEvent<RequestVRHiggsState>& acMessage) noexcept try
{
    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR HIGGS relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    if (!SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRHiggsRelay))
        return;

    const auto playerId = acMessage.pPlayer->GetId();
    const PlayerHiggsRelayState emptyState{};
    const auto existing = m_playerHiggsRelayState.find(playerId);
    const auto& relayState = existing != m_playerHiggsRelayState.end() ? existing->second : emptyState;

    RelayDecision decision{};
    if (!BuildRelayDecision(relayState, *acMessage.pPlayer, acMessage.Packet, decision))
    {
        if (RecordAggregate(m_rejectionCount))
            spdlog::warn("VR HIGGS relay rejected an invalid producer state (aggregate count: {})", m_rejectionCount);
        return;
    }

    VRObjectAuthority::Batch authorityBatch{};
    if (!PrepareObjectAuthority(decision, *acMessage.pPlayer, playerId, authorityBatch))
    {
        if (RecordAggregate(m_rejectionCount))
            spdlog::warn("VR HIGGS relay rejected an authority transaction (aggregate count: {})", m_rejectionCount);
        return;
    }

    // A conflict-only replay has no recipient-visible state. Its terminal
    // classification still must be remembered so it cannot block later edges.
    if (!decision.ForwardObservation && !decision.HasMutationReplay)
    {
        // A producer/connection rebase drops old leases. Do not record the
        // new binding until that authority change can commit atomically.
        if (decision.ResetReplayState)
            return;
        auto [state, inserted] = m_playerHiggsRelayState.try_emplace(playerId);
        TP_UNUSED(inserted);
        CommitRelayDecision(state.value(), decision);
        return;
    }

    NotifyVRHiggsState notify{};
    notify.PlayerId = playerId;
    notify.State = decision.State;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
            notify, *character,
            SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VRHiggsRelay),
            acMessage.pPlayer))
    {
        if (VRRelayLogPolicy::RecordNoRoutableCharacter(m_noRoutableCharacterCount))
            spdlog::warn("VR HIGGS relay dropped because sender has no routable character (aggregate count: {})",
                         m_noRoutableCharacterCount);
        return;
    }

    // Fanout is the authority admission boundary. The transaction owns a
    // complete shadow lease set and becomes visible only after routing accepts
    // the corresponding accepted mutation prefix.
    if (!VRObjectAuthority::CommitBatch(std::move(authorityBatch)))
        return;
    auto [state, inserted] = m_playerHiggsRelayState.try_emplace(playerId);
    TP_UNUSED(inserted);
    CommitRelayDecision(state.value(), decision);
    if (decision.RebaseProducer && RecordAggregate(m_producerRebaseCount))
        spdlog::warn("VR HIGGS relay accepted a producer replay rebase (aggregate count: {})",
                     m_producerRebaseCount);
}
catch (...)
{
    spdlog::error("VR HIGGS relay rejected an update after an allocation or fanout failure");
}

void VRHiggsRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
    {
        m_playerHiggsRelayState.erase(acEvent.pPlayer->GetId());
        VRObjectAuthority::ReleasePlayer(acEvent.pPlayer->GetId());
    }
}

bool VRHiggsRelayService::BuildRelayDecision(
    const PlayerHiggsRelayState& acPrevious, const Player& acPlayer, const RequestVRHiggsState& acRequest,
    RelayDecision& arDecision) const noexcept
{
    const auto& statePacket = acRequest.State;
    if (!statePacket.IsDecodedValid || !statePacket.IsMutationReplayValid() || statePacket.ProducerEpoch == 0 ||
        statePacket.Sequence == 0 || !HasHiggsObservation(statePacket))
        return false;

    const auto connectionGeneration = acPlayer.GetConnectionGeneration();
    if (connectionGeneration == 0)
        return false;
    const bool connectionRebased = acPrevious.HasConnectionGeneration &&
                                   acPrevious.ConnectionGeneration != connectionGeneration;
    const bool producerEpochChanged = acPrevious.HasConnectionGeneration &&
                                      acPrevious.ProducerEpoch != statePacket.ProducerEpoch;
    const bool producerRebased = statePacket.MutationReplayRebased;
    const auto now = GameServer::Get()->GetTick();

    // Rebase is an authenticated, baseline-only transaction. A marker is
    // meaningful only for a bound producer and cannot smuggle retained
    // mutations through the lease release.
    if (producerRebased)
    {
        if (!producerEpochChanged || statePacket.MutationEventCount != 0 || statePacket.MutationSequence != 0 ||
            !IsVRHiggsRelayOperational(statePacket))
            return false;
    }
    else if (producerEpochChanged)
        return false;

    const bool resetReplayState = connectionRebased || producerRebased;
    const bool hasNewObservation = resetReplayState || !acPrevious.HasObservationSequence ||
        IsNewerSequence(statePacket.Sequence, acPrevious.LastObservationSequence);
    const bool observationIntervalElapsed = acPrevious.LastObservationRelayTick == 0 ||
        now < acPrevious.LastObservationRelayTick ||
        now - acPrevious.LastObservationRelayTick >= kMinHiggsRelayIntervalMs;

    arDecision = {};
    arDecision.State = statePacket;
    // MutationReplayRebased is a sender-to-relay control marker, not state
    // receivers must retain after the relay has committed the new baseline.
    arDecision.State.MutationReplayRebased = false;
    arDecision.State.MutationEvents = {};
    arDecision.State.MutationEventCount = 0;
    arDecision.State.MutationSequence = 0;
    arDecision.ConnectionGeneration = connectionGeneration;
    arDecision.Tick = now;
    arDecision.ResetReplayState = resetReplayState;
    arDecision.RebaseProducer = producerRebased;
    arDecision.ForwardObservation = hasNewObservation && observationIntervalElapsed;

    const auto appendForwardedMutation = [&arDecision](const VRHiggsEventSnapshot& acEvent) noexcept {
        if (arDecision.State.MutationEventCount >= arDecision.State.MutationEvents.size())
            return false;
        arDecision.State.MutationEvents[arDecision.State.MutationEventCount++] = acEvent;
        return true;
    };

    for (std::size_t index = 0; index < statePacket.MutationEventCount; ++index)
    {
        const auto& event = statePacket.MutationEvents[index];
        if (event.Sequence == 0 || !IsMutationEvent(event.EventKind) || !event.ObjectId ||
            !SkyrimTogether::VR::IsHiggsMutationPayloadValid(event.Mass, event.SeparatingVelocity))
            return false;

        const auto terminal = resetReplayState ? acPrevious.MutationTerminals.begin() + acPrevious.MutationTerminalCount :
            std::find_if(acPrevious.MutationTerminals.begin(),
                         acPrevious.MutationTerminals.begin() + acPrevious.MutationTerminalCount,
                         [&event](const PlayerHiggsRelayState::MutationTerminal& acTerminal) noexcept {
                             return acTerminal.Sequence == event.Sequence;
                         });
        if (terminal != acPrevious.MutationTerminals.begin() + acPrevious.MutationTerminalCount)
        {
            if (terminal->Forwarded && !appendForwardedMutation(event))
                return false;
            continue;
        }

        // The retained sender window can contain an evicted old terminal.
        // It is already resolved and must not be evaluated again.
        if (!resetReplayState && acPrevious.HasTerminalMutationSequence &&
            !IsNewerSequence(event.Sequence, acPrevious.LastTerminalMutationSequence))
            continue;

        if (arDecision.NewMutationCount >= arDecision.NewMutations.size())
            return false;
        arDecision.NewMutations[arDecision.NewMutationCount++] = event;
    }

    return true;
}

bool VRHiggsRelayService::PrepareObjectAuthority(
    RelayDecision& arDecision, const Player& acPlayer, const uint32_t aPlayerId, VRObjectAuthority::Batch& arBatch) noexcept
{
    if (!VRObjectAuthority::BeginBatch(arBatch, arDecision.Tick))
        return false;
    if (arDecision.ResetReplayState)
        VRObjectAuthority::ReleasePlayer(arBatch, aPlayerId);

    const auto appendForwardedMutation = [&arDecision](const VRHiggsEventSnapshot& acEvent) noexcept {
        if (arDecision.State.MutationEventCount >= arDecision.State.MutationEvents.size())
            return false;
        arDecision.State.MutationEvents[arDecision.State.MutationEventCount++] = acEvent;
        return true;
    };

    for (std::size_t index = 0; index < arDecision.NewMutationCount; ++index)
    {
        const auto& event = arDecision.NewMutations[index];
        const VRObjectAuthority::Operation operation{
            event.ObjectId,
            IsLeaseReleaseEvent(event.EventKind) ? VRObjectAuthority::OperationKind::Release :
                                                   VRObjectAuthority::OperationKind::AcquireOrRenew};
        const bool isAcquire = operation.Kind == VRObjectAuthority::OperationKind::AcquireOrRenew;
        const bool accepted = !IsActorReference(m_world, event.ObjectId) &&
            (!isAcquire || IsLeaseAcquisitionInSenderRange(m_world, acPlayer, event.ObjectId)) ?
            VRObjectAuthority::TryApplyOperation(arBatch, operation, aPlayerId, arDecision.Tick) : false;
        if (arDecision.NewTerminalCount >= arDecision.NewTerminals.size())
            return false;
        arDecision.NewTerminals[arDecision.NewTerminalCount++] = {event.Sequence, accepted};
        if (accepted && !appendForwardedMutation(event))
            return false;
    }

    // Observed hand state may renew an existing lease, but it cannot acquire
    // an object without an ordered mutation edge. Never forward a held
    // object unless that renewal succeeds: stale held metadata would let a
    // receiver present an object the sender no longer owns.
    const auto renewHeldObject = [this, &arBatch, aPlayerId, &arDecision](VRHiggsHandState& arHand) noexcept {
        if (!IsHeldObject(arHand))
            return;
        const VRObjectAuthority::Operation operation{
            arHand.GrabbedObject, VRObjectAuthority::OperationKind::RenewExisting};
        if (IsActorReference(m_world, arHand.GrabbedObject) ||
            !VRObjectAuthority::TryApplyOperation(arBatch, operation, aPlayerId, arDecision.Tick))
            ClearHeldObject(arHand);
    };
    renewHeldObject(arDecision.State.Left);
    renewHeldObject(arDecision.State.Right);

    arDecision.HasMutationReplay = arDecision.State.MutationEventCount != 0;
    arDecision.State.MutationSequence = arDecision.HasMutationReplay ?
        arDecision.State.MutationEvents[arDecision.State.MutationEventCount - 1].Sequence : 0;
    return true;
}

void VRHiggsRelayService::CommitRelayDecision(
    PlayerHiggsRelayState& arState, const RelayDecision& acDecision) noexcept
{
    if (acDecision.ResetReplayState)
        arState = {};
    arState.ConnectionGeneration = acDecision.ConnectionGeneration;
    arState.HasConnectionGeneration = true;
    arState.ProducerEpoch = acDecision.State.ProducerEpoch;
    if (acDecision.RebaseProducer)
    {
        arState.LastProducerRebaseTick = acDecision.Tick;
        arState.HasProducerRebaseTick = true;
    }
    if (acDecision.ForwardObservation)
    {
        arState.LastObservationSequence = acDecision.State.Sequence;
        arState.LastObservationRelayTick = acDecision.Tick;
        arState.HasObservationSequence = true;
    }

    for (std::size_t index = 0; index < acDecision.NewTerminalCount; ++index)
    {
        const auto terminal = acDecision.NewTerminals[index];
        if (arState.MutationTerminalCount == arState.MutationTerminals.size())
        {
            for (std::size_t terminalIndex = 1; terminalIndex < arState.MutationTerminalCount; ++terminalIndex)
                arState.MutationTerminals[terminalIndex - 1] = arState.MutationTerminals[terminalIndex];
            --arState.MutationTerminalCount;
        }
        arState.MutationTerminals[arState.MutationTerminalCount++] = terminal;
        arState.LastTerminalMutationSequence = terminal.Sequence;
        arState.HasTerminalMutationSequence = true;
    }
}
