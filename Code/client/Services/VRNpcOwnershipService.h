#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <Structs/ActorData.h>
#include <Structs/Factions.h>
#include <Structs/GameId.h>
#include <Structs/VRAppearance.h>
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
struct RemoteGameplayBridgeResultEvent;
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
    [[nodiscard]] std::uint32_t GetLocalReferenceForOwnedServerId(std::uint32_t aServerId) const noexcept;
    [[nodiscard]] bool RequestOwnershipForLocalReference(std::uint32_t aReferenceFormId) noexcept;
    void CommitRemoteInventoryTransaction(std::uint32_t aServerId,
                                          const std::vector<Inventory::Entry>& acEntries,
                                          bool aReset) noexcept;

private:
    struct InventoryEntry
    {
        std::uint32_t LocalFormId{};
        Inventory::Entry Item{};
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
        std::array<float, SkyrimTogetherVR::GameplayBridge::kSkyrimActorValueCount> Values{};
        std::array<float, SkyrimTogetherVR::GameplayBridge::kSkyrimActorValueCount> Maximums{};
        SkyrimTogetherVR::AnimationGraphProtocol::SnapshotBuffer Animation{};
        std::vector<InventoryEntry> Inventory{};
        Factions FactionData{};
        VRAppearance Appearance{};
        std::uint64_t Order{};
        double Age{};
        bool Dead{};
        bool WeaponDrawn{};
        bool IsDragon{};
        bool IsMount{};
        bool IsPlayerSummon{};
        bool HasAnimationGraph{};
    };

    struct PartialSnapshot
    {
        Snapshot Data{};
        SkyrimTogetherVR::GameplayBridge::BridgeIdentity Identity{};
        std::uint64_t ActionId{};
        std::uint16_t ExpectedInventoryCount{};
        std::uint16_t ExpectedFactionCount{};
        std::uint16_t NextActorValueIndex{};
        std::uint8_t ExpectedAppearanceHeadPartCount{};
        std::uint8_t ExpectedNameChunkCount{};
        std::uint8_t NextFaceMorphIndex{};
        std::uint8_t NextFacePartIndex{};
        std::uint8_t NextNameChunkIndex{};
        std::uint8_t NextGraphChunk{};
        std::uint32_t LastFactionFormId{};
        std::size_t OpenInventoryIndex{};
        std::uint32_t InventoryEffectsRemaining{};
        std::uint32_t TotalInventoryEffects{};
        double Age{};
        bool Begun{};
        bool HasAppearance{};
        bool HasInventoryExtra{};
        bool ExpectsAnimationGraph{};
        std::array<bool, SkyrimTogetherVR::GameplayBridge::kSkyrimActorValueCount> HasValue{};
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

    struct PendingObservation
    {
        SkyrimTogetherVR::GameplayBridge::BridgeIdentity Identity{};
        std::uint32_t ReferenceFormId{};
        std::uint32_t ServerId{};
        SkyrimTogetherVR::GameplayBridge::GameplayAction Action{};
        double Age{};
        bool Active{};
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
    void OnGameplayResult(const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept;
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
    [[nodiscard]] bool StopObservation(std::uint32_t aReferenceFormId) noexcept;
    [[nodiscard]] PendingObservation* FindPendingObservation(std::uint64_t aActionId) noexcept;
    [[nodiscard]] bool HasPendingObservation(
        std::uint32_t aReferenceFormId, SkyrimTogetherVR::GameplayBridge::GameplayAction aAction) const noexcept;
    [[nodiscard]] bool HasPendingObservationSlot() const noexcept;
    [[nodiscard]] bool HasObservationCapacity(std::uint32_t aReferenceFormId) const noexcept;
    [[nodiscard]] bool TrackPendingObservation(const SkyrimTogetherVR::GameplayBridge::CommandRecord& acCommand) noexcept;
    void ClearPendingObservationsForReference(std::uint32_t aReferenceFormId) noexcept;
    void ClearPendingObservations() noexcept;
    void RequestObservationLifecycleRetirement() noexcept;
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
    std::unordered_set<std::uint32_t> m_observedReferences{};
    std::array<PendingObservation, 128> m_pendingObservations{};
    std::uint32_t m_nextAssignmentCookie{1};
    std::uint64_t m_nextStateOrder{1};
    std::uint64_t m_lastCompletedSnapshotActionId{};
    std::uint64_t m_lastLocalProjectileSequence{};
    std::uint64_t m_sessionServerInstanceNonce{};
    std::uint64_t m_sessionConnectionGeneration{};
    std::uint64_t m_bridgeLifecycleEpoch{};
    bool m_connected{false};
    bool m_bridgeWasReady{false};
    bool m_observationLifecycleRetirementRequested{false};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_localGameplayConnection;
    entt::scoped_connection m_gameplayResultConnection;
    entt::scoped_connection m_assignCharacterConnection;
    entt::scoped_connection m_characterSpawnConnection;
    entt::scoped_connection m_ownershipTransferConnection;
    entt::scoped_connection m_removeCharacterConnection;
    entt::scoped_connection m_relinquishConnection;
};
