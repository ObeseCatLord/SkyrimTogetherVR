#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <Events/PacketEvent.h>
#include <Structs/GameId.h>
#include <TiltedCore/Stl.hpp>
struct RequestVRPlanckPhysicsEvent;
struct World;
struct PlayerLeaveEvent;
struct UpdateEvent;
struct VRPlanckPhysicsRelayService
{
    VRPlanckPhysicsRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRPlanckPhysicsRelayService() noexcept;

    TP_NOCOPYMOVE(VRPlanckPhysicsRelayService);

private:
    enum class RejectionReason : uint8_t
    {
        UnauthenticatedProducer,
        InvalidPayload,
        InvalidPhysicalTarget,
        ProducerTransition,
        ReplayedEvent,
        RateLimited,
        GripLease,
        InternalFailure,
        Count,
    };

    struct GripLease
    {
        uint32_t OwnerPlayerId{0};
        ConnectionId_t OwnerConnection{};
        uint64_t OwnerConnectionGeneration{0};
        uint64_t ProducerEpoch{0};
        uint64_t ProducerSession{0};
        uint64_t GripId{0};
        uint64_t ExpiryTick{0};
        uint64_t ExpiryRevision{0};
    };

    struct PlayerState
    {
        uint64_t ProducerEpoch{0};
        ConnectionId_t OwnerConnection{};
        uint64_t ConnectionGeneration{0};
        uint64_t ClientSessionNonce{0};
        uint64_t LastEventId{0};
        uint64_t RateWindowTick{0};
        uint32_t RateCount{0};
        uint32_t ActiveGripCount{0};
        // This index is scoped by the server-authenticated producer state
        // above, so (GripId -> target) is precisely producer + GripId.
        TiltedPhoques::Map<uint64_t, GameId> GripTargets{};
        bool HasProducer{false};
        bool HasEvent{false};
    };

    struct GripExpiryRecord
    {
        GameId Target{};
        uint64_t ExpiryTick{0};
        uint64_t Revision{0};
    };

    struct ProvisionalGripLeaseRollback
    {
        explicit ProvisionalGripLeaseRollback(VRPlanckPhysicsRelayService& arService) noexcept;
        ~ProvisionalGripLeaseRollback() noexcept;

        void Arm(const GameId& acTarget) noexcept;
        void Disarm() noexcept;

        VRPlanckPhysicsRelayService& Service;
        GameId Target{};
        bool Armed{false};
    };

    void OnEvent(const PacketEvent<RequestVRPlanckPhysicsEvent>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void Expire(uint64_t aNow) noexcept;
    void ReserveExpiryRecord();
    void CommitExpiryRecord(const GameId& acTarget, const GripLease& acLease) noexcept;
    static bool IsLaterExpiry(const GripExpiryRecord& acLeft, const GripExpiryRecord& acRight) noexcept;
    static uint64_t NextLeaseRevision(uint64_t aRevision) noexcept;
    void ReleasePlayerLeases(uint32_t aPlayerId) noexcept;
    void Reset() noexcept;
    void Reject(RejectionReason aReason) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerState> m_states{};
    // Actor IDs are global to the server world. A lease therefore excludes
    // every competing producer, rather than only another grip from one sender.
    TiltedPhoques::Map<GameId, GripLease> m_actorGrips{};
    std::vector<GripExpiryRecord> m_gripExpiryHeap{};
    uint64_t m_nextGripLeaseRevision{1};
    static bool MatchesLease(const GripLease& acLease, const class Player& acPlayer, uint64_t aProducerEpoch, uint64_t aGripId) noexcept;

    std::array<uint64_t, static_cast<std::size_t>(RejectionReason::Count)> m_rejectionCounts{};
    uint64_t m_noRoutableCharacterCount{0};
    entt::scoped_connection m_eventConnection;
    entt::scoped_connection m_leaveConnection;
    entt::scoped_connection m_updateConnection;
};
