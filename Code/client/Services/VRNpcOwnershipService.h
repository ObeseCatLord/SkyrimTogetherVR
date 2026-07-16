#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <Structs/ActorData.h>
#include <Structs/Factions.h>
#include <Structs/GameId.h>
#include <vr_common/VRGameplayBridge.h>

struct AssignCharacterResponse;
struct CharacterSpawnRequest;
struct ConnectedEvent;
struct DisconnectedEvent;
struct NotifyOwnershipTransfer;
struct NotifyRelinquishControl;
struct NotifyRemoveCharacter;
struct TransportService;
struct UpdateEvent;
struct World;

namespace SkyrimTogetherVR
{
struct LocalGameplayBridgeEvent;
}

// Owns only mapped, fixed-size NPC snapshots. Native actors remain entirely in
// the CommonLib adapter; client state is indexed by persistent local reference
// form ID so it survives server-id reorderings without retaining game pointers.
struct VRNpcOwnershipService
{
    VRNpcOwnershipService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRNpcOwnershipService() noexcept = default;

    TP_NOCOPYMOVE(VRNpcOwnershipService);

    [[nodiscard]] std::uint32_t GetServerIdForLocalReference(std::uint32_t aReferenceFormId) const noexcept;
    [[nodiscard]] bool RequestOwnershipForLocalReference(std::uint32_t aReferenceFormId) noexcept;

private:
    struct InventoryEntry
    {
        GameId Id{};
        std::int32_t Count{};
        bool QuestItem{};
    };

    struct Snapshot
    {
        std::uint32_t ReferenceFormId{};
        GameId ReferenceId{};
        GameId BaseId{};
        GameId CellId{};
        GameId WorldspaceId{};
        GameId PackageId{};
        glm::vec3 Position{};
        float ZRotation{};
        std::array<float, 3> Values{};
        std::array<float, 3> Maximums{};
        std::unordered_map<std::uint32_t, InventoryEntry> Inventory{};
        Factions FactionData{};
        std::uint64_t Order{};
        double Age{};
        bool Dead{};
        bool WeaponDrawn{};
    };

    struct PartialSnapshot
    {
        Snapshot Data{};
        std::uint64_t ActionId{};
        double Age{};
        bool Begun{};
        std::array<bool, 3> HasValue{};
    };

    struct PendingAssignment
    {
        std::uint32_t ReferenceFormId{};
        std::uint64_t Order{};
        double Age{};
    };

    struct PendingClaim
    {
        std::uint32_t ServerId{};
        std::uint64_t GrantToken{};
        std::uint64_t Order{};
        double Age{};
        double RetryDelay{};
        bool ClaimSent{};
    };

    struct OwnedNpc
    {
        std::uint32_t ServerId{};
        Snapshot Baseline{};
        bool HasBaseline{};
        GameId LastRelayedPackageId{};
        GameId CandidatePackageId{};
        std::uint8_t CandidatePackageObservations{};
    };

    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnLocalGameplay(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept;
    void OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept;
    void OnCharacterSpawn(const CharacterSpawnRequest& acMessage) noexcept;
    void OnOwnershipTransfer(const NotifyOwnershipTransfer& acMessage) noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept;
    void OnRelinquishControl(const NotifyRelinquishControl& acMessage) noexcept;

    [[nodiscard]] bool IsBridgeReady() const noexcept;
    [[nodiscard]] bool IsSessionCurrent() const noexcept;
    [[nodiscard]] bool IsCurrentBridgeRecord(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) const noexcept;
    [[nodiscard]] bool IsCurrentProjectileRecord(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) const noexcept;
    [[nodiscard]] bool RelayOwnedProjectileLaunch(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept;
    [[nodiscard]] bool TranslateSnapshot(PartialSnapshot& arSnapshot) const noexcept;
    [[nodiscard]] bool BuildActorData(const Snapshot& acSnapshot, ActorData& arActorData) const noexcept;
    [[nodiscard]] bool StartObservation(std::uint32_t aReferenceFormId) noexcept;
    [[nodiscard]] bool StoreLatestSnapshot(Snapshot&& aSnapshot) noexcept;
    [[nodiscard]] bool TrackServerReference(std::uint32_t aServerId, std::uint32_t aReferenceFormId) noexcept;
    void StopObservation(std::uint32_t aReferenceFormId) noexcept;
    void HandleCompleteSnapshot(Snapshot&& aSnapshot) noexcept;
    void RequestAssignment(const Snapshot& acSnapshot) noexcept;
    [[nodiscard]] bool RequestOwnershipClaim(std::uint32_t aServerId, std::uint64_t aGrantToken, const Snapshot& acSnapshot) noexcept;
    void PromotePendingGrant(std::uint32_t aServerId, std::uint32_t aReferenceFormId) noexcept;
    void ReplicateOwnedSnapshot(OwnedNpc& arOwned, const Snapshot& acSnapshot) noexcept;
    void RelinquishOwned(bool aSendTransfer) noexcept;
    void ClearReference(std::uint32_t aReferenceFormId) noexcept;
    void ResetSessionState(bool aSendTransfer) noexcept;
    void ExpireStaleState(double aDelta) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    std::unordered_map<std::uint32_t, PartialSnapshot> m_partialSnapshots{};
    std::unordered_map<std::uint32_t, Snapshot> m_latestSnapshots{};
    std::unordered_map<std::uint32_t, PendingAssignment> m_pendingAssignments{};
    std::unordered_map<std::uint32_t, PendingClaim> m_claimAfterSnapshot{};
    std::unordered_map<std::uint32_t, PendingClaim> m_pendingGrantByServer{};
    std::unordered_map<std::uint32_t, std::uint32_t> m_serverToReference{};
    std::unordered_map<std::uint32_t, std::uint32_t> m_referenceToServer{};
    std::unordered_map<std::uint32_t, OwnedNpc> m_ownedByReference{};
    std::uint32_t m_nextAssignmentCookie{1};
    std::uint64_t m_nextStateOrder{1};
    std::uint64_t m_lastCompletedSnapshotActionId{};
    std::uint64_t m_lastLocalProjectileSequence{};
    std::uint64_t m_sessionServerInstanceNonce{};
    std::uint64_t m_sessionConnectionGeneration{};
    std::uint64_t m_bridgeLifecycleEpoch{};
    bool m_connected{false};
    bool m_bridgeWasReady{false};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_localGameplayConnection;
    entt::scoped_connection m_assignCharacterConnection;
    entt::scoped_connection m_characterSpawnConnection;
    entt::scoped_connection m_ownershipTransferConnection;
    entt::scoped_connection m_removeCharacterConnection;
    entt::scoped_connection m_relinquishConnection;
};
