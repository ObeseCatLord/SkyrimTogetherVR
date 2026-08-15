#include <TiltedOnlinePCH.h>

#include <Services/VRLocalGameplayService.h>

#include <VRCanonicalEntityIdentity.h>

#include <Events/DisconnectedEvent.h>
#include <Events/LocalGameplayBridgeEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/AssignObjectsRequest.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestDeathStateChange.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/RequestQuestUpdate.h>
#include <Messages/RequestVRAppearance.h>
#include <Messages/RequestVRGrabEvent.h>
#include <Messages/RemoveSpellRequest.h>
#include <Messages/PlayerLevelRequest.h>
#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/ActivateRequest.h>
#include <Messages/AddTargetRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/MountRequest.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/SpellCastRequest.h>
#include <Messages/SyncExperienceRequest.h>
#include <Games/Magic/MagicSystem.h>
#include <Forms/TESForm.h>
#include <Services/TransportService.h>
#include <Services/PartyService.h>
#include <Services/VRAvatarService.h>
#include <Services/VRNpcOwnershipService.h>
#include <VRGameplayBridge.h>
#include <Structs/GameId.h>
#include <World.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace GameplayBridge = SkyrimTogetherVR::GameplayBridge;

namespace
{
constexpr std::int32_t kMaximumInventoryDelta = 10'000;
constexpr std::uint32_t kGoldFormId = 0xF;
constexpr std::uint32_t kHealthActorValue = 24;
constexpr float kMaximumActorValueMagnitude = 1'000'000.0F;
constexpr float kMaximumNetworkPosition = static_cast<float>((1 << 20) - 1);
constexpr std::uint32_t kMagicEffectAreaTarget = 1u << 0;
constexpr std::uint32_t kMagicEffectDualCasted = 1u << 1;
constexpr std::uint32_t kMagicEffectHostile = 1u << 2;
constexpr std::uint32_t kMagicEffectApplyHealPerkBonus = 1u << 3;
constexpr std::uint32_t kMagicEffectApplyStaminaPerkBonus = 1u << 4;
constexpr std::uint32_t kMagicEffectKnownFlags = kMagicEffectAreaTarget | kMagicEffectDualCasted |
                                                  kMagicEffectHostile | kMagicEffectApplyHealPerkBonus |
                                                  kMagicEffectApplyStaminaPerkBonus;
constexpr std::size_t kMaximumPendingObjectSnapshots = 512;
constexpr std::uint32_t kMaximumObjectSnapshotItems = 512;
constexpr std::uint32_t kMaximumObjectSnapshotEffects = 512;
constexpr double kObjectSnapshotLifetime = 10.0;
constexpr std::size_t kMaximumWornEquipmentEntries = 64;
constexpr double kInventoryDeltaSuppressionLifetime = 5.0;
constexpr std::size_t kMaximumPendingStatefulSends = 256;
constexpr std::size_t kMaximumPendingInventoryDeltas = GameplayBridge::kMaximumInventoryTransactionItems;
constexpr std::uint64_t kAppearanceSendCoalesceKey = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t kEquipmentSendCoalesceKey = kAppearanceSendCoalesceKey - 1;
constexpr std::uint64_t kPlayerLevelSendCoalesceKey = kAppearanceSendCoalesceKey - 2;

[[nodiscard]] bool IsZero(const void* apData, const std::size_t aSize) noexcept
{
    const auto* bytes = static_cast<const std::uint8_t*>(apData);
    for (std::size_t index = 0; index < aSize; ++index)
    {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

[[nodiscard]] bool IsKnownBoolean(const std::int32_t aValue) noexcept
{
    return aValue == 0 || aValue == 1;
}

[[nodiscard]] bool IsUnsupportedLocalGameplayAction(
    const GameplayBridge::GameplayDomain aDomain,
    const GameplayBridge::GameplayAction aAction) noexcept
{
    // SetCombatTarget has no original wire message or server relay. Reject it
    // before acknowledgement so bridge capture cannot imply replication.
    return aDomain == GameplayBridge::GameplayDomain::Combat &&
           aAction == GameplayBridge::GameplayAction::SetCombatTarget;
}

[[nodiscard]] bool RequiresMappedLocalPlayerForm(
    const GameplayBridge::GameplayDomain aDomain,
    const GameplayBridge::GameplayAction aAction) noexcept
{
    // LockChangeRequest identifies only the changed reference and its cell.
    // The local-player bridge target binds the event to this client but has no
    // representation in the original request payload.
    if (aDomain == GameplayBridge::GameplayDomain::Object &&
        aAction == GameplayBridge::GameplayAction::SetLockState)
        return false;
    if (aDomain == GameplayBridge::GameplayDomain::Magic &&
        (aAction == GameplayBridge::GameplayAction::CastSpell ||
         aAction == GameplayBridge::GameplayAction::InterruptCast ||
         aAction == GameplayBridge::GameplayAction::ApplyMagicEffect))
        return false;
    return true;
}

} // namespace

template <class T>
bool VRLocalGameplayService::SendStateful(T&& aRequest, const std::size_t aDomainIndex,
                                          const std::uint64_t aActionId, const bool aCoalesce,
                                          const std::uint64_t aCoalesceKey) noexcept try
{
    using Request = std::decay_t<T>;
    Request request{std::forward<T>(aRequest)};
    if (m_pendingStatefulSends.empty() && m_pendingInventoryDeltas.empty() && m_transport.Send(request))
    {
        MarkActionAccepted(aDomainIndex, aActionId);
        return true;
    }

    QueuePendingStatefulSend(
        [this, request = std::move(request), aDomainIndex, aActionId]() mutable {
            if (!m_transport.Send(request))
                return false;
            MarkActionAccepted(aDomainIndex, aActionId);
            return true;
        },
        aCoalesce, aCoalesceKey);
    return false;
}
catch (...)
{
    spdlog::error("VR local gameplay outbound staging failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
    return false;
}

VRLocalGameplayService::VRLocalGameplayService(
    World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld), m_transport(aTransport)
{
    m_localGameplayConnection =
        aDispatcher.sink<SkyrimTogetherVR::LocalGameplayBridgeEvent>().connect<&VRLocalGameplayService::OnLocalGameplayBridgeEvent>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRLocalGameplayService::OnDisconnected>(this);
    m_syncExperienceConnection = aDispatcher.sink<NotifySyncExperience>().connect<&VRLocalGameplayService::OnNotifySyncExperience>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRLocalGameplayService::OnUpdate>(this);
}

void VRLocalGameplayService::SetLocalServerId(const std::uint32_t aServerId) noexcept
{
    if (aServerId == 0)
    {
        ResetSessionState();
        return;
    }

    if (m_localServerId == aServerId)
        return;

    ResetSessionState();
    m_localServerId = aServerId;
    m_pendingSendServerInstanceNonce = m_transport.GetServerInstanceNonce();
    m_pendingSendConnectionGeneration = m_transport.GetConnectionGeneration();
    m_pendingSendLifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    m_localCaptureArmPending = true;
    m_localCaptureArmed = false;
    TryArmLocalCapture();
}

bool VRLocalGameplayService::SeedLocalAppearance(const VRAppearance& acAppearance) noexcept
{
    if (m_localServerId == 0 || !acAppearance.IsValid())
        return false;
    m_appearance = acAppearance;
    m_appearanceDirty = false;
    return true;
}

bool VRLocalGameplayService::IsCurrentBridgeRecord(
    const GameplayBridge::MessageHeader& acHeader) const noexcept
{
    return SkyrimTogetherVR::GameplayBridgeClient::IsReady() &&
        acHeader.Identity.ServerInstanceNonce != 0 &&
        acHeader.Identity.ServerInstanceNonce == m_transport.GetServerInstanceNonce() &&
        acHeader.Identity.ConnectionGeneration != 0 &&
        acHeader.Identity.ConnectionGeneration == m_transport.GetConnectionGeneration() &&
        acHeader.Identity.LifecycleEpoch != 0 &&
        acHeader.Identity.LifecycleEpoch == SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
}

void VRLocalGameplayService::ArmGoldInventoryDeltaSuppression(const std::int32_t aCount) noexcept
{
    if (aCount >= 0 || aCount < -kMaximumInventoryDelta)
    {
        CancelGoldInventoryDeltaSuppression();
        return;
    }

    m_pendingInventoryDeltaSuppression = {kGoldFormId, aCount, kInventoryDeltaSuppressionLifetime};
}

void VRLocalGameplayService::CancelGoldInventoryDeltaSuppression() noexcept
{
    m_pendingInventoryDeltaSuppression = {};
}

bool VRLocalGameplayService::HasGoldInventoryDeltaSuppression() const noexcept
{
    return m_pendingInventoryDeltaSuppression.Remaining > 0.0;
}

void VRLocalGameplayService::OnLocalGameplayBridgeEvent(
    const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept try
{
    const auto& record = acEvent.Record;
    if (!m_transport.IsOnline() || m_localServerId == 0)
        return;
    if (!IsCurrentBridgeRecord(record.Header)) {
        m_pendingObjectSnapshots.clear();
        m_pendingInventoryTransactions.clear();
        m_pendingEquipmentSnapshot = {};
        m_nameAssembly = {};
        return;
    }

    if (record.Header.Kind == static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalProjectileLaunch))
    {
        TP_UNUSED(ApplyProjectileLaunch(record));
        return;
    }

    if (record.Header.Kind == static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayTextChunk))
    {
        ApplyAppearanceText(record);
        return;
    }
    if (record.Header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayAction))
        return;

    const auto& gameplayPayload = record.Payload.LocalGameplayAction;
    const auto gameplayDomain = static_cast<GameplayBridge::GameplayDomain>(gameplayPayload.Domain);
    const auto gameplayAction = static_cast<GameplayBridge::GameplayAction>(gameplayPayload.Action);
    const bool equipmentSnapshot = gameplayDomain == GameplayBridge::GameplayDomain::Equipment &&
        gameplayAction >= GameplayBridge::GameplayAction::EquipmentSnapshotBegin &&
        gameplayAction <= GameplayBridge::GameplayAction::EquipmentSnapshotEnd;
    if (equipmentSnapshot && SkyrimTogetherVR::GameplayBridgeClient::IsReady() &&
        record.Header.Identity.ServerInstanceNonce == m_transport.GetServerInstanceNonce() &&
        record.Header.Identity.ConnectionGeneration == m_transport.GetConnectionGeneration() &&
        record.Header.Identity.LifecycleEpoch == SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() &&
        (m_equipmentSessionServerInstanceNonce != record.Header.Identity.ServerInstanceNonce ||
         m_equipmentSessionConnectionGeneration != record.Header.Identity.ConnectionGeneration ||
         m_equipmentSessionLifecycleEpoch != record.Header.Identity.LifecycleEpoch)) {
        m_pendingEquipmentSnapshot = {};
        m_equipmentBaseline = {};
        m_hasEquipmentBaseline = false;
        m_magicEquipmentBaseline = {};
        m_hasMagicEquipmentBaseline = false;
        m_hasActionIdByDomain[static_cast<std::size_t>(GameplayBridge::GameplayDomain::Equipment)] = false;
        m_lastActionIdByDomain[static_cast<std::size_t>(GameplayBridge::GameplayDomain::Equipment)] = 0;
        m_equipmentSessionServerInstanceNonce = record.Header.Identity.ServerInstanceNonce;
        m_equipmentSessionConnectionGeneration = record.Header.Identity.ConnectionGeneration;
        m_equipmentSessionLifecycleEpoch = record.Header.Identity.LifecycleEpoch;
    }

    if (!AcceptAction(record))
        return;

    const auto& payload = record.Payload.LocalGameplayAction;
    const auto domain = static_cast<GameplayBridge::GameplayDomain>(payload.Domain);
    const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
    const bool objectSnapshot = domain == GameplayBridge::GameplayDomain::Object &&
        GameplayBridge::IsObjectSnapshotAction(action);
    if (objectSnapshot)
    {
        ApplyObjectSnapshot(record);
        return;
    }
    const bool inventoryTransaction = domain == GameplayBridge::GameplayDomain::Inventory &&
        GameplayBridge::IsInventoryTransactionAction(action);
    if (inventoryTransaction)
    {
        ApplyInventoryTransaction(record);
        return;
    }
    const bool equipmentSnapshotAction = domain == GameplayBridge::GameplayDomain::Equipment &&
        action >= GameplayBridge::GameplayAction::EquipmentSnapshotBegin &&
        action <= GameplayBridge::GameplayAction::EquipmentSnapshotEnd;
    if (equipmentSnapshotAction)
    {
        ApplyEquipmentSnapshot(record);
        return;
    }
    if (RequiresMappedLocalPlayerForm(domain, action) && !HasMappedLocalPlayerForm(payload))
        return;

    if (domain == GameplayBridge::GameplayDomain::Appearance ||
        (domain == GameplayBridge::GameplayDomain::ActorState &&
         (action == GameplayBridge::GameplayAction::SetLevel ||
          action == GameplayBridge::GameplayAction::SetEssential)))
    {
        ApplyAppearanceAction(record);
        return;
    }

    const auto domainIndex = static_cast<std::size_t>(payload.Domain);

    switch (action)
    {
    case GameplayBridge::GameplayAction::AnimationEvent:
    {
        if (domain != GameplayBridge::GameplayDomain::Animation || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdA > 23 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return;

        auto* avatars = m_world.ctx().find<VRAvatarService>();
        if (!avatars || !avatars->QueueLocalAnimationEvent(payload.LocalFormIdA))
            return;
        m_hasActionIdByDomain[domainIndex] = true;
        m_lastActionIdByDomain[domainIndex] = record.Header.Identity.ActionId;
        return;
    }
    case GameplayBridge::GameplayAction::InventoryDelta:
    {
        if (domain != GameplayBridge::GameplayDomain::Inventory || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA == 0 || payload.ValueA < -kMaximumInventoryDelta || payload.ValueA > kMaximumInventoryDelta ||
            payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            (payload.ActionFlags & ~(GameplayBridge::kInventoryQuestItem | GameplayBridge::kInventoryDrop)) != 0 ||
            ((payload.ActionFlags & GameplayBridge::kInventoryDrop) != 0 && payload.ValueA >= 0))
            return;

        RequestInventoryChanges request{};
        request.ServerId = m_localServerId;
        request.Item.Count = payload.ValueA;
        request.Item.IsQuestItem = (payload.ActionFlags & GameplayBridge::kInventoryQuestItem) != 0;
        if (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.Item.BaseId) || !request.Item.BaseId)
            return;
        request.Drop = (payload.ActionFlags & GameplayBridge::kInventoryDrop) != 0;
        request.UpdateClients = true;
        if (ConsumeInventoryDeltaSuppression(payload))
        {
            MarkActionAccepted(domainIndex, record.Header.Identity.ActionId);
            return;
        }
        QueuePendingInventoryDelta(request.Item.BaseId, request.Item.Count, request.Item.IsQuestItem, request.Drop,
                                   domainIndex, record.Header.Identity.ActionId);
        return;
    }
    case GameplayBridge::GameplayAction::CastSpell:
    {
        if (domain != GameplayBridge::GameplayDomain::Magic || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA < static_cast<std::int32_t>(MagicSystem::CastingSource::LEFT_HAND) ||
            payload.ValueA > static_cast<std::int32_t>(MagicSystem::CastingSource::INSTANT) || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || (payload.ActionFlags & ~1u) != 0)
            return;

        SpellCastRequest request{};
        request.CasterId = GetServerIdForLocalActor(payload.TargetLocalFormId);
        if (request.CasterId == 0)
            return;
        request.CastingSource = payload.ValueA;
        request.IsDualCasting = (payload.ActionFlags & 1u) != 0;
        request.DesiredTarget = payload.LocalFormIdB != 0 ? GetServerIdForLocalActor(payload.LocalFormIdB) : 0;
        if (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.SpellFormId) || !request.SpellFormId)
            return;

        TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, false, 0));
        return;
    }
    case GameplayBridge::GameplayAction::InterruptCast:
    {
        if (domain != GameplayBridge::GameplayDomain::Magic || payload.LocalFormIdA != 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA < static_cast<std::int32_t>(MagicSystem::CastingSource::LEFT_HAND) ||
            payload.ValueA > static_cast<std::int32_t>(MagicSystem::CastingSource::INSTANT) ||
            payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return;

        InterruptCastRequest request{};
        request.CasterId = GetServerIdForLocalActor(payload.TargetLocalFormId);
        request.CastingSource = payload.ValueA;
        if (request.CasterId == 0)
            return;
        TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, false, 0));
        return;
    }
    case GameplayBridge::GameplayAction::ApplyMagicEffect:
    {
        if (domain != GameplayBridge::GameplayDomain::Magic || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB == 0 || payload.LocalFormIdD != 0 || payload.ValueA < 0 ||
            payload.ValueA > 4 || payload.ValueB != 0 || !std::isfinite(payload.ScalarA) ||
            payload.ScalarA < 0.0F || payload.ScalarA > kMaximumActorValueMagnitude ||
            !std::isfinite(payload.ScalarB) || payload.ScalarB < 0.0F ||
            payload.ScalarB > kMaximumActorValueMagnitude || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || (payload.ActionFlags & ~kMagicEffectKnownFlags) != 0)
            return;

        AddTargetRequest request{};
        request.TargetId = GetServerIdForLocalActor(payload.TargetLocalFormId);
        request.CasterId = payload.LocalFormIdC != 0 ? GetServerIdForLocalActor(payload.LocalFormIdC) : 0;
        if (request.TargetId == 0 || (payload.LocalFormIdC != 0 && request.CasterId == 0) ||
            ((payload.ActionFlags & kMagicEffectHostile) != 0 && request.TargetId != m_localServerId &&
             !m_world.GetServerSettings().PvpEnabled) ||
            !m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.SpellId) || !request.SpellId ||
            !m_world.GetModSystem().GetServerModId(payload.LocalFormIdB, request.EffectId) || !request.EffectId)
            return;
        request.Magnitude = payload.ScalarA;
        request.IsDualCasting = (payload.ActionFlags & kMagicEffectDualCasted) != 0;
        request.ApplyHealPerkBonus = (payload.ActionFlags & kMagicEffectApplyHealPerkBonus) != 0;
        request.ApplyStaminaPerkBonus = (payload.ActionFlags & kMagicEffectApplyStaminaPerkBonus) != 0;
        TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, false, 0));
        return;
    }
    case GameplayBridge::GameplayAction::RemoveSpell:
    {
        if (domain != GameplayBridge::GameplayDomain::Magic || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            payload.ActionFlags != 0)
            return;

        RemoveSpellRequest request{};
        request.TargetId = GetServerIdForLocalActor(payload.TargetLocalFormId);
        if (request.TargetId == 0 ||
            !m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.SpellId) || !request.SpellId)
            return;
        TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, false, 0));
        return;
    }
    case GameplayBridge::GameplayAction::Activate:
    {
        if (domain != GameplayBridge::GameplayDomain::Object || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB == 0 || payload.LocalFormIdD != 0 || payload.ValueA < 0 ||
            payload.ValueA > std::numeric_limits<std::uint8_t>::max() || payload.ValueB < 0 ||
            payload.ValueB > std::numeric_limits<std::uint8_t>::max() || payload.ScalarA < -kMaximumNetworkPosition ||
            payload.ScalarA > kMaximumNetworkPosition || payload.ScalarB < -kMaximumNetworkPosition ||
            payload.ScalarB > kMaximumNetworkPosition || payload.ScalarC < -kMaximumNetworkPosition ||
            payload.ScalarC > kMaximumNetworkPosition || payload.ScalarD != 0.0F || payload.ActionFlags != 0 ||
            static_cast<FormType>(payload.ValueA) == FormType::Book)
            return;

        ActivateRequest request{};
        if (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.Id) || !request.Id ||
            !m_world.GetModSystem().GetServerModId(payload.LocalFormIdB, request.CellId) || !request.CellId)
            return;
        request.ActivatorId = m_localServerId;
        request.PreActivationOpenState = static_cast<std::uint8_t>(payload.ValueB);

        TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, true,
                               static_cast<std::uint64_t>(action) << 32 | payload.LocalFormIdA));
        return;
    }
    case GameplayBridge::GameplayAction::SetLockState:
    {
        if (domain != GameplayBridge::GameplayDomain::Object || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB == 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            !IsKnownBoolean(payload.ValueA) || payload.ValueB < 0 || payload.ValueB > 5 ||
            (payload.ValueA == 0 && payload.ValueB != 0) ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return;

        LockChangeRequest request{};
        if (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.Id) || !request.Id ||
            !m_world.GetModSystem().GetServerModId(payload.LocalFormIdB, request.CellId) || !request.CellId)
            return;
        request.IsLocked = payload.ValueA != 0;
        request.LockLevel = static_cast<std::uint8_t>(payload.ValueB);

        TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, true,
                               static_cast<std::uint64_t>(action) << 32 | payload.LocalFormIdA));
        return;
    }
    case GameplayBridge::GameplayAction::HiggsGrab:
    case GameplayBridge::GameplayAction::HiggsDrop:
    {
        if (domain != GameplayBridge::GameplayDomain::Higgs || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB == 0 || payload.LocalFormIdD != 0 || payload.ValueA < 0 ||
            payload.ValueA > std::numeric_limits<std::uint8_t>::max() || payload.ValueB != 0 ||
            payload.ScalarA < -kMaximumNetworkPosition || payload.ScalarA > kMaximumNetworkPosition ||
            payload.ScalarB < -kMaximumNetworkPosition || payload.ScalarB > kMaximumNetworkPosition ||
            payload.ScalarC < -kMaximumNetworkPosition || payload.ScalarC > kMaximumNetworkPosition ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return;

        RequestVRGrabEvent request{};
        auto& grab = request.Grab;
        grab.Sequence = ++m_vrGrabSequence;
        if (grab.Sequence == 0)
            grab.Sequence = ++m_vrGrabSequence;
        if (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, grab.ObjectId) || !grab.ObjectId ||
            !m_world.GetModSystem().GetServerModId(payload.LocalFormIdB, grab.CellId) || !grab.CellId ||
            (payload.LocalFormIdC != 0 &&
             (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdC, grab.WorldSpaceId) || !grab.WorldSpaceId)))
            return;
        grab.Position = glm::vec3{payload.ScalarA, payload.ScalarB, payload.ScalarC};
        grab.FormType = static_cast<std::uint8_t>(payload.ValueA);
        grab.Grabbed = action == GameplayBridge::GameplayAction::HiggsGrab;
        TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, false, 0));
        return;
    }
    case GameplayBridge::GameplayAction::DrawWeapon:
    {
        if (domain != GameplayBridge::GameplayDomain::Animation || !IsKnownBoolean(payload.ValueA) ||
            payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            payload.ActionFlags != 0)
            return;

        DrawWeaponRequest request{};
        request.Id = m_localServerId;
        request.IsWeaponDrawn = payload.ValueA != 0;
        TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, true,
                               static_cast<std::uint64_t>(action)));
        return;
    }
    case GameplayBridge::GameplayAction::SetActorValue:
    case GameplayBridge::GameplayAction::SetActorMaximum:
    {
        if (domain != GameplayBridge::GameplayDomain::ActorState ||
            payload.LocalFormIdA >= GameplayBridge::kSkyrimActorValueCount ||
            !std::isfinite(payload.ScalarA) ||
            std::abs(payload.ScalarA) > kMaximumActorValueMagnitude ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            payload.ActionFlags != 0)
            return;

        if (action == GameplayBridge::GameplayAction::SetActorValue)
        {
            RequestActorValueChanges request{};
            request.Id = m_localServerId;
            request.Values.emplace(payload.LocalFormIdA, payload.ScalarA);
            TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, true,
                                   static_cast<std::uint64_t>(action) << 32 | payload.LocalFormIdA));
        }
        else
        {
            RequestActorMaxValueChanges request{};
            request.Id = m_localServerId;
            request.Values.emplace(payload.LocalFormIdA, payload.ScalarA);
            TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, true,
                                   static_cast<std::uint64_t>(action) << 32 | payload.LocalFormIdA));
        }
        return;
    }
    case GameplayBridge::GameplayAction::SyncExperience:
    {
        if (domain != GameplayBridge::GameplayDomain::ActorState ||
            !GameplayBridge::IsCombatSkillActorValue(payload.LocalFormIdA) ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA != 0 || payload.ValueB != 0 || !std::isfinite(payload.ScalarA) ||
            payload.ScalarA <= 0.0F || payload.ScalarA > GameplayBridge::kMaximumSyncedExperience ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            payload.ActionFlags != 0)
            return;

        m_lastLocalCombatSkill = payload.LocalFormIdA;
        m_cachedExperience = std::min(
            m_cachedExperience + payload.ScalarA,
            GameplayBridge::kMaximumSyncedExperience * 4.0F);
        m_hasActionIdByDomain[domainIndex] = true;
        m_lastActionIdByDomain[domainIndex] = record.Header.Identity.ActionId;
        return;
    }
    case GameplayBridge::GameplayAction::ModifyActorValue:
    {
        if (domain != GameplayBridge::GameplayDomain::ActorState ||
            payload.LocalFormIdA != kHealthActorValue || payload.LocalFormIdB != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueA != 0 ||
            payload.ValueB != 0 || !std::isfinite(payload.ScalarA) || payload.ScalarA == 0.0F ||
            std::abs(payload.ScalarA) > kMaximumActorValueMagnitude || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return;

        const auto combined = m_pendingHealthDelta + payload.ScalarA;
        if (!std::isfinite(combined) || std::abs(combined) > kMaximumActorValueMagnitude)
            return;
        m_pendingHealthDelta = combined;
        m_hasActionIdByDomain[domainIndex] = true;
        m_lastActionIdByDomain[domainIndex] = record.Header.Identity.ActionId;
        if (std::abs(m_pendingHealthDelta) >= 1.0F)
            FlushPendingHealthDelta();
        return;
    }
    case GameplayBridge::GameplayAction::SetDeathState:
    {
        if (domain != GameplayBridge::GameplayDomain::ActorState || !IsKnownBoolean(payload.ValueA) ||
            payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            payload.ActionFlags != 0)
            return;

        RequestDeathStateChange request{};
        request.Id = m_localServerId;
        request.IsDead = payload.ValueA != 0;
        TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, true,
                               static_cast<std::uint64_t>(action)));
        return;
    }
    case GameplayBridge::GameplayAction::Mount:
    {
        if (domain != GameplayBridge::GameplayDomain::ActorState ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            payload.ActionFlags != 0)
            return;
        m_pendingMountLocalReference = payload.LocalFormIdA;
        m_pendingMountDomainIndex = domainIndex;
        m_pendingMountActionId = record.Header.Identity.ActionId;
        TrySendPendingMount();
        return;
    }
    case GameplayBridge::GameplayAction::Package:
    {
        if (domain != GameplayBridge::GameplayDomain::Dialogue || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            payload.ActionFlags != 0)
            return;

        GameId packageId{};
        if (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, packageId) || !packageId)
            return;
        if (packageId == m_lastSentPackageId && !m_pendingPackageId)
        {
            MarkActionAccepted(domainIndex, record.Header.Identity.ActionId);
            return;
        }
        m_pendingPackageId = packageId;
        m_pendingPackageDomainIndex = domainIndex;
        m_pendingPackageActionId = record.Header.Identity.ActionId;
        m_packageSendElapsed = 0.0;
        TrySendPendingPackage();
        return;
    }
    case GameplayBridge::GameplayAction::SetQuestState:
    case GameplayBridge::GameplayAction::SetQuestStage:
    {
        const bool stateUpdate = action == GameplayBridge::GameplayAction::SetQuestState;
        if (domain != GameplayBridge::GameplayDomain::Quest || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA < 0 || payload.ValueA > std::numeric_limits<std::uint16_t>::max() ||
            (stateUpdate ? (payload.ValueB != 1 && payload.ValueB != 2) : payload.ValueB != 0) ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags > 11)
            return;

        if (!m_world.GetPartyService().IsInParty())
        {
            MarkActionAccepted(domainIndex, record.Header.Identity.ActionId);
            return;
        }

        RequestQuestUpdate request{};
        if (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.Id) || !request.Id)
            return;
        request.Stage = static_cast<std::uint16_t>(payload.ValueA);
        request.Status = stateUpdate ?
            (payload.ValueB == 1 ? RequestQuestUpdate::Started : RequestQuestUpdate::Stopped) :
            RequestQuestUpdate::StageUpdate;
        request.ClientQuestType = static_cast<std::uint8_t>(payload.ActionFlags);
        TP_UNUSED(SendStateful(std::move(request), domainIndex, record.Header.Identity.ActionId, true,
                               static_cast<std::uint64_t>(action) << 32 | payload.LocalFormIdA));
        return;
    }
    default:
        // Equipment, combat, text, and VR-extension records either
        // lack required original wire fields or have a dedicated observer.
        // Never fabricate a request.
        return;
    }
}
catch (...)
{
    spdlog::error("VR local gameplay event processing failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

bool VRLocalGameplayService::ApplyAppearanceAction(const GameplayBridge::EventRecord& acRecord) noexcept
{
    const auto& payload = acRecord.Payload.LocalGameplayAction;
    const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
    GameId gameId{};

    switch (action)
    {
    case GameplayBridge::GameplayAction::SetRace:
        if (payload.LocalFormIdA == 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0 ||
            !m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, gameId) || !gameId)
            return false;
        m_appearance.RaceId = gameId;
        break;
    case GameplayBridge::GameplayAction::SetSex:
        if (!IsKnownBoolean(payload.ValueA) || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return false;
        m_appearance.Sex = static_cast<std::uint8_t>(payload.ValueA);
        break;
    case GameplayBridge::GameplayAction::SetWeight:
        if (payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 || !std::isfinite(payload.ScalarA) ||
            payload.ScalarA < 0.0F || payload.ScalarA > 100.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return false;
        m_appearance.Weight = payload.ScalarA;
        break;
    case GameplayBridge::GameplayAction::SetHairColor:
    case GameplayBridge::GameplayAction::SetFaceTexture:
        if (payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0 ||
            (payload.LocalFormIdA != 0 &&
             (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, gameId) || !gameId)))
            return false;
        if (action == GameplayBridge::GameplayAction::SetHairColor)
            m_appearance.HairColorId = gameId;
        else
            m_appearance.FaceTextureId = gameId;
        break;
    case GameplayBridge::GameplayAction::SetFaceMorph:
        if (payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA < 0 || payload.ValueA >= VRAppearance::kFaceMorphCount ||
            payload.ValueB != 0 || !std::isfinite(payload.ScalarA) ||
            std::abs(payload.ScalarA) > VRAppearance::kMaximumFaceMorphMagnitude || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return false;
        m_appearance.HasFaceData = true;
        m_appearance.FaceMorphs[static_cast<std::size_t>(payload.ValueA)] = payload.ScalarA;
        break;
    case GameplayBridge::GameplayAction::SetFacePart:
        if (payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA < 0 || payload.ValueA >= VRAppearance::kFacePartCount ||
            (payload.ValueB != VRAppearance::kFacePartDefault &&
             (payload.ValueB < 0 || payload.ValueB > VRAppearance::kMaximumFacePartPreset)) ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return false;
        m_appearance.HasFaceData = true;
        m_appearance.FaceParts[static_cast<std::size_t>(payload.ValueA)] = payload.ValueB;
        break;
    case GameplayBridge::GameplayAction::ResetFaceData:
        if (payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            payload.ActionFlags != 0)
            return false;
        m_appearance.HasFaceData = false;
        m_appearance.FaceMorphs = {};
        m_appearance.FaceParts = {};
        break;
    case GameplayBridge::GameplayAction::SetHeadPart:
    {
        if (payload.ValueA < 0 || payload.ValueA >= VRAppearance::kMaximumHeadParts || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            payload.ActionFlags != 0 || !m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, gameId) || !gameId)
            return false;
        const auto slot = static_cast<std::uint8_t>(payload.ValueA);
        auto count = m_appearance.HeadPartCount;
        for (std::uint8_t index = 0; index < count; ++index)
        {
            if (m_appearance.HeadParts[index].Slot == slot)
            {
                m_appearance.HeadParts[index].FormId = gameId;
                m_appearanceDirty = true;
                MarkActionAccepted(payload.Domain, acRecord.Header.Identity.ActionId);
                return true;
            }
        }
        if (count >= VRAppearance::kMaximumHeadParts)
            return false;
        m_appearance.HeadParts[count] = {slot, gameId};
        ++m_appearance.HeadPartCount;
        break;
    }
    case GameplayBridge::GameplayAction::ClearHeadPart:
    {
        if (payload.ValueA < 0 || payload.ValueA >= VRAppearance::kMaximumHeadParts || payload.LocalFormIdA != 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return false;
        const auto slot = static_cast<std::uint8_t>(payload.ValueA);
        for (std::uint8_t index = 0; index < m_appearance.HeadPartCount; ++index) {
            if (m_appearance.HeadParts[index].Slot != slot)
                continue;
            for (std::uint8_t next = static_cast<std::uint8_t>(index + 1);
                 next < m_appearance.HeadPartCount; ++next)
                m_appearance.HeadParts[next - 1] = m_appearance.HeadParts[next];
            --m_appearance.HeadPartCount;
            m_appearance.HeadParts[m_appearance.HeadPartCount] = {};
            break;
        }
        break;
    }
    case GameplayBridge::GameplayAction::ResetTints:
        if (payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return false;
        m_appearance.TintCount = 0;
        m_appearance.Tints = {};
        break;
    case GameplayBridge::GameplayAction::SetTint:
    {
        if (payload.ValueA < 0 || payload.ValueA >= VRAppearance::kMaximumTints || payload.LocalFormIdA != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueB < 0 || payload.ValueB >= 15 ||
            !std::isfinite(payload.ScalarA) ||
            payload.ScalarA < 0.0F || payload.ScalarA > 1.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return false;
        const auto index = static_cast<std::uint8_t>(payload.ValueA);
        if (index != m_appearance.TintCount)
            return false;
        m_appearance.Tints[index] = {
            static_cast<std::uint8_t>(payload.ValueB), payload.LocalFormIdB, payload.ScalarA};
        ++m_appearance.TintCount;
        break;
    }
    case GameplayBridge::GameplayAction::SetLevel:
        if (static_cast<GameplayBridge::GameplayDomain>(payload.Domain) != GameplayBridge::GameplayDomain::ActorState ||
            payload.ValueA <= 0 || payload.ValueA > std::numeric_limits<std::uint16_t>::max() ||
            payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            payload.ActionFlags != 0)
            return false;
        m_appearance.Level = static_cast<std::uint16_t>(payload.ValueA);
        if (m_lastPublishedPlayerLevel != m_appearance.Level)
        {
            PlayerLevelRequest request{};
            request.NewLevel = m_appearance.Level;
            const auto playerLevelPending = std::any_of(
                m_pendingStatefulSends.begin(), m_pendingStatefulSends.end(), [](const PendingStatefulSend& acPending) {
                    return acPending.Coalesce &&
                           acPending.CoalesceKey == kPlayerLevelSendCoalesceKey;
                });
            if (!playerLevelPending && m_pendingStatefulSends.empty() && m_pendingInventoryDeltas.empty() &&
                m_transport.Send(request))
                m_lastPublishedPlayerLevel = m_appearance.Level;
            else
            {
                QueuePendingStatefulSend([this, request = std::move(request)]() mutable {
                    if (!m_transport.Send(request))
                        return false;
                    m_lastPublishedPlayerLevel = request.NewLevel;
                    return true;
                }, true, kPlayerLevelSendCoalesceKey);
            }
        }
        break;
    case GameplayBridge::GameplayAction::SetEssential:
        if (static_cast<GameplayBridge::GameplayDomain>(payload.Domain) != GameplayBridge::GameplayDomain::ActorState ||
            !IsKnownBoolean(payload.ValueA) || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return false;
        m_appearance.Essential = payload.ValueA != 0;
        break;
    case GameplayBridge::GameplayAction::CommitAppearance:
        if (payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return false;
        if (!m_appearanceDirty) {
            MarkActionAccepted(payload.Domain, acRecord.Header.Identity.ActionId);
            return true;
        }
        if (!PublishAppearance(payload.Domain, acRecord.Header.Identity.ActionId))
            return false;
        m_appearanceDirty = false;
        return true;
    default:
        return false;
    }

    m_appearanceDirty = true;
    MarkActionAccepted(payload.Domain, acRecord.Header.Identity.ActionId);
    return true;
}

bool VRLocalGameplayService::AcceptAppearanceText(const GameplayBridge::EventRecord& acRecord) const noexcept
{
    const auto& header = acRecord.Header;
    const auto& payload = acRecord.Payload.LocalGameplayTextChunk;
    const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
    const bool name = action == GameplayBridge::GameplayAction::SetName && payload.Reserved0 == 0 &&
                      payload.AuxiliaryLocalFormId == 0 && payload.ChunkCount <= 3;
    const bool tintPath = action == GameplayBridge::GameplayAction::SetTint &&
                          payload.Reserved0 == GameplayBridge::kGameplayTextAppearanceDeferred &&
                          payload.AuxiliaryLocalFormId >= 1 &&
                          payload.AuxiliaryLocalFormId <= VRAppearance::kMaximumTints &&
                          payload.ChunkCount <= 6;
    return IsCurrentBridgeRecord(header) &&
           header.Kind == static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayTextChunk) &&
           header.PayloadSize == GameplayBridge::kFixedPayloadBytes && header.Flags == 0 &&
           header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 && header.Identity.SequenceId == 0 &&
           header.Identity.ActionId != 0 && payload.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value &&
           payload.TargetLocalFormId != 0 && payload.Domain == static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Appearance) &&
           (name || tintPath) && payload.TextId != 0 && payload.ChunkCount != 0 &&
           payload.ChunkIndex < payload.ChunkCount && payload.ByteCount <= GameplayBridge::kGameplayTextBytesPerChunk;
}

bool VRLocalGameplayService::ApplyAppearanceText(const GameplayBridge::EventRecord& acRecord) noexcept
{
    if (!AcceptAppearanceText(acRecord))
        return false;
    const auto& payload = acRecord.Payload.LocalGameplayTextChunk;
    if (m_hasActionIdByDomain[static_cast<std::size_t>(GameplayBridge::GameplayDomain::Appearance)] &&
        acRecord.Header.Identity.ActionId <= m_lastActionIdByDomain[static_cast<std::size_t>(GameplayBridge::GameplayDomain::Appearance)])
        return false;

    if (m_nameAssembly.TextId != payload.TextId || m_nameAssembly.ChunkCount != payload.ChunkCount ||
        m_nameAssembly.Action != payload.Action ||
        m_nameAssembly.AuxiliaryLocalFormId != payload.AuxiliaryLocalFormId) {
        m_nameAssembly = {};
        m_nameAssembly.TextId = payload.TextId;
        m_nameAssembly.ChunkCount = payload.ChunkCount;
        m_nameAssembly.Action = payload.Action;
        m_nameAssembly.AuxiliaryLocalFormId = payload.AuxiliaryLocalFormId;
    }
    const auto offset = static_cast<std::size_t>(payload.ChunkIndex) * GameplayBridge::kGameplayTextBytesPerChunk;
    std::memcpy(m_nameAssembly.Bytes.data() + offset, payload.Utf8Bytes, payload.ByteCount);
    m_nameAssembly.Lengths[payload.ChunkIndex] = payload.ByteCount;
    m_nameAssembly.ReceivedMask |= static_cast<std::uint16_t>(1u << payload.ChunkIndex);

    const auto expectedMask = static_cast<std::uint16_t>((1u << payload.ChunkCount) - 1u);
    if (m_nameAssembly.ReceivedMask != expectedMask)
        return true;

    std::size_t nameLength{};
    for (std::uint16_t index = 0; index < payload.ChunkCount; ++index)
    {
        if (index + 1 != payload.ChunkCount && m_nameAssembly.Lengths[index] != GameplayBridge::kGameplayTextBytesPerChunk)
            return false;
        nameLength += m_nameAssembly.Lengths[index];
    }
    const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
    if (action == GameplayBridge::GameplayAction::SetName) {
        if (nameLength == 0 || nameLength > VRAppearance::kMaximumNameBytes)
            return false;
        m_appearance.Name = {};
        std::memcpy(m_appearance.Name.data(), m_nameAssembly.Bytes.data(), nameLength);
        m_appearance.NameLength = static_cast<std::uint8_t>(nameLength);
    } else {
        if (nameLength == 0 || nameLength > VRAppearanceTint::kMaximumTexturePathBytes ||
            payload.AuxiliaryLocalFormId == 0 || payload.AuxiliaryLocalFormId > m_appearance.TintCount)
            return false;
        auto& tint = m_appearance.Tints[payload.AuxiliaryLocalFormId - 1];
        tint.TexturePath = {};
        std::memcpy(tint.TexturePath.data(), m_nameAssembly.Bytes.data(), nameLength);
        tint.TexturePathLength = static_cast<std::uint8_t>(nameLength);
    }
    m_appearanceDirty = true;
    MarkActionAccepted(static_cast<std::size_t>(GameplayBridge::GameplayDomain::Appearance),
                       acRecord.Header.Identity.ActionId);
    return true;
}

bool VRLocalGameplayService::PublishAppearance(const std::size_t aDomainIndex,
                                               const std::uint64_t aActionId) noexcept
{
    if (!m_appearance.RaceId)
        return false;
    ++m_appearance.Sequence;
    if (m_appearance.Sequence == 0)
        ++m_appearance.Sequence;
    if (!m_appearance.IsValid())
        return false;
    RequestVRAppearance request{};
    request.Appearance = m_appearance;
    return SendStateful(std::move(request), aDomainIndex, aActionId, true, kAppearanceSendCoalesceKey);
}

bool VRLocalGameplayService::ApplyInventoryTransaction(const GameplayBridge::EventRecord& acRecord) noexcept try
{
    const auto& header = acRecord.Header;
    const auto& payload = acRecord.Payload.LocalGameplayAction;
    const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
    const auto ownerFormId = payload.TargetLocalFormId;
    const auto reject = [this, ownerFormId]() {
        m_pendingInventoryTransactions.erase(ownerFormId);
        return false;
    };
    const auto sameTransaction = [&header](const PendingInventoryTransaction& acPending) {
        return acPending.ActionId == header.Identity.ActionId &&
               acPending.ServerInstanceNonce == header.Identity.ServerInstanceNonce &&
               acPending.ConnectionGeneration == header.Identity.ConnectionGeneration &&
               acPending.LifecycleEpoch == header.Identity.LifecycleEpoch;
    };

    if (action == GameplayBridge::GameplayAction::InventoryTransactionBegin) {
        if (payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA <= 0 ||
            payload.ValueA > static_cast<std::int32_t>(GameplayBridge::kMaximumInventoryTransactionItems) ||
            payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return reject();
        const auto serverId = GetServerIdForLocalInventoryOwner(ownerFormId);
        if (serverId == 0)
            return reject();
        if (const auto existing = m_pendingInventoryTransactions.find(ownerFormId);
            existing != m_pendingInventoryTransactions.end()) {
            if (sameTransaction(existing->second))
                return reject();
            if (existing->second.ActionId > header.Identity.ActionId)
                return false;
            m_pendingInventoryTransactions.erase(existing);
        }
        if (m_pendingInventoryTransactions.size() >= kMaximumPendingObjectSnapshots)
            return false;
        PendingInventoryTransaction pending{};
        pending.ActionId = header.Identity.ActionId;
        pending.ServerInstanceNonce = header.Identity.ServerInstanceNonce;
        pending.ConnectionGeneration = header.Identity.ConnectionGeneration;
        pending.LifecycleEpoch = header.Identity.LifecycleEpoch;
        pending.ServerId = serverId;
        pending.ExpectedItems = static_cast<std::uint16_t>(payload.ValueA);
        pending.Items.reserve(pending.ExpectedItems);
        pending.Drops.reserve(pending.ExpectedItems);
        pending.Suppressed.reserve(pending.ExpectedItems);
        m_pendingInventoryTransactions.insert_or_assign(ownerFormId, std::move(pending));
        return true;
    }

    const auto pending = m_pendingInventoryTransactions.find(ownerFormId);
    if (pending == m_pendingInventoryTransactions.end() || !sameTransaction(pending->second))
        return reject();
    auto& transaction = pending->second;
    const auto mapRequired = [this](const std::uint32_t aLocalId, GameId& arId) {
        return aLocalId != 0 && m_world.GetModSystem().GetServerModId(aLocalId, arId) && static_cast<bool>(arId);
    };
    const auto mapOptional = [this](const std::uint32_t aLocalId, GameId& arId) {
        return aLocalId == 0 ||
               (m_world.GetModSystem().GetServerModId(aLocalId, arId) && static_cast<bool>(arId));
    };
    switch (action) {
    case GameplayBridge::GameplayAction::InventoryTransactionItem:
    {
        if (transaction.Items.size() >= transaction.ExpectedItems ||
            (transaction.Items.size() != 0 &&
             (!transaction.HasOpenItemExtra || transaction.EffectsRemaining != 0)) ||
            payload.LocalFormIdA == 0 || payload.LocalFormIdB != transaction.ExpectedItems ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueA == 0 ||
            payload.ValueA < -kMaximumInventoryDelta || payload.ValueA > kMaximumInventoryDelta ||
            payload.ValueB != static_cast<std::int32_t>(transaction.Items.size()) ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F ||
            (payload.ActionFlags & ~GameplayBridge::kInventoryTransactionItemWireKnownFlags) != 0 ||
            ((payload.ActionFlags & GameplayBridge::kInventoryDrop) != 0 && payload.ValueA >= 0))
            return reject();
        Inventory::Entry item{};
        if (!mapRequired(payload.LocalFormIdA, item.BaseId))
            return reject();
        item.Count = payload.ValueA;
        item.IsQuestItem = (payload.ActionFlags & GameplayBridge::kInventoryTransactionQuestItem) != 0;
        item.ExtraWorn = (payload.ActionFlags & GameplayBridge::kInventoryTransactionWorn) != 0;
        item.ExtraWornLeft = (payload.ActionFlags & GameplayBridge::kInventoryTransactionWornLeft) != 0;
        item.EquipmentFlags =
            ((payload.ActionFlags & GameplayBridge::kInventoryTransactionWeapon) != 0 ?
                 Inventory::Entry::kEquipmentWeapon : 0u) |
            ((payload.ActionFlags & GameplayBridge::kInventoryTransactionAmmo) != 0 ?
                 Inventory::Entry::kEquipmentAmmo : 0u);
        transaction.Items.push_back(std::move(item));
        transaction.Drops.push_back((payload.ActionFlags & GameplayBridge::kInventoryDrop) != 0);
        transaction.Suppressed.push_back(ConsumeInventoryDeltaSuppression(payload));
        transaction.OpenItemIndex = transaction.Items.size() - 1;
        transaction.HasOpenItemExtra = false;
        return true;
    }
    case GameplayBridge::GameplayAction::InventoryTransactionItemExtra:
    {
        if (transaction.Items.empty() || transaction.HasOpenItemExtra ||
            transaction.OpenItemIndex != transaction.Items.size() - 1 || payload.LocalFormIdC > 5 ||
            payload.LocalFormIdD > GameplayBridge::kMaximumInventoryTransactionEffects ||
            payload.ValueA < 0 || payload.ValueA > std::numeric_limits<std::uint16_t>::max() ||
            payload.ValueB < 0 || !std::isfinite(payload.ScalarA) || !std::isfinite(payload.ScalarB) ||
            payload.ScalarA < 0.0F || payload.ScalarA > Inventory::Entry::kMaximumMutationScalarMagnitude ||
            payload.ScalarB < 0.0F || payload.ScalarB > Inventory::Entry::kMaximumMutationScalarMagnitude ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            (payload.ActionFlags & ~GameplayBridge::kInventoryTransactionExtraKnownFlags) != 0 ||
            (payload.LocalFormIdA == 0 &&
             (payload.ValueA != 0 || payload.LocalFormIdD != 0 || payload.ActionFlags != 0)) ||
            ((payload.LocalFormIdB == 0) != (payload.ValueB == 0)) ||
            transaction.TotalEffects > GameplayBridge::kMaximumInventoryTransactionEffects - payload.LocalFormIdD)
            return reject();
        auto& item = transaction.Items[transaction.OpenItemIndex];
        if (!mapOptional(payload.LocalFormIdA, item.ExtraEnchantId) ||
            !mapOptional(payload.LocalFormIdB, item.ExtraPoisonId))
            return reject();
        item.ExtraSoulLevel = static_cast<std::int32_t>(payload.LocalFormIdC);
        item.ExtraEnchantCharge = static_cast<std::uint16_t>(payload.ValueA);
        item.ExtraPoisonCount = static_cast<std::uint32_t>(payload.ValueB);
        item.ExtraCharge = payload.ScalarA;
        item.ExtraHealth = payload.ScalarB;
        item.ExtraEnchantRemoveUnequip =
            (payload.ActionFlags & GameplayBridge::kInventoryTransactionEnchantRemoveUnequip) != 0;
        item.EnchantData.IsWeapon =
            (payload.ActionFlags & GameplayBridge::kInventoryTransactionEnchantIsWeapon) != 0;
        transaction.EffectsRemaining = payload.LocalFormIdD;
        transaction.TotalEffects += payload.LocalFormIdD;
        transaction.HasOpenItemExtra = true;
        return true;
    }
    case GameplayBridge::GameplayAction::InventoryTransactionItemEffect:
    {
        if (transaction.Items.empty() || !transaction.HasOpenItemExtra ||
            transaction.EffectsRemaining == 0 || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB != transaction.OpenItemIndex ||
            payload.LocalFormIdC != transaction.Items[transaction.OpenItemIndex].EnchantData.Effects.size() ||
            payload.LocalFormIdD != transaction.Items[transaction.OpenItemIndex].EnchantData.Effects.size() +
                transaction.EffectsRemaining || payload.ValueA < 0 || payload.ValueB < 0 ||
            !std::isfinite(payload.ScalarA) || !std::isfinite(payload.ScalarB) ||
            std::abs(payload.ScalarA) > Inventory::Entry::kMaximumMutationScalarMagnitude ||
            payload.ScalarB < 0.0F || payload.ScalarB > Inventory::Entry::kMaximumMutationScalarMagnitude ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return reject();
        Inventory::EffectItem effect{};
        if (!mapRequired(payload.LocalFormIdA, effect.EffectId))
            return reject();
        effect.Area = payload.ValueA;
        effect.Duration = payload.ValueB;
        effect.Magnitude = payload.ScalarA;
        effect.RawCost = payload.ScalarB;
        transaction.Items[transaction.OpenItemIndex].EnchantData.Effects.push_back(std::move(effect));
        --transaction.EffectsRemaining;
        return true;
    }
    case GameplayBridge::GameplayAction::InventoryTransactionEnd:
    {
        if (transaction.Items.size() != transaction.ExpectedItems || transaction.Items.empty() ||
            !transaction.HasOpenItemExtra || transaction.EffectsRemaining != 0 ||
            payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return reject();
        bool queuedAny{};
        for (std::size_t index = 0; index < transaction.Items.size(); ++index) {
            if (!transaction.Items[index].IsValidMutation())
                return reject();
            if (!transaction.Suppressed[index]) {
                queuedAny = true;
                QueuePendingInventoryChange(transaction.ServerId, std::move(transaction.Items[index]),
                                            transaction.Drops[index],
                                            static_cast<std::size_t>(GameplayBridge::GameplayDomain::Inventory),
                                            header.Identity.ActionId);
            }
        }
        if (!queuedAny)
            MarkActionAccepted(static_cast<std::size_t>(GameplayBridge::GameplayDomain::Inventory),
                               header.Identity.ActionId);
        m_pendingInventoryTransactions.erase(pending);
        return true;
    }
    default:
        return reject();
    }
}
catch (...)
{
    m_pendingInventoryTransactions.erase(acRecord.Payload.LocalGameplayAction.TargetLocalFormId);
    spdlog::error("VR inventory transaction assembly failed; rebasing the gameplay epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
    return false;
}

bool VRLocalGameplayService::ApplyObjectSnapshot(const GameplayBridge::EventRecord& acRecord) noexcept try
{
    const auto& payload = acRecord.Payload.LocalGameplayAction;
    const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
    const auto domainIndex = static_cast<std::size_t>(payload.Domain);
    const auto markAccepted = [&]() {
        m_hasActionIdByDomain[domainIndex] = true;
        m_lastActionIdByDomain[domainIndex] = acRecord.Header.Identity.ActionId;
    };

    if (action == GameplayBridge::GameplayAction::ObjectSnapshotBegin)
    {
        if (payload.TargetHandle.Value != 0 || payload.TargetLocalFormId == 0 || payload.SecondaryHandle.Value != 0 ||
            payload.LocalFormIdA == 0 ||
            payload.LocalFormIdC > kMaximumObjectSnapshotItems ||
            payload.LocalFormIdD != 0 ||
            payload.ValueA < 0 || payload.ValueA > 2 || payload.ValueB < -1 || payload.ValueB > 255 ||
            payload.ScalarA < -kMaximumNetworkPosition || payload.ScalarA > kMaximumNetworkPosition ||
            payload.ScalarB < -kMaximumNetworkPosition || payload.ScalarB > kMaximumNetworkPosition ||
            payload.ScalarC < -kMaximumNetworkPosition || payload.ScalarC > kMaximumNetworkPosition ||
            payload.ScalarD != 0.0F ||
            (payload.ActionFlags & ~(GameplayBridge::kObjectSnapshotContainer |
                                     GameplayBridge::kObjectSnapshotPlayerHome)) != 0 ||
            ((payload.ActionFlags & GameplayBridge::kObjectSnapshotContainer) == 0 &&
             payload.LocalFormIdC != 0)) {
            m_pendingObjectSnapshots.erase(payload.TargetLocalFormId);
            return false;
        }

        auto existing = m_pendingObjectSnapshots.find(payload.TargetLocalFormId);
        if (existing == m_pendingObjectSnapshots.end() &&
            m_pendingObjectSnapshots.size() >= kMaximumPendingObjectSnapshots)
            return false;

        PendingObjectSnapshot pending{};
        pending.ActionId = acRecord.Header.Identity.ActionId;
        pending.ExpectedItems = payload.LocalFormIdC;
        pending.IsContainer =
            (payload.ActionFlags & GameplayBridge::kObjectSnapshotContainer) != 0;
        pending.Ignore = (payload.ActionFlags & GameplayBridge::kObjectSnapshotPlayerHome) != 0 &&
                         !m_world.GetServerSettings().SyncPlayerHomes;
        if (!pending.Ignore)
        {
            if (!m_world.GetModSystem().GetServerModId(payload.TargetLocalFormId, pending.Data.Id) ||
                !pending.Data.Id || !m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, pending.Data.CellId) ||
                !pending.Data.CellId ||
                (payload.LocalFormIdB != 0 &&
                 (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdB, pending.Data.WorldSpaceId) ||
                  !pending.Data.WorldSpaceId)))
                pending.Ignore = true;
            pending.Data.CurrentCoords = GridCellCoords::CalculateGridCellCoords(payload.ScalarA, payload.ScalarB);
            pending.Data.CurrentLockData.IsLocked = payload.ValueB >= 0;
            pending.Data.CurrentLockData.LockLevel = payload.ValueB >= 0 ?
                static_cast<std::uint8_t>(payload.ValueB) : 0;
        }
        m_pendingObjectSnapshots.insert_or_assign(payload.TargetLocalFormId, std::move(pending));
        return true;
    }

    auto pending = m_pendingObjectSnapshots.find(payload.TargetLocalFormId);
    if (pending == m_pendingObjectSnapshots.end())
        return false;
    const auto rejectPending = [&]() {
        m_pendingObjectSnapshots.erase(pending);
        return false;
    };

    if (action == GameplayBridge::GameplayAction::ObjectSnapshotItem)
    {
        constexpr auto knownFlags = GameplayBridge::kAssignmentBootstrapInventoryQuestItem |
            GameplayBridge::kAssignmentBootstrapInventoryWorn |
            GameplayBridge::kAssignmentBootstrapInventoryWornLeft |
            GameplayBridge::kAssignmentBootstrapInventoryWeapon |
            GameplayBridge::kAssignmentBootstrapInventoryAmmo;
        if (pending->second.ActionId != acRecord.Header.Identity.ActionId || !pending->second.IsContainer ||
            (pending->second.HasOpenInventory &&
             (!pending->second.HasInventoryExtra || pending->second.InventoryEffectsRemaining != 0)) ||
            payload.TargetHandle.Value != 0 || payload.SecondaryHandle.Value != 0 || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB != pending->second.ExpectedItems || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA <= 0 || payload.ValueB < 0 ||
            static_cast<std::uint32_t>(payload.ValueB) != pending->second.NextItemOrdinal ||
            pending->second.NextItemOrdinal >= pending->second.ExpectedItems ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || (payload.ActionFlags & ~knownFlags) != 0 ||
            ((payload.ActionFlags & GameplayBridge::kAssignmentBootstrapInventoryWeapon) != 0 &&
             (payload.ActionFlags & GameplayBridge::kAssignmentBootstrapInventoryAmmo) != 0))
            return rejectPending();

        if (!pending->second.Ignore)
        {
            Inventory::Entry entry{};
            entry.Count = payload.ValueA;
            entry.IsQuestItem =
                (payload.ActionFlags & GameplayBridge::kAssignmentBootstrapInventoryQuestItem) != 0;
            entry.ExtraWorn =
                (payload.ActionFlags & GameplayBridge::kAssignmentBootstrapInventoryWorn) != 0;
            entry.ExtraWornLeft =
                (payload.ActionFlags & GameplayBridge::kAssignmentBootstrapInventoryWornLeft) != 0;
            entry.EquipmentFlags =
                ((payload.ActionFlags & GameplayBridge::kAssignmentBootstrapInventoryWeapon) != 0 ?
                     Inventory::Entry::kEquipmentWeapon : 0u) |
                ((payload.ActionFlags & GameplayBridge::kAssignmentBootstrapInventoryAmmo) != 0 ?
                     Inventory::Entry::kEquipmentAmmo : 0u);
            if (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, entry.BaseId) || !entry.BaseId)
                return rejectPending();
            pending->second.Data.CurrentInventory.Entries.push_back(std::move(entry));
            pending->second.OpenInventoryIndex = pending->second.Data.CurrentInventory.Entries.size() - 1;
        }
        pending->second.HasOpenInventory = true;
        pending->second.HasInventoryExtra = false;
        pending->second.InventoryEffectsRemaining = 0;
        return true;
    }

    if (action == GameplayBridge::GameplayAction::ObjectSnapshotItemExtra)
    {
        constexpr auto knownFlags = GameplayBridge::kAssignmentBootstrapEnchantRemoveUnequip |
            GameplayBridge::kAssignmentBootstrapEnchantIsWeapon;
        if (pending->second.ActionId != acRecord.Header.Identity.ActionId ||
            !pending->second.HasOpenInventory || pending->second.HasInventoryExtra ||
            payload.TargetHandle.Value != 0 || payload.SecondaryHandle.Value != 0 ||
            (payload.ActionFlags & ~knownFlags) != 0 || payload.LocalFormIdC > 5 ||
            payload.LocalFormIdD > kMaximumObjectSnapshotEffects ||
            payload.ValueA < 0 || payload.ValueA > std::numeric_limits<std::uint16_t>::max() ||
            payload.ValueB < 0 || !std::isfinite(payload.ScalarA) || !std::isfinite(payload.ScalarB) ||
            payload.ScalarA < 0.0F || payload.ScalarB < 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F ||
            (payload.LocalFormIdA == 0 &&
             (payload.ValueA != 0 || payload.LocalFormIdD != 0 ||
              (payload.ActionFlags & knownFlags) != 0)) ||
            (payload.LocalFormIdB == 0 && payload.ValueB != 0) ||
            pending->second.TotalEffects > kMaximumObjectSnapshotEffects - payload.LocalFormIdD)
            return rejectPending();

        if (!pending->second.Ignore) {
            auto& entry = pending->second.Data.CurrentInventory.Entries[pending->second.OpenInventoryIndex];
            const auto mapOptional = [this](const std::uint32_t aLocalId, GameId& arId) {
                return aLocalId == 0 ||
                       (m_world.GetModSystem().GetServerModId(aLocalId, arId) && static_cast<bool>(arId));
            };
            if (!mapOptional(payload.LocalFormIdA, entry.ExtraEnchantId) ||
                !mapOptional(payload.LocalFormIdB, entry.ExtraPoisonId))
                return rejectPending();
            entry.ExtraEnchantCharge = static_cast<std::uint16_t>(payload.ValueA);
            entry.ExtraPoisonCount = static_cast<std::uint32_t>(payload.ValueB);
            entry.ExtraSoulLevel = static_cast<std::int32_t>(payload.LocalFormIdC);
            entry.ExtraCharge = payload.ScalarA;
            entry.ExtraHealth = payload.ScalarB;
            entry.ExtraEnchantRemoveUnequip =
                (payload.ActionFlags & GameplayBridge::kAssignmentBootstrapEnchantRemoveUnequip) != 0;
            entry.EnchantData.IsWeapon =
                (payload.ActionFlags & GameplayBridge::kAssignmentBootstrapEnchantIsWeapon) != 0;
        }
        pending->second.HasInventoryExtra = true;
        pending->second.InventoryEffectsRemaining = payload.LocalFormIdD;
        pending->second.TotalEffects += payload.LocalFormIdD;
        if (payload.LocalFormIdD == 0)
            ++pending->second.NextItemOrdinal;
        return true;
    }

    if (action == GameplayBridge::GameplayAction::ObjectSnapshotItemEffect)
    {
        if (pending->second.ActionId != acRecord.Header.Identity.ActionId ||
            !pending->second.HasOpenInventory || !pending->second.HasInventoryExtra ||
            pending->second.InventoryEffectsRemaining == 0 || payload.TargetHandle.Value != 0 ||
            payload.SecondaryHandle.Value != 0 || payload.LocalFormIdA == 0 || payload.ValueA < 0 ||
            payload.ValueB < 0 || payload.LocalFormIdB != pending->second.NextItemOrdinal ||
            payload.LocalFormIdD == 0 || payload.LocalFormIdC >= payload.LocalFormIdD ||
            payload.LocalFormIdD != pending->second.InventoryEffectsRemaining + payload.LocalFormIdC ||
            !std::isfinite(payload.ScalarA) || !std::isfinite(payload.ScalarB) ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return rejectPending();
        if (!pending->second.Ignore) {
            Inventory::EffectItem effect{};
            if (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, effect.EffectId) || !effect.EffectId)
                return rejectPending();
            effect.Area = payload.ValueA;
            effect.Duration = payload.ValueB;
            effect.Magnitude = payload.ScalarA;
            effect.RawCost = payload.ScalarB;
            pending->second.Data.CurrentInventory.Entries[pending->second.OpenInventoryIndex]
                .EnchantData.Effects.push_back(effect);
        }
        --pending->second.InventoryEffectsRemaining;
        if (pending->second.InventoryEffectsRemaining == 0)
            ++pending->second.NextItemOrdinal;
        return true;
    }

    if (action != GameplayBridge::GameplayAction::ObjectSnapshotEnd || payload.TargetHandle.Value != 0 ||
        pending->second.ActionId != acRecord.Header.Identity.ActionId ||
        payload.SecondaryHandle.Value != 0 || payload.LocalFormIdA != pending->second.ExpectedItems ||
        pending->second.NextItemOrdinal != pending->second.ExpectedItems ||
        (pending->second.HasOpenInventory &&
         (!pending->second.HasInventoryExtra || pending->second.InventoryEffectsRemaining != 0)) ||
        payload.LocalFormIdB != 0 ||
        payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
        payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
        payload.ScalarD != 0.0F || payload.ActionFlags != 0)
        return rejectPending();

    auto complete = std::move(pending->second);
    m_pendingObjectSnapshots.erase(pending);
    if (complete.Ignore)
    {
        markAccepted();
        return true;
    }

    AssignObjectsRequest request{};
    request.Objects.push_back(std::move(complete.Data));
    return SendStateful(std::move(request), domainIndex, acRecord.Header.Identity.ActionId, true,
                        static_cast<std::uint64_t>(payload.TargetLocalFormId));
}
catch (...)
{
    spdlog::error("VR object snapshot assembly failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
    return false;
}

bool VRLocalGameplayService::ApplyEquipmentSnapshot(const GameplayBridge::EventRecord& acRecord) noexcept try
{
    const auto& header = acRecord.Header;
    const auto& payload = acRecord.Payload.LocalGameplayAction;
    const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
    const auto domainIndex = static_cast<std::size_t>(payload.Domain);
    const auto sessionCurrent = SkyrimTogetherVR::GameplayBridgeClient::IsReady() &&
        header.Identity.ServerInstanceNonce != 0 && header.Identity.ConnectionGeneration != 0 &&
        header.Identity.LifecycleEpoch != 0 &&
        header.Identity.ServerInstanceNonce == m_transport.GetServerInstanceNonce() &&
        header.Identity.ConnectionGeneration == m_transport.GetConnectionGeneration() &&
        header.Identity.LifecycleEpoch == SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    if (!sessionCurrent)
        return false;

    const auto clearPartial = [&]() { m_pendingEquipmentSnapshot = {}; };
    const auto markAccepted = [&]() {
        m_hasActionIdByDomain[domainIndex] = true;
        m_lastActionIdByDomain[domainIndex] = header.Identity.ActionId;
    };

    if (action == GameplayBridge::GameplayAction::EquipmentSnapshotBegin)
    {
        if (payload.LocalFormIdD != 0 || payload.ValueA < 0 ||
            payload.ValueA > static_cast<std::int32_t>(kMaximumWornEquipmentEntries) || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0) {
            clearPartial();
            return false;
        }
        m_pendingEquipmentSnapshot = {
            header.Identity.ActionId,
            header.Identity.ServerInstanceNonce,
            header.Identity.ConnectionGeneration,
            header.Identity.LifecycleEpoch,
            {payload.LocalFormIdA, payload.LocalFormIdB, payload.LocalFormIdC},
            static_cast<std::uint16_t>(payload.ValueA),
            {},
        };
        m_pendingEquipmentSnapshot.Items.reserve(static_cast<std::size_t>(payload.ValueA));
        return true;
    }

    auto& pending = m_pendingEquipmentSnapshot;
    if (pending.TransactionId != header.Identity.ActionId ||
        pending.ServerInstanceNonce != header.Identity.ServerInstanceNonce ||
        pending.ConnectionGeneration != header.Identity.ConnectionGeneration ||
        pending.LifecycleEpoch != header.Identity.LifecycleEpoch)
        return false;

    if (action == GameplayBridge::GameplayAction::EquipmentSnapshotItem)
    {
        if (payload.LocalFormIdA == 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA <= 0 || payload.ValueA > kMaximumInventoryDelta ||
            payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            (payload.ActionFlags & ~(GameplayBridge::kEquipmentSnapshotWorn |
                                     GameplayBridge::kEquipmentSnapshotWornLeft |
                                     GameplayBridge::kEquipmentSnapshotWeapon |
                                     GameplayBridge::kEquipmentSnapshotAmmo)) != 0 ||
            (payload.ActionFlags & (GameplayBridge::kEquipmentSnapshotWorn |
                                    GameplayBridge::kEquipmentSnapshotWornLeft)) == 0 ||
            ((payload.ActionFlags & GameplayBridge::kEquipmentSnapshotWeapon) != 0 &&
             (payload.ActionFlags & GameplayBridge::kEquipmentSnapshotAmmo) != 0) ||
            pending.Items.size() >= pending.ExpectedEntries ||
            std::any_of(pending.Items.begin(), pending.Items.end(), [&payload](const EquipmentSnapshotItem& acItem) {
                return acItem.LocalFormId == payload.LocalFormIdA;
            })) {
            clearPartial();
            return false;
        }
        pending.Items.push_back({payload.LocalFormIdA, {}, payload.ValueA,
                                 (payload.ActionFlags & GameplayBridge::kEquipmentSnapshotWorn) != 0,
                                 (payload.ActionFlags & GameplayBridge::kEquipmentSnapshotWornLeft) != 0,
                                 (payload.ActionFlags & GameplayBridge::kEquipmentSnapshotWeapon) != 0,
                                 (payload.ActionFlags & GameplayBridge::kEquipmentSnapshotAmmo) != 0});
        return true;
    }

    if (action != GameplayBridge::GameplayAction::EquipmentSnapshotEnd || payload.LocalFormIdA != 0 ||
        payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
        payload.ValueA != 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
        payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0 ||
        pending.Items.size() != pending.ExpectedEntries) {
        clearPartial();
        return false;
    }

    Inventory complete{};
    complete.Entries.reserve(pending.Items.size());
    for (auto& item : pending.Items) {
        Inventory::Entry entry{};
        entry.Count = item.Count;
        entry.ExtraWorn = item.Worn;
        entry.ExtraWornLeft = item.WornLeft;
        entry.EquipmentFlags = (item.Weapon ? Inventory::Entry::kEquipmentWeapon : 0u) |
                               (item.Ammo ? Inventory::Entry::kEquipmentAmmo : 0u);
        if (!m_world.GetModSystem().GetServerModId(item.LocalFormId, item.ServerFormId) || !item.ServerFormId) {
            clearPartial();
            return false;
        }
        entry.BaseId = item.ServerFormId;
        complete.Entries.push_back(std::move(entry));
    }
    std::sort(complete.Entries.begin(), complete.Entries.end(), [](const Inventory::Entry& acLeft, const Inventory::Entry& acRight) {
        return acLeft.BaseId.ModId != acRight.BaseId.ModId ? acLeft.BaseId.ModId < acRight.BaseId.ModId :
               acLeft.BaseId.BaseId < acRight.BaseId.BaseId;
    });
    if (std::adjacent_find(complete.Entries.begin(), complete.Entries.end(),
            [](const Inventory::Entry& acLeft, const Inventory::Entry& acRight) {
                return acLeft.BaseId == acRight.BaseId;
            }) != complete.Entries.end()) {
        clearPartial();
        return false;
    }

    const auto mapMagic = [this](const std::uint32_t aLocalFormId, GameId& arServerFormId) {
        return aLocalFormId == 0 ||
               (m_world.GetModSystem().GetServerModId(aLocalFormId, arServerFormId) && arServerFormId);
    };
    auto& magicEquipment = complete.CurrentMagicEquipment;
    if (!mapMagic(pending.LocalMagicForms[0], magicEquipment.LeftHandSpell) ||
        !mapMagic(pending.LocalMagicForms[1], magicEquipment.RightHandSpell) ||
        !mapMagic(pending.LocalMagicForms[2], magicEquipment.Shout)) {
        clearPartial();
        return false;
    }

    RequestEquipmentChanges request{};
    request.ServerId = m_localServerId;
    request.TransactionId = pending.TransactionId;
    request.CurrentInventory = complete;
    const auto equipmentPending = std::any_of(
        m_pendingStatefulSends.begin(), m_pendingStatefulSends.end(), [](const PendingStatefulSend& acPending) {
            return acPending.Coalesce && acPending.CoalesceKey == kEquipmentSendCoalesceKey;
        });
    if (!equipmentPending && m_pendingStatefulSends.empty() && m_pendingInventoryDeltas.empty() &&
        m_transport.Send(request)) {
        m_equipmentBaseline = pending.Items;
        m_hasEquipmentBaseline = true;
        m_magicEquipmentBaseline = magicEquipment;
        m_hasMagicEquipmentBaseline = true;
        markAccepted();
    } else {
        QueuePendingStatefulSend(
            [this, request = std::move(request), baseline = pending.Items, magicEquipment, domainIndex,
             actionId = header.Identity.ActionId]() mutable {
                if (!m_transport.Send(request))
                    return false;
                m_equipmentBaseline = std::move(baseline);
                m_hasEquipmentBaseline = true;
                m_magicEquipmentBaseline = magicEquipment;
                m_hasMagicEquipmentBaseline = true;
                MarkActionAccepted(domainIndex, actionId);
                return true;
            },
            true, kEquipmentSendCoalesceKey);
    }
    clearPartial();
    return true;
}
catch (...)
{
    m_pendingEquipmentSnapshot = {};
    spdlog::error("VR equipment snapshot assembly failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
    return false;
}

bool VRLocalGameplayService::ApplyProjectileLaunch(const GameplayBridge::EventRecord& acRecord) noexcept try
{
    const auto& header = acRecord.Header;
    const auto& payload = acRecord.Payload.LocalProjectileLaunch;
    if (!SkyrimTogetherVR::GameplayBridgeClient::IsReady() || header.Identity.ServerInstanceNonce == 0 ||
        header.Identity.ConnectionGeneration == 0 || header.Identity.LifecycleEpoch == 0 ||
        header.Identity.ServerInstanceNonce != m_transport.GetServerInstanceNonce() ||
        header.Identity.ConnectionGeneration != m_transport.GetConnectionGeneration() ||
        header.Identity.LifecycleEpoch != SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() ||
        header.Identity.SequenceId == 0 || header.Identity.SequenceId <= m_lastLocalProjectileSequence ||
        header.Identity.ActionId != 0 || payload.TargetHandle.Value != GameplayBridge::kLocalPlayerHandle.Value ||
        payload.LocalShooterFormId != 0)
        return false;

    ProjectileLaunchRequest request{};
    request.ShooterID = m_localServerId;
    const auto mapRequired = [this](const std::uint32_t aLocalFormId, GameId& arServerFormId) {
        return aLocalFormId != 0 && m_world.GetModSystem().GetServerModId(aLocalFormId, arServerFormId) &&
               static_cast<bool>(arServerFormId);
    };
    const auto mapOptional = [this](const std::uint32_t aLocalFormId, GameId& arServerFormId) {
        return aLocalFormId == 0 ||
               (m_world.GetModSystem().GetServerModId(aLocalFormId, arServerFormId) &&
                static_cast<bool>(arServerFormId));
    };
    if (!mapRequired(payload.LocalProjectileBaseFormId, request.ProjectileBaseID) ||
        !mapRequired(payload.LocalParentCellFormId, request.ParentCellID) ||
        !mapOptional(payload.LocalWeaponFormId, request.WeaponID) ||
        !mapOptional(payload.LocalAmmoFormId, request.AmmoID) ||
        !mapOptional(payload.LocalSpellFormId, request.SpellID))
        return false;

    request.OriginX = payload.OriginX;
    request.OriginY = payload.OriginY;
    request.OriginZ = payload.OriginZ;
    request.XAngle = payload.AngleX;
    request.ZAngle = payload.AngleZ;
    request.YAngle = 0.0F;
    request.CastingSource = payload.CastingSource;
    request.Area = payload.Area;
    request.Power = payload.Power;
    request.Scale = payload.Scale;
    request.AlwaysHit = (payload.LaunchFlags & GameplayBridge::ProjectileAlwaysHit) != 0;
    request.NoDamageOutsideCombat =
        (payload.LaunchFlags & GameplayBridge::ProjectileNoDamageOutsideCombat) != 0;
    request.AutoAim = (payload.LaunchFlags & GameplayBridge::ProjectileAutoAim) != 0;
    request.UnkBool2 = (payload.LaunchFlags & GameplayBridge::ProjectileChainShatter) != 0;
    request.DeferInitialization =
        (payload.LaunchFlags & GameplayBridge::ProjectileDeferInitialization) != 0;
    request.ForceConeOfFire =
        (payload.LaunchFlags & GameplayBridge::ProjectileForceConeOfFire) != 0;
    request.UnkBool1 = false;
    if (m_pendingStatefulSends.empty() && m_pendingInventoryDeltas.empty() && m_transport.Send(request))
    {
        m_lastLocalProjectileSequence = header.Identity.SequenceId;
        return true;
    }
    QueuePendingStatefulSend(
        [this, request = std::move(request), sequenceId = header.Identity.SequenceId]() mutable {
            if (!m_transport.Send(request))
                return false;
            m_lastLocalProjectileSequence = sequenceId;
            return true;
        },
        false, 0);
    return false;
}
catch (...)
{
    spdlog::error("VR projectile staging failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
    return false;
}

void VRLocalGameplayService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    ResetSessionState();
}

void VRLocalGameplayService::OnNotifySyncExperience(const NotifySyncExperience& acMessage) noexcept
{
    if (!std::isfinite(acMessage.Experience) || acMessage.Experience <= 0.0F ||
        acMessage.Experience > GameplayBridge::kMaximumSyncedExperience ||
        !GameplayBridge::IsCombatSkillActorValue(m_lastLocalCombatSkill))
        return;

    auto* avatars = m_world.ctx().find<VRAvatarService>();
    if (!avatars)
        return;
    GameplayBridge::CommandRecord command{};
    if (!avatars->BuildLocalGameplayCommand(GameplayBridge::GameplayDomain::ActorState,
                                             GameplayBridge::GameplayAction::SyncExperience, command))
        return;
    command.Payload.ApplyGameplayAction.LocalFormIdA = m_lastLocalCombatSkill;
    command.Payload.ApplyGameplayAction.ScalarA = acMessage.Experience;
    TP_UNUSED(SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command));
}

void VRLocalGameplayService::MarkActionAccepted(const std::size_t aDomainIndex,
                                                const std::uint64_t aActionId) noexcept
{
    if (aDomainIndex >= m_hasActionIdByDomain.size() || aActionId == 0)
        return;
    m_hasActionIdByDomain[aDomainIndex] = true;
    m_lastActionIdByDomain[aDomainIndex] = std::max(m_lastActionIdByDomain[aDomainIndex], aActionId);
}

void VRLocalGameplayService::QueuePendingStatefulSend(std::function<bool()>&& aTrySend, const bool aCoalesce,
                                                      const std::uint64_t aCoalesceKey) noexcept try
{
    if (aCoalesce)
    {
        const auto existing = std::find_if(m_pendingStatefulSends.rbegin(), m_pendingStatefulSends.rend(),
                                           [aCoalesceKey](const PendingStatefulSend& acPending) {
                                               return acPending.Coalesce && acPending.CoalesceKey == aCoalesceKey;
                                           });
        if (existing != m_pendingStatefulSends.rend())
        {
            m_pendingStatefulSends.erase(std::prev(existing.base()));
            m_pendingStatefulSends.push_back(
                {std::move(aTrySend), aCoalesceKey, true, ++m_nextPendingSendOrder});
            return;
        }
    }
    if (m_pendingStatefulSends.size() >= kMaximumPendingStatefulSends)
    {
        spdlog::error("VR local gameplay retry queue exhausted; rebasing the native capture epoch");
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
        return;
    }

    if (m_pendingStatefulSends.empty() && m_pendingInventoryDeltas.empty())
    {
        m_pendingSendServerInstanceNonce = m_transport.GetServerInstanceNonce();
        m_pendingSendConnectionGeneration = m_transport.GetConnectionGeneration();
        m_pendingSendLifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    }
    m_pendingStatefulSends.push_back(
        {std::move(aTrySend), aCoalesceKey, aCoalesce, ++m_nextPendingSendOrder});
}
catch (...)
{
    spdlog::error("VR local gameplay retry queue allocation failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

void VRLocalGameplayService::QueuePendingInventoryDelta(const GameId aBaseId, const std::int32_t aCount,
                                                        const bool aIsQuestItem, const bool aDrop,
                                                        const std::size_t aDomainIndex,
                                                        const std::uint64_t aActionId) noexcept
{
    Inventory::Entry item{};
    item.BaseId = aBaseId;
    item.Count = aCount;
    item.IsQuestItem = aIsQuestItem;
    QueuePendingInventoryChange(m_localServerId, std::move(item), aDrop, aDomainIndex, aActionId);
}

void VRLocalGameplayService::QueuePendingInventoryChange(
    const std::uint32_t aServerId, Inventory::Entry aItem, const bool aDrop,
    const std::size_t aDomainIndex, const std::uint64_t aActionId) noexcept try
{
    if (aServerId == 0 || !aItem.IsValidMutation())
        return;
    if (m_pendingInventoryDeltas.empty() && m_pendingStatefulSends.empty()) {
        RequestInventoryChanges request{};
        request.ServerId = aServerId;
        request.Item = aItem;
        request.Drop = aDrop;
        request.UpdateClients = true;
        if (m_transport.Send(request)) {
            MarkActionAccepted(aDomainIndex, aActionId);
            return;
        }
    }
    if (!m_pendingInventoryDeltas.empty())
    {
        auto& previous = m_pendingInventoryDeltas.back();
        const auto combined = static_cast<std::int64_t>(previous.Item.Count) + aItem.Count;
        if (previous.Order == m_nextPendingSendOrder && previous.ServerId == aServerId &&
            previous.Item.CanBeMerged(aItem) && previous.Drop == aDrop &&
            combined >= -Inventory::Entry::kMaximumMutationCount &&
            combined <= Inventory::Entry::kMaximumMutationCount)
        {
            previous.Item.Count = static_cast<std::int32_t>(combined);
            previous.ActionId = aActionId;
            return;
        }
    }
    if (m_pendingInventoryDeltas.size() >= kMaximumPendingInventoryDeltas)
    {
        spdlog::error("VR local inventory retry queue exhausted; rebasing the native capture epoch");
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
        return;
    }

    if (m_pendingStatefulSends.empty() && m_pendingInventoryDeltas.empty())
    {
        m_pendingSendServerInstanceNonce = m_transport.GetServerInstanceNonce();
        m_pendingSendConnectionGeneration = m_transport.GetConnectionGeneration();
        m_pendingSendLifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    }
    m_pendingInventoryDeltas.push_back(
        {aServerId, std::move(aItem), aDomainIndex, aActionId, false, aDrop, ++m_nextPendingSendOrder});
}
catch (...)
{
    spdlog::error("VR inventory retry queue allocation failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

void VRLocalGameplayService::TrySendPendingOutbound() noexcept try
{
    while (!m_pendingStatefulSends.empty() || !m_pendingInventoryDeltas.empty())
    {
        const auto sendInventory = m_pendingStatefulSends.empty() ||
            (!m_pendingInventoryDeltas.empty() &&
             m_pendingInventoryDeltas.front().Order < m_pendingStatefulSends.front().Order);
        if (!sendInventory)
        {
            if (!m_pendingStatefulSends.front().TrySend())
                return;
            m_pendingStatefulSends.pop_front();
            continue;
        }

        const auto& pending = m_pendingInventoryDeltas.front();
        RequestInventoryChanges request{};
        request.ServerId = pending.ServerId;
        request.Item = pending.Item;
        request.Drop = pending.Drop;
        request.UpdateClients = true;
        if (!m_transport.Send(request))
            return;
        MarkActionAccepted(pending.DomainIndex, pending.ActionId);
        m_pendingInventoryDeltas.pop_front();
    }
}
catch (...)
{
    spdlog::error("VR local gameplay retry processing failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

void VRLocalGameplayService::TryArmLocalCapture() noexcept
{
    if (!m_localCaptureArmPending || m_localCaptureArmed || m_localServerId == 0 ||
        !m_transport.IsOnline() || !SkyrimTogetherVR::GameplayBridgeClient::IsReady())
        return;

    const auto serverInstanceNonce = m_transport.GetServerInstanceNonce();
    const auto connectionGeneration = m_transport.GetConnectionGeneration();
    const auto lifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    SkyrimTogetherVR::CanonicalEntity::BridgeIdentity entityIdentity{};
    if (serverInstanceNonce == 0 || connectionGeneration == 0 || lifecycleEpoch == 0 ||
        !SkyrimTogetherVR::CanonicalEntity::TrySplitServerId(m_localServerId, entityIdentity))
        return;

    GameplayBridge::CommandRecord command{};
    command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyGameplayAction);
    command.Header.PayloadSize = GameplayBridge::kFixedPayloadBytes;
    command.Header.Identity.ServerInstanceNonce = serverInstanceNonce;
    command.Header.Identity.ConnectionGeneration = connectionGeneration;
    command.Header.Identity.LifecycleEpoch = lifecycleEpoch;
    command.Header.Identity.EntityId = entityIdentity.EntityId;
    command.Header.Identity.EntityGeneration = entityIdentity.EntityGeneration;
    command.Payload.ApplyGameplayAction.TargetHandle = GameplayBridge::kLocalPlayerHandle;
    command.Payload.ApplyGameplayAction.Domain = static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::ActorState);
    command.Payload.ApplyGameplayAction.Action = static_cast<std::uint16_t>(GameplayBridge::GameplayAction::ArmLocalCapture);
    if (SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command))
    {
        m_pendingSendServerInstanceNonce = serverInstanceNonce;
        m_pendingSendConnectionGeneration = connectionGeneration;
        m_pendingSendLifecycleEpoch = lifecycleEpoch;
        m_localCaptureArmPending = false;
        m_localCaptureArmed = true;
    }
}

void VRLocalGameplayService::OnUpdate(const UpdateEvent& acEvent) noexcept try
{
    if (m_localServerId != 0 && m_pendingSendServerInstanceNonce != 0 &&
        (!SkyrimTogetherVR::GameplayBridgeClient::IsReady() ||
         m_pendingSendServerInstanceNonce != m_transport.GetServerInstanceNonce() ||
         m_pendingSendConnectionGeneration != m_transport.GetConnectionGeneration() ||
         m_pendingSendLifecycleEpoch != SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch()))
    {
        const auto localServerId = m_localServerId;
        ResetSessionState();
        m_localServerId = localServerId;
        m_pendingSendServerInstanceNonce = m_transport.GetServerInstanceNonce();
        m_pendingSendConnectionGeneration = m_transport.GetConnectionGeneration();
        m_pendingSendLifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
        m_localCaptureArmPending = true;
    }

    TryArmLocalCapture();

    if (std::isfinite(acEvent.Delta) && acEvent.Delta > 0.0) {
        for (auto snapshot = m_pendingObjectSnapshots.begin(); snapshot != m_pendingObjectSnapshots.end();) {
            snapshot->second.Elapsed += acEvent.Delta;
            if (snapshot->second.Elapsed >= kObjectSnapshotLifetime)
                snapshot = m_pendingObjectSnapshots.erase(snapshot);
            else
                ++snapshot;
        }
        for (auto transaction = m_pendingInventoryTransactions.begin();
             transaction != m_pendingInventoryTransactions.end();) {
            transaction->second.Elapsed += acEvent.Delta;
            if (transaction->second.Elapsed >= kObjectSnapshotLifetime)
                transaction = m_pendingInventoryTransactions.erase(transaction);
            else
                ++transaction;
        }
    }

    if (!m_pendingStatefulSends.empty() || !m_pendingInventoryDeltas.empty())
    {
        const auto sessionCurrent = SkyrimTogetherVR::GameplayBridgeClient::IsReady() &&
            m_pendingSendServerInstanceNonce == m_transport.GetServerInstanceNonce() &&
            m_pendingSendConnectionGeneration == m_transport.GetConnectionGeneration() &&
            m_pendingSendLifecycleEpoch == SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
        if (!sessionCurrent)
        {
            m_pendingStatefulSends.clear();
            m_pendingInventoryDeltas.clear();
            m_nextPendingSendOrder = 0;
        }
        else if (m_transport.IsOnline() && m_localServerId != 0)
        {
            TrySendPendingOutbound();
        }
    }

    TrySendPendingMount();

    if (m_pendingPackageId)
    {
        m_packageSendElapsed += acEvent.Delta;
        if (m_packageSendElapsed >= 0.25)
        {
            m_packageSendElapsed = 0.0;
            TrySendPendingPackage();
        }
    }

    m_healthSendElapsed += acEvent.Delta;
    if (m_healthSendElapsed >= 0.25)
    {
        m_healthSendElapsed = 0.0;
        FlushPendingHealthDelta();
    }

    m_experienceSendElapsed += acEvent.Delta;
    if (m_experienceSendElapsed >= 1.0)
    {
        m_experienceSendElapsed = 0.0;
        auto* party = m_world.ctx().find<PartyService>();
        if (m_cachedExperience > 0.0F && party && party->IsInParty())
        {
            SyncExperienceRequest request{};
            request.Experience = std::min(m_cachedExperience, GameplayBridge::kMaximumSyncedExperience);
            if (m_pendingStatefulSends.empty() && m_pendingInventoryDeltas.empty() && m_transport.Send(request))
                m_cachedExperience -= request.Experience;
        }
    }

    if (m_pendingInventoryDeltaSuppression.Remaining > 0.0)
    {
        m_pendingInventoryDeltaSuppression.Remaining -= acEvent.Delta;
        if (m_pendingInventoryDeltaSuppression.Remaining <= 0.0)
            CancelGoldInventoryDeltaSuppression();
    }
}
catch (...)
{
    spdlog::error("VR local gameplay update failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

void VRLocalGameplayService::FlushPendingHealthDelta() noexcept
{
    if (!m_transport.IsOnline() || m_localServerId == 0 || m_pendingHealthDelta == 0.0F)
        return;
    RequestHealthChangeBroadcast request{};
    request.Id = m_localServerId;
    request.DeltaHealth = m_pendingHealthDelta;
    if (m_pendingStatefulSends.empty() && m_pendingInventoryDeltas.empty() && m_transport.Send(request))
        m_pendingHealthDelta = 0.0F;
}

void VRLocalGameplayService::TrySendPendingMount() noexcept
{
    if (m_pendingMountLocalReference == 0 || !m_transport.IsOnline() || m_localServerId == 0)
        return;
    auto* ownership = m_world.ctx().find<VRNpcOwnershipService>();
    if (!ownership)
        return;
    TP_UNUSED(ownership->RequestOwnershipForLocalReference(m_pendingMountLocalReference));
    const auto mountServerId = ownership ? ownership->GetServerIdForLocalReference(m_pendingMountLocalReference) : 0;
    if (mountServerId == 0)
        return;
    MountRequest request{};
    request.RiderId = m_localServerId;
    request.MountId = mountServerId;
    if (m_pendingStatefulSends.empty() && m_pendingInventoryDeltas.empty() && m_transport.Send(request))
    {
        MarkActionAccepted(m_pendingMountDomainIndex, m_pendingMountActionId);
        m_pendingMountLocalReference = 0;
        m_pendingMountDomainIndex = 0;
        m_pendingMountActionId = 0;
    }
}

void VRLocalGameplayService::TrySendPendingPackage() noexcept
{
    if (!m_pendingPackageId || !m_transport.IsOnline() || m_localServerId == 0)
        return;
    NewPackageRequest request{};
    request.ActorId = m_localServerId;
    request.PackageId = m_pendingPackageId;
    if (m_pendingStatefulSends.empty() && m_pendingInventoryDeltas.empty() && m_transport.Send(request))
    {
        m_lastSentPackageId = m_pendingPackageId;
        m_pendingPackageId = {};
        MarkActionAccepted(m_pendingPackageDomainIndex, m_pendingPackageActionId);
        m_pendingPackageDomainIndex = 0;
        m_pendingPackageActionId = 0;
        m_packageSendElapsed = 0.0;
    }
}

bool VRLocalGameplayService::AcceptAction(const GameplayBridge::EventRecord& acRecord) noexcept
{
    const auto& header = acRecord.Header;
    const auto& payload = acRecord.Payload.LocalGameplayAction;
    const auto domain = static_cast<GameplayBridge::GameplayDomain>(payload.Domain);
    const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
    const auto domainIndex = static_cast<std::size_t>(payload.Domain);

    if (!IsCurrentBridgeRecord(header) ||
        header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayAction) ||
        header.PayloadSize != GameplayBridge::kFixedPayloadBytes || header.Flags != 0 || header.Identity.EntityId != 0 ||
        header.Identity.EntityGeneration != 0 || header.Identity.SequenceId != 0 || header.Identity.ActionId == 0 ||
        payload.SecondaryHandle.Value != 0 ||
        domainIndex == 0 || domainIndex >= m_lastActionIdByDomain.size() ||
        !GameplayBridge::IsActionInDomain(domain, action) || !std::isfinite(payload.ScalarA) ||
        !std::isfinite(payload.ScalarB) || !std::isfinite(payload.ScalarC) || !std::isfinite(payload.ScalarD) ||
        payload.Reserved0 != 0 || !IsZero(payload.ReservedTail, sizeof(payload.ReservedTail)))
        return false;
    if (IsUnsupportedLocalGameplayAction(domain, action))
        return false;

    const bool objectSnapshot = domain == GameplayBridge::GameplayDomain::Object &&
        GameplayBridge::IsObjectSnapshotAction(action);
    const bool inventoryTransaction = domain == GameplayBridge::GameplayDomain::Inventory &&
        GameplayBridge::IsInventoryTransactionAction(action);
    if ((!objectSnapshot && !inventoryTransaction &&
         payload.TargetHandle.Value != GameplayBridge::kLocalPlayerHandle.Value) ||
        (inventoryTransaction &&
         (payload.TargetHandle.Value != GameplayBridge::kLocalPlayerHandle.Value || payload.TargetLocalFormId == 0)) ||
        (objectSnapshot && (payload.TargetHandle.Value != 0 || payload.TargetLocalFormId == 0)))
        return false;

    if (objectSnapshot && action != GameplayBridge::GameplayAction::ObjectSnapshotBegin) {
        const auto pending = m_pendingObjectSnapshots.find(payload.TargetLocalFormId);
        if (pending != m_pendingObjectSnapshots.end() &&
            pending->second.ActionId == acRecord.Header.Identity.ActionId)
            return true;
    }
    if (inventoryTransaction && action != GameplayBridge::GameplayAction::InventoryTransactionBegin) {
        const auto pending = m_pendingInventoryTransactions.find(payload.TargetLocalFormId);
        if (pending != m_pendingInventoryTransactions.end() &&
            pending->second.ActionId == acRecord.Header.Identity.ActionId &&
            pending->second.ServerInstanceNonce == acRecord.Header.Identity.ServerInstanceNonce &&
            pending->second.ConnectionGeneration == acRecord.Header.Identity.ConnectionGeneration &&
            pending->second.LifecycleEpoch == acRecord.Header.Identity.LifecycleEpoch)
            return true;
    }

    return !m_hasActionIdByDomain[domainIndex] ||
           acRecord.Header.Identity.ActionId > m_lastActionIdByDomain[domainIndex];
}

bool VRLocalGameplayService::HasMappedLocalPlayerForm(const GameplayBridge::GameplayActionPayload& acPayload) const noexcept
{
    if (acPayload.TargetLocalFormId == 0)
        return false;

    GameId gameId{};
    return m_world.GetModSystem().GetServerModId(acPayload.TargetLocalFormId, gameId) && gameId;
}

std::uint32_t VRLocalGameplayService::GetServerIdForLocalActor(const std::uint32_t aLocalFormId) const noexcept
{
    if (aLocalFormId == 0)
        return 0;
    constexpr std::uint32_t kPlayerReferenceFormId = 0x14;
    if (aLocalFormId == kPlayerReferenceFormId)
        return m_localServerId;
    if (const auto* avatars = m_world.ctx().find<VRAvatarService>()) {
        if (const auto serverId = avatars->GetRemoteServerIdForLocalReference(aLocalFormId); serverId != 0)
            return serverId;
    }
    if (const auto* ownership = m_world.ctx().find<VRNpcOwnershipService>())
        return ownership->GetServerIdForLocalReference(aLocalFormId);
    return 0;
}

std::uint32_t VRLocalGameplayService::GetServerIdForLocalInventoryOwner(
    const std::uint32_t aLocalFormId) const noexcept
{
    if (aLocalFormId == 0)
        return 0;
    constexpr std::uint32_t kPlayerReferenceFormId = 0x14;
    if (aLocalFormId == kPlayerReferenceFormId)
        return m_localServerId;
    if (const auto* ownership = m_world.ctx().find<VRNpcOwnershipService>()) {
        if (const auto serverId = ownership->GetServerIdForLocalReference(aLocalFormId); serverId != 0)
            return serverId;
    }
    const auto objects = m_world.view<FormIdComponent, ObjectComponent>();
    std::uint32_t serverId{};
    for (const auto entity : objects) {
        if (objects.get<FormIdComponent>(entity).Id != aLocalFormId)
            continue;
        const auto candidate = objects.get<ObjectComponent>(entity).Id;
        if (candidate == 0 || serverId != 0)
            return 0;
        serverId = candidate;
    }
    return serverId;
}

bool VRLocalGameplayService::ConsumeInventoryDeltaSuppression(
    const GameplayBridge::GameplayActionPayload& acPayload) noexcept
{
    const auto& pending = m_pendingInventoryDeltaSuppression;
    if (pending.Remaining <= 0.0 || acPayload.LocalFormIdA != pending.LocalFormId || acPayload.ValueA != pending.Count)
        return false;

    CancelGoldInventoryDeltaSuppression();
    return true;
}

void VRLocalGameplayService::ResetSessionState() noexcept
{
    m_lastActionIdByDomain.fill(0);
    m_hasActionIdByDomain.fill(false);
    m_appearance = {};
    m_nameAssembly = {};
    m_appearanceDirty = false;
    m_pendingObjectSnapshots.clear();
    m_pendingInventoryTransactions.clear();
    m_pendingEquipmentSnapshot = {};
    m_equipmentBaseline = {};
    m_hasEquipmentBaseline = false;
    m_magicEquipmentBaseline = {};
    m_hasMagicEquipmentBaseline = false;
    m_equipmentSessionServerInstanceNonce = 0;
    m_equipmentSessionConnectionGeneration = 0;
    m_equipmentSessionLifecycleEpoch = 0;
    m_pendingStatefulSends.clear();
    m_pendingInventoryDeltas.clear();
    m_nextPendingSendOrder = 0;
    m_pendingSendServerInstanceNonce = 0;
    m_pendingSendConnectionGeneration = 0;
    m_pendingSendLifecycleEpoch = 0;
    m_localCaptureArmPending = false;
    m_localCaptureArmed = false;
    CancelGoldInventoryDeltaSuppression();
    m_localServerId = 0;
    m_lastPublishedPlayerLevel = 0;
    m_pendingMountLocalReference = 0;
    m_pendingMountDomainIndex = 0;
    m_pendingMountActionId = 0;
    m_pendingPackageId = {};
    m_pendingPackageDomainIndex = 0;
    m_pendingPackageActionId = 0;
    m_lastSentPackageId = {};
    m_packageSendElapsed = 0.0;
    m_lastLocalCombatSkill = 0;
    m_cachedExperience = 0.0F;
    m_experienceSendElapsed = 0.0;
    m_pendingHealthDelta = 0.0F;
    m_healthSendElapsed = 0.0;
    m_lastLocalProjectileSequence = 0;
    m_vrGrabSequence = 0;
}
