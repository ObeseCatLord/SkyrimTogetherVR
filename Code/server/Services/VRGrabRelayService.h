#pragma once

#include <cstddef>
#include <cstdint>
#include <Events/PacketEvent.h>
#include <Structs/GameId.h>
#include <TiltedCore/Stl.hpp>

struct RequestVRGrabEvent;
struct World;
struct PlayerLeaveEvent;
struct UpdateEvent;

namespace VRObjectAuthority
{
enum class OperationKind : uint8_t
{
    AcquireOrRenew,
    RenewExisting,
    Release,
};

struct Operation
{
    GameId ObjectId{};
    OperationKind Kind{OperationKind::AcquireOrRenew};
};

struct Lease
{
    uint32_t OwnerPlayerId{0};
    uint64_t ExpiryTick{0};
};

// A transaction owns a shadow of the complete lease set. Building one never
// mutates live authority; CommitBatch swaps it into place after fanout accepts
// the corresponding gameplay packet.
struct Batch
{
    TiltedPhoques::Map<GameId, Lease> Leases{};
    bool Prepared{false};
};

[[nodiscard]] bool PrepareBatch(Batch& arBatch, const Operation* apOperations,
                                std::size_t aOperationCount, uint32_t aPlayerId,
                                uint64_t aTick) noexcept;
[[nodiscard]] bool BeginBatch(Batch& arBatch, uint64_t aTick) noexcept;
[[nodiscard]] bool TryApplyOperation(Batch& arBatch, const Operation& acOperation,
                                     uint32_t aPlayerId, uint64_t aTick) noexcept;
[[nodiscard]] bool CommitBatch(Batch&& arBatch) noexcept;
[[nodiscard]] bool AcquireOrRenew(const GameId& acObjectId, uint32_t aPlayerId, uint64_t aTick);
[[nodiscard]] bool Release(const GameId& acObjectId, uint32_t aPlayerId) noexcept;
void ReleasePlayer(uint32_t aPlayerId) noexcept;
void Expire(uint64_t aTick) noexcept;
void Reset() noexcept;
} // namespace VRObjectAuthority

struct VRGrabRelayService
{
    VRGrabRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VRGrabRelayService() noexcept;

    TP_NOCOPYMOVE(VRGrabRelayService);

private:
    struct PlayerGrabRelayState
    {
        uint32_t LastSequence{0};
        bool HasSequence{false};
    };

    struct RelayDecision
    {
        uint32_t Sequence{0};
    };

    void OnVRGrabEvent(const PacketEvent<RequestVRGrabEvent>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    [[nodiscard]] bool PrepareRelayDecision(const PlayerGrabRelayState& acPrevious,
                                            uint32_t aPlayerId, const RequestVRGrabEvent& acRequest,
                                            RelayDecision& arDecision,
                                            VRObjectAuthority::Batch& arAuthorityBatch) const noexcept;
    static void CommitRelayDecision(PlayerGrabRelayState& arState,
                                    const RelayDecision& acDecision) noexcept;

    World& m_world;
    TiltedPhoques::Map<uint32_t, PlayerGrabRelayState> m_playerGrabRelayState{};
    entt::scoped_connection m_vrGrabEventConnection;
    entt::scoped_connection m_playerLeaveConnection;
    entt::scoped_connection m_updateConnection;
};
