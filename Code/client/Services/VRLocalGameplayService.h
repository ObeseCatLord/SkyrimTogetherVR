#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include <vr_common/VRGameplayBridge.h>
#include <Structs/MagicEquipment.h>
#include <Structs/ObjectData.h>
#include <Structs/VRAppearance.h>

struct DisconnectedEvent;
struct NotifySyncExperience;
struct TransportService;
struct UpdateEvent;
struct World;

namespace SkyrimTogetherVR
{
struct LocalGameplayBridgeEvent;
}

/**
 * Translates only bridge actions with an exact original Skyrim Together
 * request representation. The local avatar owner supplies its assigned server
 * actor ID after assignment; until then records are intentionally discarded.
 */
struct VRLocalGameplayService
{
    VRLocalGameplayService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRLocalGameplayService() noexcept = default;

    TP_NOCOPYMOVE(VRLocalGameplayService);

    void SetLocalServerId(std::uint32_t aServerId) noexcept;
    void ArmGoldInventoryDeltaSuppression(std::int32_t aCount) noexcept;
    void CancelGoldInventoryDeltaSuppression() noexcept;
    [[nodiscard]] bool HasGoldInventoryDeltaSuppression() const noexcept;

private:
    void OnLocalGameplayBridgeEvent(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnNotifySyncExperience(const NotifySyncExperience& acMessage) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    [[nodiscard]] bool AcceptAction(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    [[nodiscard]] bool AcceptAppearanceText(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) const noexcept;
    [[nodiscard]] bool HasMappedLocalPlayerForm(
        const SkyrimTogetherVR::GameplayBridge::GameplayActionPayload& acPayload) const noexcept;
    [[nodiscard]] bool ApplyAppearanceAction(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    [[nodiscard]] bool ApplyAppearanceText(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    [[nodiscard]] bool ApplyObjectSnapshot(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    [[nodiscard]] bool ApplyEquipmentSnapshot(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    [[nodiscard]] bool ApplyProjectileLaunch(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    [[nodiscard]] std::uint32_t GetServerIdForLocalActor(std::uint32_t aLocalFormId) const noexcept;
    [[nodiscard]] bool ConsumeInventoryDeltaSuppression(
        const SkyrimTogetherVR::GameplayBridge::GameplayActionPayload& acPayload) noexcept;
    template <class T>
    [[nodiscard]] bool SendStateful(T&& aRequest, std::size_t aDomainIndex, std::uint64_t aActionId,
                                    bool aCoalesce, std::uint64_t aCoalesceKey) noexcept;
    void QueuePendingStatefulSend(std::function<bool()>&& aTrySend, bool aCoalesce,
                                  std::uint64_t aCoalesceKey) noexcept;
    void QueuePendingInventoryDelta(GameId aBaseId, std::int32_t aCount, bool aIsQuestItem, bool aDrop,
                                    std::size_t aDomainIndex, std::uint64_t aActionId) noexcept;
    void TrySendPendingOutbound() noexcept;
    void MarkActionAccepted(std::size_t aDomainIndex, std::uint64_t aActionId) noexcept;
    void TryArmLocalCapture() noexcept;
    [[nodiscard]] bool PublishAppearance(std::size_t aDomainIndex, std::uint64_t aActionId) noexcept;
    void FlushPendingHealthDelta() noexcept;
    void TrySendPendingMount() noexcept;
    void TrySendPendingPackage() noexcept;
    void ResetSessionState() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::array<std::uint64_t, 18> m_lastActionIdByDomain{};
    std::array<bool, 18> m_hasActionIdByDomain{};
    struct NameAssembly
    {
        std::uint64_t TextId{0};
        std::uint16_t ChunkCount{0};
        std::uint16_t ReceivedMask{0};
        std::array<std::uint16_t, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks> Lengths{};
        std::array<char, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks *
                             SkyrimTogetherVR::GameplayBridge::kGameplayTextBytesPerChunk> Bytes{};
    };
    VRAppearance m_appearance{};
    NameAssembly m_nameAssembly{};
    struct PendingObjectSnapshot
    {
        ObjectData Data{};
        bool Ignore{false};
    };
    struct PendingInventoryDeltaSuppression
    {
        std::uint32_t LocalFormId{0};
        std::int32_t Count{0};
        double Remaining{0.0};
    };
    struct PendingStatefulSend
    {
        std::function<bool()> TrySend{};
        std::uint64_t CoalesceKey{};
        bool Coalesce{};
        std::uint64_t Order{};
    };
    struct PendingInventoryDelta
    {
        GameId BaseId{};
        std::int32_t Count{};
        std::size_t DomainIndex{};
        std::uint64_t ActionId{};
        bool IsQuestItem{};
        bool Drop{};
        std::uint64_t Order{};
    };
    struct EquipmentSnapshotItem
    {
        std::uint32_t LocalFormId{};
        GameId ServerFormId{};
        std::int32_t Count{};
        bool Worn{};
        bool WornLeft{};
        bool Weapon{};
        bool Ammo{};
    };
    struct PendingEquipmentSnapshot
    {
        std::uint64_t TransactionId{};
        std::uint64_t ServerInstanceNonce{};
        std::uint64_t ConnectionGeneration{};
        std::uint64_t LifecycleEpoch{};
        std::array<std::uint32_t, 3> LocalMagicForms{};
        std::uint16_t ExpectedEntries{};
        std::vector<EquipmentSnapshotItem> Items{};
    };
    std::unordered_map<std::uint32_t, PendingObjectSnapshot> m_pendingObjectSnapshots{};
    PendingEquipmentSnapshot m_pendingEquipmentSnapshot{};
    std::vector<EquipmentSnapshotItem> m_equipmentBaseline{};
    bool m_hasEquipmentBaseline{false};
    MagicEquipment m_magicEquipmentBaseline{};
    bool m_hasMagicEquipmentBaseline{false};
    std::uint64_t m_equipmentSessionServerInstanceNonce{};
    std::uint64_t m_equipmentSessionConnectionGeneration{};
    std::uint64_t m_equipmentSessionLifecycleEpoch{};
    std::uint64_t m_lastLocalProjectileSequence{};
    std::deque<PendingStatefulSend> m_pendingStatefulSends{};
    std::deque<PendingInventoryDelta> m_pendingInventoryDeltas{};
    std::uint64_t m_nextPendingSendOrder{};
    std::uint64_t m_pendingSendServerInstanceNonce{};
    std::uint64_t m_pendingSendConnectionGeneration{};
    std::uint64_t m_pendingSendLifecycleEpoch{};
    bool m_localCaptureArmPending{false};
    bool m_localCaptureArmed{false};
    PendingInventoryDeltaSuppression m_pendingInventoryDeltaSuppression{};
    std::uint32_t m_localServerId{0};
    std::uint16_t m_lastPublishedPlayerLevel{0};
    std::uint32_t m_pendingMountLocalReference{0};
    std::size_t m_pendingMountDomainIndex{};
    std::uint64_t m_pendingMountActionId{};
    GameId m_pendingPackageId{};
    std::size_t m_pendingPackageDomainIndex{};
    std::uint64_t m_pendingPackageActionId{};
    GameId m_lastSentPackageId{};
    double m_packageSendElapsed{};
    std::uint32_t m_lastLocalCombatSkill{};
    float m_cachedExperience{};
    double m_experienceSendElapsed{};
    float m_pendingHealthDelta{};
    double m_healthSendElapsed{};
    entt::scoped_connection m_localGameplayConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_syncExperienceConnection;
    entt::scoped_connection m_updateConnection;
};
