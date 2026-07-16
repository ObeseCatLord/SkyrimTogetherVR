#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include <vr_common/VRGameplayBridge.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/NotifyAddTarget.h>
#include <Structs/ActionEvent.h>
#include <Structs/Inventory.h>
#include <Structs/VRAppearance.h>
#include <Structs/VREquipmentUpdate.h>

struct DisconnectedEvent;
struct NotifyActorMaxValueChanges;
struct NotifyActorValueChanges;
struct NotifyDeathStateChange;
struct NotifyHealthChangeBroadcast;
struct NotifyDrawWeapon;
struct NotifyEquipmentChanges;
struct NotifyFactionsChanges;
struct NotifyInventoryChanges;
struct NotifyMount;
struct NotifyRemoveSpell;
struct NotifyRemoveCharacter;
struct NotifyPlayerLeft;
struct NotifyPlayerLevel;
struct NotifyProjectileLaunch;
struct NotifyRespawn;
struct NotifySpawnData;
struct NotifySpellCast;
struct NotifyInterruptCast;
struct NotifyVRCombatHitEvent;
struct NotifyVREquipmentUpdate;
struct NotifyVRGrabEvent;
struct NotifyVRHiggsState;
struct NotifyVRAppearance;
struct NotifyVRMagicEffectEvent;
struct NotifyVRPoseUpdate;
struct NotifyVRProjectileEvent;
struct ServerReferencesMoveRequest;
namespace SkyrimTogetherVR { struct RemoteGameplayBridgeResultEvent; }
namespace SkyrimTogetherVR { struct LocalGameplayBridgeEvent; }
struct TransportService;
struct VRAvatarService;
struct VRNpcOwnershipService;
struct World;
struct UpdateEvent;

/**
 * Maps remote server gameplay messages to fixed GameplayActionPayload records.
 *
 * Payload encoding is action-specific but always deterministic: LocalFormIdA-D
 * hold translated GameIds in wire-field order, ValueA-B hold integral wire
 * fields, ScalarA-D hold floating wire fields, and ActionFlags packs boolean,
 * enum, and compact node metadata. Pose and equipment indices occupy bits 8+;
 * boolean flags occupy bits 0-4. Opaque strings/appearance blobs are never
 * decoded here; face-tint color/type/alpha is emitted when supplied.
 */
struct VRActorReplicationService
{
    VRActorReplicationService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                              VRAvatarService& aAvatars, VRNpcOwnershipService& aNpcOwnership) noexcept;
    ~VRActorReplicationService() noexcept = default;

    TP_NOCOPYMOVE(VRActorReplicationService);

private:
    struct SequenceLedger
    {
        std::uint32_t LastSequence{0};
        std::uint64_t LastSignature{0};
        bool HasSequence{false};
        bool HasSignature{false};
    };

    // Channel 0 is canonical/domain traffic; channel 1 is the independent
    // dedicated grab observer. Their producer sequence counters are unrelated.
    using DomainLedgers = std::array<SequenceLedger, 36>;

    struct PendingMagicEffect
    {
        NotifyAddTarget Message{};
        std::uint8_t Attempts{0};
    };

    struct LocalActorActionTransaction
    {
        SkyrimTogetherVR::GameplayBridge::AdapterHandle TargetHandle{};
        SkyrimTogetherVR::GameplayBridge::ActorActionPayload Metadata{};
        SkyrimTogetherVR::AnimationGraphProtocol::SnapshotBuffer Snapshot{};
        std::array<std::array<char, SkyrimTogetherVR::GameplayBridge::kGameplayTextBytesPerChunk>,
                   SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks> TextChunks{};
        std::array<std::uint16_t, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks> TextLengths{};
        std::bitset<SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks> TextReceived{};
        std::uint32_t ActorLocalFormId{};
        std::uint16_t TextChunkCount{};
        std::uint64_t TextId{};
        std::uint64_t Order{};
        bool HasMetadata{false};
    };

    struct PendingRemoteActorAction
    {
        std::uint32_t ServerId{};
        ActionEvent Action{};
    };

    struct SpawnActionTracking
    {
        std::uint32_t ServerId{0};
        std::uint16_t RemainingResults{0};
        double ResultWaitElapsed{};
    };

    struct PendingEquipmentEntry
    {
        std::uint32_t LocalFormId{};
        std::int32_t Count{};
        std::uint32_t Flags{};
    };

    struct PendingEquipmentApplication
    {
        std::uint64_t TransactionId{};
        std::uint32_t LeftSpell{};
        std::uint32_t RightSpell{};
        std::uint32_t Shout{};
        std::vector<PendingEquipmentEntry> Entries{};
        std::uint64_t ActionId{};
        std::uint8_t ResultFailures{};
        double ResultWaitElapsed{};
        bool AwaitingResult{};
    };

    struct EquipmentActionTracking
    {
        std::uint32_t ServerId{};
        std::uint64_t TransactionId{};
    };

    enum class MagicEffectSubmitResult : std::uint8_t
    {
        Submitted,
        AwaitingActor,
        Rejected,
    };

    void OnCharacterSpawn(const CharacterSpawnRequest& acMessage) noexcept;
    void OnReferencesMove(const ServerReferencesMoveRequest& acMessage) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDrawWeapon(const NotifyDrawWeapon& acMessage) noexcept;
    void OnEquipment(const NotifyEquipmentChanges& acMessage) noexcept;
    void OnFactionsChanges(const NotifyFactionsChanges& acMessage) noexcept;
    void OnInventory(const NotifyInventoryChanges& acMessage) noexcept;
    void OnActorValues(const NotifyActorValueChanges& acMessage) noexcept;
    void OnActorMaximums(const NotifyActorMaxValueChanges& acMessage) noexcept;
    void OnHealthChangeBroadcast(const NotifyHealthChangeBroadcast& acMessage) noexcept;
    void OnDeath(const NotifyDeathStateChange& acMessage) noexcept;
    void OnRespawn(const NotifyRespawn& acMessage) noexcept;
    void OnMount(const NotifyMount& acMessage) noexcept;
    void OnProjectile(const NotifyProjectileLaunch& acMessage) noexcept;
    void OnSpawnData(const NotifySpawnData& acMessage) noexcept;
    void OnSpellCast(const NotifySpellCast& acMessage) noexcept;
    void OnInterruptCast(const NotifyInterruptCast& acMessage) noexcept;
    void OnNotifyRemoveSpell(const NotifyRemoveSpell& acMessage) noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept;
    void OnNotifyAddTarget(const NotifyAddTarget& acMessage) noexcept;
    void OnVrEquipment(const NotifyVREquipmentUpdate& acMessage) noexcept;
    void OnVrCombat(const NotifyVRCombatHitEvent& acMessage) noexcept;
    void OnVrMagic(const NotifyVRMagicEffectEvent& acMessage) noexcept;
    void OnVrProjectile(const NotifyVRProjectileEvent& acMessage) noexcept;
    void OnVrPose(const NotifyVRPoseUpdate& acMessage) noexcept;
    void OnVrHiggs(const NotifyVRHiggsState& acMessage) noexcept;
    void OnVrAppearance(const NotifyVRAppearance& acMessage) noexcept;
    void OnVrGrab(const NotifyVRGrabEvent& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnPlayerLevel(const NotifyPlayerLevel& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnGameplayResult(const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept;
    void OnLocalGameplay(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept;

    [[nodiscard]] bool Accept(std::uint32_t aPlayerId, SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                              std::uint32_t aSequence, std::uint64_t aSignature,
                              std::uint8_t aChannel = 0) noexcept;
    [[nodiscard]] bool ApplyForPlayer(std::uint32_t aPlayerId, SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                      SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                      const SkyrimTogetherVR::GameplayBridge::GameplayActionPayload& acPayload) noexcept;
    [[nodiscard]] bool ApplyForServer(std::uint32_t aServerId, SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                      SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                      const SkyrimTogetherVR::GameplayBridge::GameplayActionPayload& acPayload) noexcept;
    [[nodiscard]] bool ApplyForTarget(std::uint32_t aServerId, SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                      SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                      const SkyrimTogetherVR::GameplayBridge::GameplayActionPayload& acPayload) noexcept;
    [[nodiscard]] bool ApplyTextForPlayer(std::uint32_t aPlayerId,
                                          SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                          SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                          std::uint64_t aTextId, std::string_view acText) noexcept;
    [[nodiscard]] bool ApplyTextForServer(std::uint32_t aServerId,
                                          SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain,
                                          SkyrimTogetherVR::GameplayBridge::GameplayAction aAction,
                                          std::uint64_t aTextId, std::string_view acText) noexcept;
    [[nodiscard]] std::uint32_t PlayerForServer(std::uint32_t aServerId) const noexcept;
    [[nodiscard]] bool SubmitSpawn(const CharacterSpawnRequest& acMessage) noexcept;
    [[nodiscard]] bool ApplyVRAppearance(std::uint32_t aPlayerId, const VRAppearance& acAppearance) noexcept;
    [[nodiscard]] MagicEffectSubmitResult SubmitMagicEffect(const NotifyAddTarget& acMessage) noexcept;
    [[nodiscard]] bool TryApplyMount(std::uint32_t aRiderServerId, std::uint32_t aMountServerId) noexcept;
    [[nodiscard]] bool HasExactActorActionCapability() const noexcept;
    [[nodiscard]] bool IsCurrentActorActionRecord(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) const noexcept;
    [[nodiscard]] LocalActorActionTransaction& GetOrCreateLocalActorAction(std::uint64_t aActionId) noexcept;
    [[nodiscard]] bool TryCommitLocalActorAction(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    [[nodiscard]] bool HasHumanoidActorActionVariables(const ActionEvent& acAction) const noexcept;
    [[nodiscard]] bool SubmitRemoteActorAction(std::uint32_t aServerId, const ActionEvent& acAction) noexcept;
    [[nodiscard]] bool TrySubmitEquipmentApplication(std::uint32_t aServerId,
                                                     PendingEquipmentApplication& arPending) noexcept;
    void ForgetEquipmentApplication(std::uint32_t aServerId) noexcept;
    [[nodiscard]] std::uint64_t NextRemoteActorActionId() noexcept;
    void QueueRemoteActorAction(std::uint32_t aServerId, const ActionEvent& acAction) noexcept;
    void ForgetSpawnActionIds(std::uint32_t aServerId) noexcept;
    [[nodiscard]] bool HasSpawnActionIds(std::uint32_t aServerId) const noexcept;
    void ForgetPlayer(std::uint32_t aPlayerId) noexcept;
    void ForgetServer(std::uint32_t aServerId) noexcept;

    World& m_world;
    TransportService& m_transport;
    VRAvatarService& m_avatars;
    VRNpcOwnershipService& m_npcOwnership;
    std::unordered_map<std::uint32_t, std::uint32_t> m_serverPlayers{};
    std::unordered_map<std::uint32_t, CharacterSpawnRequest> m_pendingSpawns{};
    std::unordered_map<std::uint32_t, CharacterSpawnRequest> m_spawnSnapshots{};
    std::unordered_map<std::uint32_t, VRAppearance> m_latestAppearances{};
    std::unordered_map<std::uint32_t, std::uint32_t> m_pendingMounts{};
    std::unordered_map<std::uint32_t, std::uint8_t> m_resyncAttempts{};
    std::unordered_map<std::uint32_t, DomainLedgers> m_ledgers{};
    std::unordered_map<std::uint32_t, std::uint64_t> m_lastEquipmentTransactionByServer{};
    std::unordered_map<std::uint32_t, PendingEquipmentApplication> m_pendingEquipmentApplications{};
    std::unordered_map<std::uint64_t, EquipmentActionTracking> m_equipmentActionOwners{};
    std::vector<PendingMagicEffect> m_pendingMagicEffects{};
    std::unordered_map<std::uint64_t, LocalActorActionTransaction> m_localActorActions{};
    std::vector<PendingRemoteActorAction> m_pendingRemoteActorActions{};
    std::unordered_map<std::uint64_t, SpawnActionTracking> m_spawnActionOwners{};
    std::uint32_t m_recordingSpawnServerId{0};
    std::uint32_t m_localServerId{0};
    std::uint64_t m_observedLifecycleEpoch{0};
    std::uint64_t m_nextLocalActorActionOrder{1};
    std::uint64_t m_nextRemoteActorActionId{1};
    bool m_replayAfterLifecycleBoundary{false};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_characterSpawnConnection;
    entt::scoped_connection m_referencesMoveConnection;
    entt::scoped_connection m_drawWeaponConnection;
    entt::scoped_connection m_equipmentConnection;
    entt::scoped_connection m_factionsConnection;
    entt::scoped_connection m_inventoryConnection;
    entt::scoped_connection m_actorValuesConnection;
    entt::scoped_connection m_actorMaximumsConnection;
    entt::scoped_connection m_healthChangeConnection;
    entt::scoped_connection m_deathConnection;
    entt::scoped_connection m_respawnConnection;
    entt::scoped_connection m_mountConnection;
    entt::scoped_connection m_projectileConnection;
    entt::scoped_connection m_spawnDataConnection;
    entt::scoped_connection m_spellCastConnection;
    entt::scoped_connection m_interruptCastConnection;
    entt::scoped_connection m_removeSpellConnection;
    entt::scoped_connection m_removeCharacterConnection;
    entt::scoped_connection m_addTargetConnection;
    entt::scoped_connection m_vrEquipmentConnection;
    entt::scoped_connection m_vrCombatConnection;
    entt::scoped_connection m_vrMagicConnection;
    entt::scoped_connection m_vrProjectileConnection;
    entt::scoped_connection m_vrPoseConnection;
    entt::scoped_connection m_vrHiggsConnection;
    entt::scoped_connection m_vrAppearanceConnection;
    entt::scoped_connection m_vrGrabConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_playerLevelConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_gameplayResultConnection;
    entt::scoped_connection m_localGameplayConnection;
};
