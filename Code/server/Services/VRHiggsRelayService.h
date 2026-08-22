#pragma once

#include <array>
#include <cstdint>
#include <Events/PacketEvent.h>
#include <Services/VRGrabRelayService.h>
#include <Structs/VRHiggsState.h>
#include <TiltedCore/Stl.hpp>

struct RequestVRHiggsState;
struct World;
struct PlayerLeaveEvent;

struct VRHiggsRelayService
{
    VRHiggsRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRHiggsRelayService() noexcept = default;

    TP_NOCOPYMOVE(VRHiggsRelayService);

private:
    struct PlayerHiggsRelayState
    {
        uint64_t ConnectionGeneration{0};
        uint64_t ProducerEpoch{0};
        uint32_t LastObservationSequence{0};
        uint32_t LastTerminalMutationSequence{0};
        uint64_t LastObservationRelayTick{0};
        uint64_t LastProducerRebaseTick{0};
        bool HasConnectionGeneration{false};
        bool HasObservationSequence{false};
        bool HasTerminalMutationSequence{false};
        bool HasProducerRebaseTick{false};

        // The sender has a bounded replay window. Retain the same bounded
        // terminal classification so an authority-denied edge is not retried
        // forever while accepted edges remain available to receivers.
        struct MutationTerminal
        {
            uint32_t Sequence{0};
            bool Forwarded{false};
        };
        std::array<MutationTerminal, kMaximumHiggsMutationEvents> MutationTerminals{};
        uint8_t MutationTerminalCount{0};
    };

    struct RelayDecision
    {
        VRHiggsState State{};
        std::array<VRHiggsEventSnapshot, kMaximumHiggsMutationEvents> NewMutations{};
        std::array<PlayerHiggsRelayState::MutationTerminal, kMaximumHiggsMutationEvents> NewTerminals{};
        uint8_t NewMutationCount{0};
        uint8_t NewTerminalCount{0};
        uint64_t ConnectionGeneration{0};
        uint64_t Tick{0};
        bool ForwardObservation{false};
        bool HasMutationReplay{false};
        bool ResetReplayState{false};
        bool RebaseProducer{false};
    };

    void OnVRHiggsState(const PacketEvent<RequestVRHiggsState>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    [[nodiscard]] bool BuildRelayDecision(const PlayerHiggsRelayState& acPrevious,
                                          const class Player& acPlayer,
                                          const RequestVRHiggsState& acRequest,
                                          RelayDecision& arDecision) const noexcept;
    [[nodiscard]] bool PrepareObjectAuthority(RelayDecision& arDecision, const class Player& acPlayer,
                                              uint32_t aPlayerId,
                                              VRObjectAuthority::Batch& arBatch) noexcept;
    static void CommitRelayDecision(PlayerHiggsRelayState& arState,
                                    const RelayDecision& acDecision) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerHiggsRelayState> m_playerHiggsRelayState{};
    uint64_t m_rejectionCount{0};
    uint64_t m_producerRebaseCount{0};
    uint64_t m_noRoutableCharacterCount{0};
    entt::scoped_connection m_vrHiggsStateConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
