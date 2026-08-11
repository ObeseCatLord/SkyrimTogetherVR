#include <Services/VRHiggsRelayService.h>
#include <Services/VRGrabRelayService.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRHiggsState.h>
#include <Messages/RequestVRHiggsState.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VRHiggsState.h>
#include <Structs/VRInteractionValidation.h>

namespace
{
constexpr uint64_t kMinHiggsRelayIntervalMs = 200;

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
} // namespace

VRHiggsRelayService::VRHiggsRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrHiggsStateConnection(aDispatcher.sink<PacketEvent<RequestVRHiggsState>>().connect<&VRHiggsRelayService::OnVRHiggsState>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRHiggsRelayService::OnPlayerLeave>(this))
{
}

void VRHiggsRelayService::OnVRHiggsState(const PacketEvent<RequestVRHiggsState>& acMessage) noexcept try
{
    TP_UNUSED(m_world);

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
    auto [relayState, inserted] = m_playerHiggsRelayState.try_emplace(playerId);
    TP_UNUSED(inserted);

    RelayDecision decision{};
    if (!BuildRelayDecision(relayState->second, acMessage.Packet, decision))
        return;

    VRObjectAuthority::Batch authorityBatch{};
    if (!PrepareObjectAuthority(decision, playerId, authorityBatch))
        return;

    // A conflict-only replay has no recipient-visible state. Its terminal
    // classification still must be remembered so it cannot block later edges.
    if (!decision.ForwardObservation && !decision.HasMutationReplay)
    {
        CommitRelayDecision(relayState->second, decision);
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
        spdlog::warn("VR relay dropped because sender has no routable character");
        return;
    }

    // Fanout is the authority admission boundary. The transaction owns a
    // complete shadow lease set and becomes visible only after routing accepts
    // the corresponding accepted mutation prefix.
    if (!VRObjectAuthority::CommitBatch(std::move(authorityBatch)))
        return;
    CommitRelayDecision(relayState->second, decision);
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
    const PlayerHiggsRelayState& acPrevious, const RequestVRHiggsState& acRequest,
    RelayDecision& arDecision) const noexcept
{
    const auto& statePacket = acRequest.State;
    if (!statePacket.IsDecodedValid || !statePacket.IsMutationReplayValid() ||
        statePacket.Sequence == 0 || !HasHiggsObservation(statePacket))
        return false;

    const auto now = GameServer::Get()->GetTick();
    const bool hasNewObservation = !acPrevious.HasObservationSequence ||
        IsNewerSequence(statePacket.Sequence, acPrevious.LastObservationSequence);
    const bool observationIntervalElapsed = acPrevious.LastObservationRelayTick == 0 ||
        now < acPrevious.LastObservationRelayTick ||
        now - acPrevious.LastObservationRelayTick >= kMinHiggsRelayIntervalMs;

    arDecision = {};
    arDecision.State = statePacket;
    arDecision.State.MutationEvents = {};
    arDecision.State.MutationEventCount = 0;
    arDecision.State.MutationSequence = 0;
    arDecision.Tick = now;
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

        const auto terminal = std::find_if(
            acPrevious.MutationTerminals.begin(),
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
        if (acPrevious.HasTerminalMutationSequence &&
            !IsNewerSequence(event.Sequence, acPrevious.LastTerminalMutationSequence))
            continue;

        if (arDecision.NewMutationCount >= arDecision.NewMutations.size())
            return false;
        arDecision.NewMutations[arDecision.NewMutationCount++] = event;
    }

    return true;
}

bool VRHiggsRelayService::PrepareObjectAuthority(
    RelayDecision& arDecision, const uint32_t aPlayerId, VRObjectAuthority::Batch& arBatch) noexcept
{
    if (!VRObjectAuthority::BeginBatch(arBatch, arDecision.Tick))
        return false;

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
        const bool accepted = VRObjectAuthority::TryApplyOperation(arBatch, operation, aPlayerId, arDecision.Tick);
        if (arDecision.NewTerminalCount >= arDecision.NewTerminals.size())
            return false;
        arDecision.NewTerminals[arDecision.NewTerminalCount++] = {event.Sequence, accepted};
        if (accepted && !appendForwardedMutation(event))
            return false;
    }

    // Observed hand state may renew an existing lease, but it cannot acquire
    // an object without an ordered mutation edge. A failed renewal is merely
    // stale telemetry and never invalidates accepted later mutations.
    const auto renewHeldObject = [&arBatch, aPlayerId, &arDecision](const VRHiggsHandState& acHand) noexcept {
        if (!IsHeldObject(acHand))
            return;
        const VRObjectAuthority::Operation operation{
            acHand.GrabbedObject, VRObjectAuthority::OperationKind::RenewExisting};
        TP_UNUSED(VRObjectAuthority::TryApplyOperation(arBatch, operation, aPlayerId, arDecision.Tick));
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
