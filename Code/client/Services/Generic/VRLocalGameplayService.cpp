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
constexpr std::size_t kMaximumPendingObjectSnapshots = 512;
constexpr std::size_t kMaximumObjectSnapshotEntries = 512;
constexpr std::size_t kMaximumWornEquipmentEntries = 64;
constexpr double kInventoryDeltaSuppressionLifetime = 5.0;
constexpr std::size_t kMaximumPendingStatefulSends = 256;
constexpr std::size_t kMaximumPendingInventoryDeltas = 256;
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
                                          const std::uint64_t aCoalesceKey) noexcept
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

void VRLocalGameplayService::OnLocalGameplayBridgeEvent(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept
{
    const auto& record = acEvent.Record;
    if (!m_transport.IsOnline() || m_localServerId == 0)
        return;

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
        action >= GameplayBridge::GameplayAction::ObjectSnapshotBegin &&
        action <= GameplayBridge::GameplayAction::ObjectSnapshotEnd;
    if (objectSnapshot)
    {
        ApplyObjectSnapshot(record);
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
            payload.ScalarD != 0.0F || (payload.ActionFlags & ~0x7u) != 0)
            return;

        AddTargetRequest request{};
        request.TargetId = GetServerIdForLocalActor(payload.TargetLocalFormId);
        request.CasterId = payload.LocalFormIdC != 0 ? GetServerIdForLocalActor(payload.LocalFormIdC) : 0;
        if (request.TargetId == 0 || (payload.LocalFormIdC != 0 && request.CasterId == 0) ||
            ((payload.ActionFlags & 0x4u) != 0 && request.TargetId != m_localServerId &&
             !m_world.GetServerSettings().PvpEnabled) ||
            !m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.SpellId) || !request.SpellId ||
            !m_world.GetModSystem().GetServerModId(payload.LocalFormIdB, request.EffectId) || !request.EffectId)
            return;
        request.Magnitude = payload.ScalarA;
        request.IsDualCasting = (payload.ActionFlags & 0x2u) != 0;
        request.ApplyHealPerkBonus = false;
        request.ApplyStaminaPerkBonus = false;
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
        if (domain != GameplayBridge::GameplayDomain::ActorState || payload.LocalFormIdA == 0 ||
            !std::isfinite(payload.ScalarA) || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
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
                return PublishAppearance(payload.Domain, acRecord.Header.Identity.ActionId);
            }
        }
        if (count >= VRAppearance::kMaximumHeadParts)
            return false;
        m_appearance.HeadParts[count] = {slot, gameId};
        ++m_appearance.HeadPartCount;
        break;
    }
    case GameplayBridge::GameplayAction::SetTint:
    {
        if (payload.ValueA != static_cast<std::int32_t>(GameplayBridge::kSupportedSkinTintType) || payload.LocalFormIdA != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueB != 0 || !std::isfinite(payload.ScalarA) ||
            payload.ScalarA < 0.0F || payload.ScalarA > 1.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return false;
        const auto type = static_cast<std::uint8_t>(payload.ValueA);
        auto count = m_appearance.TintCount;
        for (std::uint8_t index = 0; index < count; ++index)
        {
            if (m_appearance.Tints[index].Type == type)
            {
                m_appearance.Tints[index] = {type, payload.LocalFormIdB, payload.ScalarA};
                return PublishAppearance(payload.Domain, acRecord.Header.Identity.ActionId);
            }
        }
        if (count >= VRAppearance::kMaximumTints)
            return false;
        m_appearance.Tints[count] = {type, payload.LocalFormIdB, payload.ScalarA};
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
    default:
        return false;
    }

    return PublishAppearance(payload.Domain, acRecord.Header.Identity.ActionId);
}

bool VRLocalGameplayService::AcceptAppearanceText(const GameplayBridge::EventRecord& acRecord) const noexcept
{
    const auto& header = acRecord.Header;
    const auto& payload = acRecord.Payload.LocalGameplayTextChunk;
    return header.Kind == static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayTextChunk) &&
           header.PayloadSize == GameplayBridge::kFixedPayloadBytes && header.Flags == 0 &&
           header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 && header.Identity.SequenceId == 0 &&
           header.Identity.ActionId != 0 && payload.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value &&
           payload.TargetLocalFormId != 0 && payload.Domain == static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Appearance) &&
           payload.Action == static_cast<std::uint16_t>(GameplayBridge::GameplayAction::SetName) && payload.TextId != 0 &&
           payload.ChunkCount != 0 && payload.ChunkCount <= 3 && payload.ChunkIndex < payload.ChunkCount &&
           payload.ByteCount <= GameplayBridge::kGameplayTextBytesPerChunk && payload.Reserved0 == 0 &&
           payload.AuxiliaryLocalFormId == 0;
}

bool VRLocalGameplayService::ApplyAppearanceText(const GameplayBridge::EventRecord& acRecord) noexcept
{
    if (!AcceptAppearanceText(acRecord))
        return false;
    const auto& payload = acRecord.Payload.LocalGameplayTextChunk;
    if (m_hasActionIdByDomain[static_cast<std::size_t>(GameplayBridge::GameplayDomain::Appearance)] &&
        acRecord.Header.Identity.ActionId <= m_lastActionIdByDomain[static_cast<std::size_t>(GameplayBridge::GameplayDomain::Appearance)])
        return false;

    if (m_nameAssembly.TextId != payload.TextId || m_nameAssembly.ChunkCount != payload.ChunkCount)
        m_nameAssembly = {payload.TextId, payload.ChunkCount};
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
    if (nameLength > VRAppearance::kMaximumNameBytes)
        return false;
    m_appearance.Name = {};
    std::memcpy(m_appearance.Name.data(), m_nameAssembly.Bytes.data(), nameLength);
    m_appearance.NameLength = static_cast<std::uint8_t>(nameLength);
    return PublishAppearance(static_cast<std::size_t>(GameplayBridge::GameplayDomain::Appearance),
                             acRecord.Header.Identity.ActionId);
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

bool VRLocalGameplayService::ApplyObjectSnapshot(const GameplayBridge::EventRecord& acRecord) noexcept
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
            payload.LocalFormIdA == 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA < 0 || payload.ValueA > 2 || payload.ValueB < -1 || payload.ValueB > 255 ||
            payload.ScalarA < -kMaximumNetworkPosition || payload.ScalarA > kMaximumNetworkPosition ||
            payload.ScalarB < -kMaximumNetworkPosition || payload.ScalarB > kMaximumNetworkPosition ||
            payload.ScalarC < -kMaximumNetworkPosition || payload.ScalarC > kMaximumNetworkPosition ||
            payload.ScalarD != 0.0F ||
            (payload.ActionFlags & ~(GameplayBridge::kObjectSnapshotContainer |
                                     GameplayBridge::kObjectSnapshotPlayerHome)) != 0)
            return false;

        auto existing = m_pendingObjectSnapshots.find(payload.TargetLocalFormId);
        if (existing == m_pendingObjectSnapshots.end() &&
            m_pendingObjectSnapshots.size() >= kMaximumPendingObjectSnapshots)
            return false;

        PendingObjectSnapshot pending{};
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

    if (action == GameplayBridge::GameplayAction::ObjectSnapshotItem)
    {
        if (payload.TargetHandle.Value != 0 || payload.SecondaryHandle.Value != 0 || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA <= 0 || payload.ValueA > kMaximumInventoryDelta || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || (payload.ActionFlags & ~GameplayBridge::kInventoryQuestItem) != 0)
            return false;

        if (!pending->second.Ignore)
        {
            if (pending->second.Data.CurrentInventory.Entries.size() >= kMaximumObjectSnapshotEntries)
                return false;
            Inventory::Entry entry{};
            entry.Count = payload.ValueA;
            entry.IsQuestItem = (payload.ActionFlags & GameplayBridge::kInventoryQuestItem) != 0;
            if (!m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, entry.BaseId) || !entry.BaseId)
                return false;
            pending->second.Data.CurrentInventory.Entries.push_back(std::move(entry));
        }
        return true;
    }

    if (action != GameplayBridge::GameplayAction::ObjectSnapshotEnd || payload.TargetHandle.Value != 0 ||
        payload.SecondaryHandle.Value != 0 || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
        payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
        payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
        payload.ScalarD != 0.0F || payload.ActionFlags != 0)
        return false;

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

bool VRLocalGameplayService::ApplyEquipmentSnapshot(const GameplayBridge::EventRecord& acRecord) noexcept
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

bool VRLocalGameplayService::ApplyProjectileLaunch(const GameplayBridge::EventRecord& acRecord) noexcept
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
                                                      const std::uint64_t aCoalesceKey) noexcept
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

void VRLocalGameplayService::QueuePendingInventoryDelta(const GameId aBaseId, const std::int32_t aCount,
                                                        const bool aIsQuestItem, const bool aDrop,
                                                        const std::size_t aDomainIndex,
                                                        const std::uint64_t aActionId) noexcept
{
    if (m_pendingInventoryDeltas.empty() && m_pendingStatefulSends.empty())
    {
        RequestInventoryChanges request{};
        request.ServerId = m_localServerId;
        request.Item.BaseId = aBaseId;
        request.Item.Count = aCount;
        request.Item.IsQuestItem = aIsQuestItem;
        request.Drop = aDrop;
        request.UpdateClients = true;
        if (m_transport.Send(request))
        {
            MarkActionAccepted(aDomainIndex, aActionId);
            return;
        }
    }

    if (!m_pendingInventoryDeltas.empty())
    {
        auto& previous = m_pendingInventoryDeltas.back();
        const auto combined = static_cast<std::int64_t>(previous.Count) + aCount;
        if (previous.Order == m_nextPendingSendOrder && previous.BaseId == aBaseId &&
            previous.IsQuestItem == aIsQuestItem && previous.Drop == aDrop &&
            combined >= -kMaximumInventoryDelta && combined <= kMaximumInventoryDelta)
        {
            previous.Count = static_cast<std::int32_t>(combined);
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
        {aBaseId, aCount, aDomainIndex, aActionId, aIsQuestItem, aDrop, ++m_nextPendingSendOrder});
}

void VRLocalGameplayService::TrySendPendingOutbound() noexcept
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
        request.ServerId = m_localServerId;
        request.Item.BaseId = pending.BaseId;
        request.Item.Count = pending.Count;
        request.Item.IsQuestItem = pending.IsQuestItem;
        request.Drop = pending.Drop;
        request.UpdateClients = true;
        if (!m_transport.Send(request))
            return;
        MarkActionAccepted(pending.DomainIndex, pending.ActionId);
        m_pendingInventoryDeltas.pop_front();
    }
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

void VRLocalGameplayService::OnUpdate(const UpdateEvent& acEvent) noexcept
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

    if (header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayAction) ||
        header.PayloadSize != GameplayBridge::kFixedPayloadBytes || header.Flags != 0 || header.Identity.EntityId != 0 ||
        header.Identity.EntityGeneration != 0 || header.Identity.SequenceId != 0 || header.Identity.ActionId == 0 ||
        payload.SecondaryHandle.Value != 0 ||
        domainIndex == 0 || domainIndex >= m_lastActionIdByDomain.size() ||
        !GameplayBridge::IsActionInDomain(domain, action) || !std::isfinite(payload.ScalarA) ||
        !std::isfinite(payload.ScalarB) || !std::isfinite(payload.ScalarC) || !std::isfinite(payload.ScalarD) ||
        payload.Reserved0 != 0 || !IsZero(payload.ReservedTail, sizeof(payload.ReservedTail)))
        return false;

    const bool objectSnapshot = domain == GameplayBridge::GameplayDomain::Object &&
        action >= GameplayBridge::GameplayAction::ObjectSnapshotBegin &&
        action <= GameplayBridge::GameplayAction::ObjectSnapshotEnd;
    if ((!objectSnapshot && payload.TargetHandle.Value != GameplayBridge::kLocalPlayerHandle.Value) ||
        (objectSnapshot && (payload.TargetHandle.Value != 0 || payload.TargetLocalFormId == 0)))
        return false;

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
    m_pendingObjectSnapshots.clear();
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
}
