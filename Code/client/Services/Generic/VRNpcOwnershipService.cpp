#include <TiltedOnlinePCH.h>

#include <Services/VRNpcOwnershipService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/LocalGameplayBridgeEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/NotifyOwnershipTransfer.h>
#include <Messages/NotifyRelinquishControl.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestDeathStateChange.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/RequestOwnershipClaim.h>
#include <Messages/RequestOwnershipTransfer.h>
#include <Services/TransportService.h>
#include <Services/VRAvatarService.h>
#include <Structs/GameplayCapabilities.h>
#include <VRGameplayBridge.h>
#include <World.h>

#include <algorithm>
#include <bit>
#include <cmath>

namespace GameplayBridge = SkyrimTogetherVR::GameplayBridge;

namespace
{
constexpr std::array<std::uint32_t, 3> kActorValues{24u, 25u, 26u};
constexpr std::size_t kMaximumNpcItems = 64;
constexpr std::size_t kMaximumNpcFactions = 64;
constexpr std::size_t kMaximumTrackedNpcs = 64;
constexpr float kMaximumNetworkPosition = 10000000.0F;
constexpr double kPartialSnapshotLifetime = 2.0;
constexpr double kPendingAssignmentLifetime = 15.0;
constexpr double kPendingClaimLifetime = 15.0;

[[nodiscard]] bool IsFinite(const float aValue) noexcept
{
    return std::isfinite(aValue);
}

[[nodiscard]] bool IsFinite(const glm::vec3& acValue) noexcept
{
    return IsFinite(acValue.x) && IsFinite(acValue.y) && IsFinite(acValue.z);
}

[[nodiscard]] bool IsValidPosition(const glm::vec3& acValue) noexcept
{
    return IsFinite(acValue) && std::abs(acValue.x) <= kMaximumNetworkPosition &&
           std::abs(acValue.y) <= kMaximumNetworkPosition && std::abs(acValue.z) <= kMaximumNetworkPosition;
}

[[nodiscard]] std::size_t ActorValueIndex(const std::uint32_t aValue) noexcept
{
    const auto it = std::find(kActorValues.begin(), kActorValues.end(), aValue);
    return it == kActorValues.end() ? kActorValues.size() : static_cast<std::size_t>(it - kActorValues.begin());
}

[[nodiscard]] bool IsZero(const void* apData, const std::size_t aSize) noexcept
{
    const auto* bytes = static_cast<const std::uint8_t*>(apData);
    for (std::size_t index = 0; index < aSize; ++index) {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

} // namespace

VRNpcOwnershipService::VRNpcOwnershipService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRNpcOwnershipService::OnUpdate>(this);
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&VRNpcOwnershipService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRNpcOwnershipService::OnDisconnected>(this);
    m_localGameplayConnection = aDispatcher.sink<SkyrimTogetherVR::LocalGameplayBridgeEvent>().connect<&VRNpcOwnershipService::OnLocalGameplay>(this);
    m_assignCharacterConnection = aDispatcher.sink<AssignCharacterResponse>().connect<&VRNpcOwnershipService::OnAssignCharacter>(this);
    m_characterSpawnConnection = aDispatcher.sink<CharacterSpawnRequest>().connect<&VRNpcOwnershipService::OnCharacterSpawn>(this);
    m_ownershipTransferConnection = aDispatcher.sink<NotifyOwnershipTransfer>().connect<&VRNpcOwnershipService::OnOwnershipTransfer>(this);
    m_removeCharacterConnection = aDispatcher.sink<NotifyRemoveCharacter>().connect<&VRNpcOwnershipService::OnRemoveCharacter>(this);
    m_relinquishConnection = aDispatcher.sink<NotifyRelinquishControl>().connect<&VRNpcOwnershipService::OnRelinquishControl>(this);
}

std::uint32_t VRNpcOwnershipService::GetServerIdForLocalReference(
    const std::uint32_t aReferenceFormId) const noexcept
{
    const auto it = m_referenceToServer.find(aReferenceFormId);
    return IsSessionCurrent() && it != m_referenceToServer.end() ? it->second : 0;
}

bool VRNpcOwnershipService::RequestOwnershipForLocalReference(const std::uint32_t aReferenceFormId) noexcept
{
    if (!IsSessionCurrent() || aReferenceFormId == 0)
        return false;

    const auto server = m_referenceToServer.find(aReferenceFormId);
    if (server == m_referenceToServer.end() || server->second == 0)
        return StartObservation(aReferenceFormId);
    if (const auto owned = m_ownedByReference.find(aReferenceFormId);
        owned != m_ownedByReference.end() && owned->second.ServerId == server->second)
        return true;
    if (const auto claim = m_claimAfterSnapshot.find(aReferenceFormId); claim != m_claimAfterSnapshot.end() &&
        claim->second.ServerId == server->second && claim->second.GrantToken != 0)
        return true;
    // NPC claims are only valid after a server-issued transfer grant.
    return StartObservation(aReferenceFormId);
}

void VRNpcOwnershipService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    const auto bridgeReady = IsBridgeReady();
    if (!bridgeReady) {
        if (m_bridgeWasReady || m_sessionServerInstanceNonce != 0 || m_sessionConnectionGeneration != 0 ||
            m_bridgeLifecycleEpoch != 0)
            ResetSessionState(m_connected && m_transport.IsOnline());
        m_bridgeWasReady = false;
        return;
    }

    const auto serverInstanceNonce = m_transport.GetServerInstanceNonce();
    const auto connectionGeneration = m_transport.GetConnectionGeneration();
    const auto lifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    if ((m_sessionServerInstanceNonce != 0 || m_sessionConnectionGeneration != 0 || m_bridgeLifecycleEpoch != 0) &&
        (m_sessionServerInstanceNonce != serverInstanceNonce || m_sessionConnectionGeneration != connectionGeneration ||
         m_bridgeLifecycleEpoch != lifecycleEpoch))
        ResetSessionState(false);

    m_sessionServerInstanceNonce = serverInstanceNonce;
    m_sessionConnectionGeneration = connectionGeneration;
    m_bridgeLifecycleEpoch = lifecycleEpoch;
    m_bridgeWasReady = bridgeReady;
    ExpireStaleState(acEvent.Delta);
}

void VRNpcOwnershipService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);
    ResetSessionState(false);
    m_connected = true;
    m_bridgeWasReady = IsBridgeReady();
    if (m_bridgeWasReady) {
        m_sessionServerInstanceNonce = m_transport.GetServerInstanceNonce();
        m_sessionConnectionGeneration = m_transport.GetConnectionGeneration();
        m_bridgeLifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    }
}

void VRNpcOwnershipService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);
    ResetSessionState(false);
    m_connected = false;
    m_bridgeWasReady = false;
}

bool VRNpcOwnershipService::IsBridgeReady() const noexcept
{
    return m_connected && m_transport.IsOnline() && !m_transport.IsGameplayCleanupRequired() &&
           m_transport.GetServerInstanceNonce() != 0 && m_transport.GetConnectionGeneration() != 0 &&
           SkyrimTogetherVR::GameplayBridgeClient::IsReady() &&
           SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() != 0 &&
           GameplayBridge::HasCapability(SkyrimTogetherVR::GameplayBridgeClient::GetActiveCapabilities(), GameplayBridge::Capability::NpcOwnership) &&
           SkyrimTogether::Protocol::HasCapability(
               m_transport.GetNegotiatedGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::NpcOwnership);
}

bool VRNpcOwnershipService::IsSessionCurrent() const noexcept
{
    return IsBridgeReady() && m_sessionServerInstanceNonce != 0 && m_sessionConnectionGeneration != 0 &&
           m_bridgeLifecycleEpoch != 0 && m_sessionServerInstanceNonce == m_transport.GetServerInstanceNonce() &&
           m_sessionConnectionGeneration == m_transport.GetConnectionGeneration() &&
           m_bridgeLifecycleEpoch == SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
}

bool VRNpcOwnershipService::IsCurrentBridgeRecord(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) const noexcept
{
    const auto& record = acEvent.Record;
    const auto& payload = record.Payload.LocalGameplayAction;
    return IsSessionCurrent() &&
           record.Header.Kind == static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayAction) &&
           record.Header.PayloadSize == GameplayBridge::kFixedPayloadBytes && record.Header.Flags == 0 &&
           record.Header.Identity.ServerInstanceNonce == m_transport.GetServerInstanceNonce() &&
           record.Header.Identity.ConnectionGeneration == m_transport.GetConnectionGeneration() &&
           record.Header.Identity.LifecycleEpoch == SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() &&
           record.Header.Identity.EntityId == 0 && record.Header.Identity.EntityGeneration == 0 &&
           record.Header.Identity.SequenceId == 0 && record.Header.Identity.ActionId != 0 &&
           payload.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value && payload.TargetLocalFormId != 0 &&
           payload.SecondaryHandle.Value == 0 && payload.Reserved0 == 0 &&
           IsZero(payload.ReservedTail, sizeof(payload.ReservedTail));
}

bool VRNpcOwnershipService::IsCurrentProjectileRecord(
    const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) const noexcept
{
    const auto& record = acEvent.Record;
    const auto& header = record.Header;
    const auto& payload = record.Payload.LocalProjectileLaunch;
    const auto bounded = [](const float aValue, const float aLimit) noexcept {
        return std::isfinite(aValue) && aValue >= -aLimit && aValue <= aLimit;
    };
    return IsSessionCurrent() &&
           header.Kind == static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalProjectileLaunch) &&
           header.PayloadSize == GameplayBridge::kFixedPayloadBytes && header.Flags == 0 &&
           header.Identity.ServerInstanceNonce == m_transport.GetServerInstanceNonce() &&
           header.Identity.ConnectionGeneration == m_transport.GetConnectionGeneration() &&
           header.Identity.LifecycleEpoch == SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() &&
           header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 &&
           header.Identity.SequenceId != 0 && header.Identity.SequenceId > m_lastLocalProjectileSequence &&
           header.Identity.ActionId == 0 && header.Identity.Reserved0 == 0 && payload.TargetHandle.Value == 0 &&
           payload.LocalShooterFormId != 0 && payload.LocalProjectileBaseFormId != 0 &&
           payload.LocalParentCellFormId != 0 &&
           bounded(payload.OriginX, GameplayBridge::kMaximumProjectileCoordinate) &&
           bounded(payload.OriginY, GameplayBridge::kMaximumProjectileCoordinate) &&
           bounded(payload.OriginZ, GameplayBridge::kMaximumProjectileCoordinate) &&
           bounded(payload.AngleX, GameplayBridge::kMaximumProjectileAngle) &&
           bounded(payload.AngleZ, GameplayBridge::kMaximumProjectileAngle) &&
           std::isfinite(payload.Power) && payload.Power >= 0.0F &&
           payload.Power <= GameplayBridge::kMaximumProjectilePower && std::isfinite(payload.Scale) &&
           payload.Scale >= 0.0F && payload.Scale <= GameplayBridge::kMaximumProjectileScale &&
           payload.CastingSource >= 0 && payload.CastingSource <= 3 && payload.Area >= 0 &&
           payload.Area <= GameplayBridge::kMaximumProjectileArea &&
           (payload.LaunchFlags & ~GameplayBridge::kProjectileLaunchKnownFlags) == 0 &&
           IsZero(payload.ReservedTail, sizeof(payload.ReservedTail));
}

bool VRNpcOwnershipService::RelayOwnedProjectileLaunch(
    const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept
{
    if (!IsCurrentProjectileRecord(acEvent))
        return false;

    const auto& record = acEvent.Record;
    const auto& payload = record.Payload.LocalProjectileLaunch;
    const auto reference = m_referenceToServer.find(payload.LocalShooterFormId);
    const auto owned = m_ownedByReference.find(payload.LocalShooterFormId);
    if (reference == m_referenceToServer.end() || reference->second == 0 || owned == m_ownedByReference.end() ||
        owned->second.ServerId == 0 || owned->second.ServerId != reference->second)
        return false;

    ProjectileLaunchRequest request{};
    request.ShooterID = owned->second.ServerId;
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
    if (!m_transport.Send(request))
        return false;

    m_lastLocalProjectileSequence = record.Header.Identity.SequenceId;
    return true;
}

void VRNpcOwnershipService::OnLocalGameplay(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept
{
    const auto& record = acEvent.Record;
    if (record.Header.Kind == static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalProjectileLaunch)) {
        TP_UNUSED(RelayOwnedProjectileLaunch(acEvent));
        return;
    }
    if (!IsCurrentBridgeRecord(acEvent))
        return;

    const auto& payload = record.Payload.LocalGameplayAction;
    if (static_cast<GameplayBridge::GameplayDomain>(payload.Domain) != GameplayBridge::GameplayDomain::NpcOwnership ||
        payload.TargetHandle.Value != GameplayBridge::kLocalPlayerHandle.Value || payload.TargetLocalFormId == 0 ||
        record.Header.Identity.ActionId == 0)
        return;

    const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
    PartialSnapshot* partial{};
    if (action != GameplayBridge::GameplayAction::NpcSnapshotBegin) {
        const auto partialIt = m_partialSnapshots.find(payload.TargetLocalFormId);
        if (partialIt == m_partialSnapshots.end())
            return;
        partial = &partialIt->second;
    }
    switch (action) {
    case GameplayBridge::GameplayAction::NpcSnapshotBegin:
    {
        if (record.Header.Identity.ActionId <= m_lastCompletedSnapshotActionId)
            return;
        if (payload.LocalFormIdA == 0 || payload.LocalFormIdB == 0 || !IsFinite(payload.ScalarA) ||
            !IsFinite(payload.ScalarB) || !IsFinite(payload.ScalarC) || !IsFinite(payload.ScalarD) ||
            payload.ValueA != 0 || payload.ValueB != 0 ||
            (payload.ActionFlags & ~(GameplayBridge::kNpcSnapshotDead | GameplayBridge::kNpcSnapshotWeaponDrawn)) != 0)
            return;
        if (!m_partialSnapshots.contains(payload.TargetLocalFormId) && m_partialSnapshots.size() >= kMaximumTrackedNpcs) {
            auto oldest = m_partialSnapshots.begin();
            for (auto it = std::next(oldest); it != m_partialSnapshots.end(); ++it) {
                if (it->second.Age > oldest->second.Age ||
                    (it->second.Age == oldest->second.Age && it->first < oldest->first))
                    oldest = it;
            }
            m_partialSnapshots.erase(oldest);
        }
        auto& newPartial = m_partialSnapshots[payload.TargetLocalFormId];
        newPartial = {};
        newPartial.Begun = true;
        newPartial.ActionId = record.Header.Identity.ActionId;
        newPartial.Data.ReferenceFormId = payload.TargetLocalFormId;
        newPartial.Data.Position = {payload.ScalarA, payload.ScalarB, payload.ScalarC};
        newPartial.Data.ZRotation = payload.ScalarD;
        newPartial.Data.Dead = (payload.ActionFlags & GameplayBridge::kNpcSnapshotDead) != 0;
        newPartial.Data.WeaponDrawn = (payload.ActionFlags & GameplayBridge::kNpcSnapshotWeaponDrawn) != 0;
        // Keep raw local IDs in BaseId/BaseId.ModId until the complete record
        // is translated as one all-or-nothing snapshot.
        newPartial.Data.BaseId.BaseId = payload.LocalFormIdA;
        newPartial.Data.CellId.BaseId = payload.LocalFormIdB;
        newPartial.Data.WorldspaceId.BaseId = payload.LocalFormIdC;
        newPartial.Data.PackageId.BaseId = payload.LocalFormIdD;
        return;
    }
    case GameplayBridge::GameplayAction::NpcSnapshotValue:
    {
        const auto index = ActorValueIndex(payload.LocalFormIdA);
        if (!partial->Begun || partial->ActionId != record.Header.Identity.ActionId || index == kActorValues.size() ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA != 0 || payload.ValueB != 0 || !IsFinite(payload.ScalarA) || !IsFinite(payload.ScalarB) ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return;
        partial->Data.Values[index] = payload.ScalarA;
        partial->Data.Maximums[index] = payload.ScalarB;
        partial->HasValue[index] = true;
        return;
    }
    case GameplayBridge::GameplayAction::NpcSnapshotItem:
        if (!partial->Begun || partial->ActionId != record.Header.Identity.ActionId || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA <= 0 || payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            (payload.ActionFlags & ~GameplayBridge::kInventoryQuestItem) != 0 ||
            (partial->Data.Inventory.size() >= kMaximumNpcItems && !partial->Data.Inventory.contains(payload.LocalFormIdA)))
            return;
        partial->Data.Inventory[payload.LocalFormIdA] = {{}, payload.ValueA,
                                                          (payload.ActionFlags & GameplayBridge::kInventoryQuestItem) != 0};
        return;
    case GameplayBridge::GameplayAction::NpcSnapshotFaction:
        if (!partial->Begun || partial->ActionId != record.Header.Identity.ActionId || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA < std::numeric_limits<std::int8_t>::min() ||
            payload.ValueA > std::numeric_limits<std::int8_t>::max() ||
            payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0 ||
            partial->Data.FactionData.ExtraFactions.size() >= kMaximumNpcFactions)
            return;
        {
            Faction faction{};
            faction.Id.BaseId = payload.LocalFormIdA;
            faction.Rank = static_cast<std::int8_t>(payload.ValueA);
            partial->Data.FactionData.ExtraFactions.push_back(faction);
        }
        return;
    case GameplayBridge::GameplayAction::NpcSnapshotEnd:
        if (!partial->Begun || partial->ActionId != record.Header.Identity.ActionId ||
            payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0 ||
            !std::all_of(partial->HasValue.begin(), partial->HasValue.end(), [](const bool aValue) { return aValue; }) ||
            !TranslateSnapshot(*partial))
            return;
        HandleCompleteSnapshot(std::move(partial->Data));
        m_partialSnapshots.erase(payload.TargetLocalFormId);
        m_lastCompletedSnapshotActionId = record.Header.Identity.ActionId;
        return;
    default:
        return;
    }
}

bool VRNpcOwnershipService::TranslateSnapshot(PartialSnapshot& arSnapshot) const noexcept
{
    auto& modSystem = m_world.GetModSystem();
    auto& data = arSnapshot.Data;
    const auto rawBase = data.BaseId.BaseId;
    const auto rawCell = data.CellId.BaseId;
    const auto rawWorldspace = data.WorldspaceId.BaseId;
    const auto rawPackage = data.PackageId.BaseId;
    if (!IsValidPosition(data.Position) || !IsFinite(data.ZRotation) ||
        !modSystem.GetServerModId(data.ReferenceFormId, data.ReferenceId) || !data.ReferenceId ||
        !modSystem.GetServerModId(rawBase, data.BaseId) || !data.BaseId ||
        !modSystem.GetServerModId(rawCell, data.CellId) || !data.CellId ||
        (rawWorldspace != 0 && (!modSystem.GetServerModId(rawWorldspace, data.WorldspaceId) || !data.WorldspaceId)) ||
        (rawPackage != 0 && (!modSystem.GetServerModId(rawPackage, data.PackageId) || !data.PackageId)))
        return false;

    for (auto& [localId, entry] : data.Inventory) {
        if (!modSystem.GetServerModId(localId, entry.Id) || !entry.Id)
            return false;
    }
    for (auto& faction : data.FactionData.ExtraFactions) {
        const auto localId = faction.Id.BaseId;
        if (!modSystem.GetServerModId(localId, faction.Id) || !faction.Id)
            return false;
    }
    return true;
}

bool VRNpcOwnershipService::BuildActorData(const Snapshot& acSnapshot, ActorData& arActorData) const noexcept
{
    arActorData = {};
    for (std::size_t index = 0; index < kActorValues.size(); ++index) {
        arActorData.InitialActorValues.ActorValuesList.emplace(kActorValues[index], acSnapshot.Values[index]);
        arActorData.InitialActorValues.ActorMaxValuesList.emplace(kActorValues[index], acSnapshot.Maximums[index]);
    }
    for (const auto& [localId, entry] : acSnapshot.Inventory) {
        TP_UNUSED(localId);
        if (!entry.Id || entry.Count <= 0)
            return false;
        auto& output = arActorData.InitialInventory.Entries.emplace_back();
        output.BaseId = entry.Id;
        output.Count = entry.Count;
        output.IsQuestItem = entry.QuestItem;
    }
    arActorData.IsDead = acSnapshot.Dead;
    arActorData.IsWeaponDrawn = acSnapshot.WeaponDrawn;
    return true;
}

void VRNpcOwnershipService::HandleCompleteSnapshot(Snapshot&& aSnapshot) noexcept
{
    const auto referenceId = aSnapshot.ReferenceFormId;

    if (const auto claim = m_claimAfterSnapshot.find(referenceId); claim != m_claimAfterSnapshot.end()) {
        if (!StoreLatestSnapshot(std::move(aSnapshot)))
            return;
        if (claim->second.ClaimSent || claim->second.RetryDelay > 0.0)
            return;
        if (RequestOwnershipClaim(claim->second.ServerId, claim->second.GrantToken,
                                  m_latestSnapshots.at(referenceId)))
            claim->second.ClaimSent = true;
        else
            claim->second.RetryDelay = 1.0;
        return;
    }
    if (auto owned = m_ownedByReference.find(referenceId); owned != m_ownedByReference.end()) {
        ReplicateOwnedSnapshot(owned->second, aSnapshot);
        return;
    }
    if (m_referenceToServer.contains(referenceId))
        return;
    if (!m_pendingAssignments.empty()) {
        const auto pending = std::find_if(m_pendingAssignments.begin(), m_pendingAssignments.end(), [referenceId](const auto& acPair) {
            return acPair.second.ReferenceFormId == referenceId;
        });
        if (pending != m_pendingAssignments.end())
        {
            StoreLatestSnapshot(std::move(aSnapshot));
            return;
        }
    }
    if (!StoreLatestSnapshot(std::move(aSnapshot)))
        return;
    RequestAssignment(m_latestSnapshots.at(referenceId));
}

void VRNpcOwnershipService::RequestAssignment(const Snapshot& acSnapshot) noexcept
{
    if (!IsSessionCurrent() || acSnapshot.ReferenceFormId == 0 || !acSnapshot.ReferenceId || !acSnapshot.BaseId ||
        !acSnapshot.CellId || m_referenceToServer.contains(acSnapshot.ReferenceFormId))
        return;

    ActorData actorData{};
    if (!BuildActorData(acSnapshot, actorData))
        return;
    if (m_pendingAssignments.size() >= kMaximumTrackedNpcs) {
        auto oldest = m_pendingAssignments.begin();
        for (auto it = std::next(oldest); it != m_pendingAssignments.end(); ++it) {
            if (it->second.Order < oldest->second.Order ||
                (it->second.Order == oldest->second.Order && it->first < oldest->first))
                oldest = it;
        }
        ClearReference(oldest->second.ReferenceFormId);
    }
    AssignCharacterRequest request{};
    do {
        request.Cookie = m_nextAssignmentCookie++;
    } while (request.Cookie == 0 || m_pendingAssignments.contains(request.Cookie));
    request.ReferenceId = acSnapshot.ReferenceId;
    request.FormId = acSnapshot.BaseId;
    request.CellId = acSnapshot.CellId;
    request.WorldSpaceId = acSnapshot.WorldspaceId;
    request.Position = acSnapshot.Position;
    request.Rotation = {0.0F, acSnapshot.ZRotation};
    request.CurrentActorData = actorData;
    request.FactionsContent = acSnapshot.FactionData;
    if (!m_transport.Send(request))
        return;
    m_pendingAssignments.emplace(request.Cookie, PendingAssignment{acSnapshot.ReferenceFormId, m_nextStateOrder++, 0.0});
}

void VRNpcOwnershipService::OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept
{
    const auto pending = m_pendingAssignments.find(acMessage.Cookie);
    if (pending == m_pendingAssignments.end())
        return;
    const auto referenceId = pending->second.ReferenceFormId;
    m_pendingAssignments.erase(pending);
    if (!IsSessionCurrent() || acMessage.ServerId == 0 || !TrackServerReference(acMessage.ServerId, referenceId))
        return;
    if (!acMessage.Owner) {
        m_latestSnapshots.erase(referenceId);
        return;
    }
    const auto snapshot = m_latestSnapshots.find(referenceId);
    if (snapshot == m_latestSnapshots.end())
        return;
    auto& owned = m_ownedByReference[referenceId];
    owned.ServerId = acMessage.ServerId;
    owned.Baseline = snapshot->second;
    owned.HasBaseline = true;
    owned.LastRelayedPackageId = snapshot->second.PackageId;
    m_latestSnapshots.erase(snapshot);
    StartObservation(referenceId);
}

void VRNpcOwnershipService::OnCharacterSpawn(const CharacterSpawnRequest& acMessage) noexcept
{
    if (!IsSessionCurrent() || acMessage.ServerId == 0 || !acMessage.FormId)
        return;
    auto localReference = std::uint32_t{};
    if (const auto* avatars = m_world.ctx().find<VRAvatarService>())
        localReference = avatars->GetPersistentLocalReferenceForServerId(acMessage.ServerId);
    if (localReference == 0)
        localReference = m_world.GetModSystem().GetGameId(acMessage.FormId);
    if (localReference != 0)
        TrackServerReference(acMessage.ServerId, localReference);
}

void VRNpcOwnershipService::OnOwnershipTransfer(const NotifyOwnershipTransfer& acMessage) noexcept
{
    if (!IsSessionCurrent() || acMessage.ServerId == 0)
        return;
    const auto reference = m_serverToReference.find(acMessage.ServerId);
    if (acMessage.GrantToken == 0) {
        if (reference == m_serverToReference.end())
            return;
        const auto claim = m_claimAfterSnapshot.find(reference->second);
        if (claim == m_claimAfterSnapshot.end() || claim->second.ServerId != acMessage.ServerId ||
            claim->second.GrantToken == 0 || !claim->second.ClaimSent)
            return;
        const auto snapshot = m_latestSnapshots.find(reference->second);
        if (snapshot == m_latestSnapshots.end())
            return;
        auto& owned = m_ownedByReference[reference->second];
        owned.ServerId = acMessage.ServerId;
        owned.Baseline = snapshot->second;
        owned.HasBaseline = true;
        owned.LastRelayedPackageId = snapshot->second.PackageId;
        m_latestSnapshots.erase(snapshot);
        m_claimAfterSnapshot.erase(claim);
        return;
    }
    if (reference == m_serverToReference.end()) {
        if (!m_pendingGrantByServer.contains(acMessage.ServerId) &&
            m_pendingGrantByServer.size() >= kMaximumTrackedNpcs) {
            auto oldest = m_pendingGrantByServer.begin();
            for (auto it = std::next(oldest); it != m_pendingGrantByServer.end(); ++it) {
                if (it->second.Order < oldest->second.Order ||
                    (it->second.Order == oldest->second.Order && it->first < oldest->first))
                    oldest = it;
            }
            m_pendingGrantByServer.erase(oldest);
        }
        m_pendingGrantByServer[acMessage.ServerId] =
            PendingClaim{acMessage.ServerId, acMessage.GrantToken, m_nextStateOrder++, 0.0, 0.0, false};
        return;
    }
    if (const auto owned = m_ownedByReference.find(reference->second);
        owned != m_ownedByReference.end() && owned->second.ServerId == acMessage.ServerId)
        return;
    if (const auto claim = m_claimAfterSnapshot.find(reference->second);
        claim != m_claimAfterSnapshot.end() && claim->second.ServerId == acMessage.ServerId &&
        claim->second.GrantToken == acMessage.GrantToken)
        return;
    if (!m_claimAfterSnapshot.contains(reference->second) && m_claimAfterSnapshot.size() >= kMaximumTrackedNpcs) {
        auto oldest = m_claimAfterSnapshot.begin();
        for (auto it = std::next(oldest); it != m_claimAfterSnapshot.end(); ++it) {
            if (it->second.Order < oldest->second.Order ||
                (it->second.Order == oldest->second.Order && it->first < oldest->first))
                oldest = it;
        }
        ClearReference(oldest->first);
    }
    m_claimAfterSnapshot[reference->second] =
        PendingClaim{acMessage.ServerId, acMessage.GrantToken, m_nextStateOrder++, 0.0, 0.0, false};
    if (!StartObservation(reference->second))
        m_claimAfterSnapshot.erase(reference->second);
}

bool VRNpcOwnershipService::RequestOwnershipClaim(const std::uint32_t aServerId, const std::uint64_t aGrantToken,
                                                   const Snapshot& acSnapshot) noexcept
{
    const auto reference = m_referenceToServer.find(acSnapshot.ReferenceFormId);
    if (!IsSessionCurrent() || aServerId == 0 || aGrantToken == 0 || acSnapshot.ReferenceFormId == 0 || reference == m_referenceToServer.end() ||
        reference->second != aServerId || !m_serverToReference.contains(aServerId) ||
        m_serverToReference.at(aServerId) != acSnapshot.ReferenceFormId || m_ownedByReference.contains(acSnapshot.ReferenceFormId))
        return false;
    ActorData actorData{};
    if (!BuildActorData(acSnapshot, actorData))
        return false;
    RequestOwnershipClaim request{};
    request.ServerId = aServerId;
    request.GrantToken = aGrantToken;
    request.NewActorData = actorData;
    return m_transport.Send(request);
}

bool VRNpcOwnershipService::StoreLatestSnapshot(Snapshot&& aSnapshot) noexcept
{
    const auto referenceId = aSnapshot.ReferenceFormId;
    if (referenceId == 0)
        return false;
    if (!m_latestSnapshots.contains(referenceId) && m_latestSnapshots.size() >= kMaximumTrackedNpcs) {
        auto oldest = m_latestSnapshots.begin();
        for (auto it = std::next(oldest); it != m_latestSnapshots.end(); ++it) {
            if (it->second.Order < oldest->second.Order ||
                (it->second.Order == oldest->second.Order && it->first < oldest->first))
                oldest = it;
        }
        ClearReference(oldest->first);
    }
    aSnapshot.Order = m_nextStateOrder++;
    aSnapshot.Age = 0.0;
    m_latestSnapshots.insert_or_assign(referenceId, std::move(aSnapshot));
    return true;
}

bool VRNpcOwnershipService::TrackServerReference(const std::uint32_t aServerId, const std::uint32_t aReferenceFormId) noexcept
{
    if (!IsSessionCurrent() || aServerId == 0 || aReferenceFormId == 0)
        return false;
    if (const auto server = m_serverToReference.find(aServerId); server != m_serverToReference.end() &&
        server->second != aReferenceFormId)
        ClearReference(server->second);
    if (const auto reference = m_referenceToServer.find(aReferenceFormId); reference != m_referenceToServer.end() &&
        reference->second != aServerId)
        ClearReference(aReferenceFormId);
    if (m_serverToReference.contains(aServerId) && m_referenceToServer.contains(aReferenceFormId))
        return m_serverToReference.at(aServerId) == aReferenceFormId &&
               m_referenceToServer.at(aReferenceFormId) == aServerId;
    if (m_serverToReference.size() >= kMaximumTrackedNpcs) {
        auto oldest = m_serverToReference.begin();
        for (auto it = std::next(oldest); it != m_serverToReference.end(); ++it) {
            if (it->first < oldest->first)
                oldest = it;
        }
        ClearReference(oldest->second);
    }
    m_serverToReference.emplace(aServerId, aReferenceFormId);
    m_referenceToServer.emplace(aReferenceFormId, aServerId);
    PromotePendingGrant(aServerId, aReferenceFormId);
    return true;
}

void VRNpcOwnershipService::PromotePendingGrant(const std::uint32_t aServerId,
                                                const std::uint32_t aReferenceFormId) noexcept
{
    const auto pending = m_pendingGrantByServer.find(aServerId);
    if (pending == m_pendingGrantByServer.end() || pending->second.GrantToken == 0)
        return;
    if (!m_claimAfterSnapshot.contains(aReferenceFormId) && m_claimAfterSnapshot.size() >= kMaximumTrackedNpcs) {
        auto oldest = m_claimAfterSnapshot.begin();
        for (auto it = std::next(oldest); it != m_claimAfterSnapshot.end(); ++it) {
            if (it->second.Order < oldest->second.Order ||
                (it->second.Order == oldest->second.Order && it->first < oldest->first))
                oldest = it;
        }
        ClearReference(oldest->first);
    }
    m_claimAfterSnapshot.insert_or_assign(aReferenceFormId, pending->second);
    m_pendingGrantByServer.erase(pending);
    if (!StartObservation(aReferenceFormId))
        m_claimAfterSnapshot.erase(aReferenceFormId);
}

bool VRNpcOwnershipService::StartObservation(const std::uint32_t aReferenceFormId) noexcept
{
    if (!IsSessionCurrent() || aReferenceFormId == 0)
        return false;
    auto* avatars = m_world.ctx().find<VRAvatarService>();
    GameplayBridge::CommandRecord command{};
    if (!avatars || !avatars->BuildLocalGameplayCommand(
                        GameplayBridge::GameplayDomain::NpcOwnership,
                        GameplayBridge::GameplayAction::StartNpcObservation,
                        command))
        return false;
    command.Payload.ApplyGameplayAction.TargetLocalFormId = aReferenceFormId;
    return SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command);
}

void VRNpcOwnershipService::StopObservation(const std::uint32_t aReferenceFormId) noexcept
{
    if (!IsSessionCurrent() || aReferenceFormId == 0)
        return;
    auto* avatars = m_world.ctx().find<VRAvatarService>();
    GameplayBridge::CommandRecord command{};
    if (!avatars || !avatars->BuildLocalGameplayCommand(
                        GameplayBridge::GameplayDomain::NpcOwnership,
                        GameplayBridge::GameplayAction::StopNpcObservation,
                        command))
        return;
    command.Payload.ApplyGameplayAction.TargetLocalFormId = aReferenceFormId;
    SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command);
}

void VRNpcOwnershipService::ReplicateOwnedSnapshot(OwnedNpc& arOwned, const Snapshot& acSnapshot) noexcept
{
    if (!IsSessionCurrent() || arOwned.ServerId == 0)
        return;
    if (!arOwned.HasBaseline) {
        arOwned.Baseline = acSnapshot;
        arOwned.HasBaseline = true;
        arOwned.LastRelayedPackageId = acSnapshot.PackageId;
        return;
    }
    const auto& baseline = arOwned.Baseline;
    auto acceptedBaseline = baseline;
    if (baseline.CellId != acSnapshot.CellId || baseline.WorldspaceId != acSnapshot.WorldspaceId ||
        baseline.Position != acSnapshot.Position || baseline.ZRotation != acSnapshot.ZRotation) {
        ClientReferencesMoveRequest request{};
        request.Tick = m_world.GetTick();
        auto& movement = request.Updates[arOwned.ServerId].UpdatedMovement;
        movement.CellId = acSnapshot.CellId;
        movement.WorldSpaceId = acSnapshot.WorldspaceId;
        movement.Position = acSnapshot.Position;
        movement.Rotation = {0.0F, acSnapshot.ZRotation};
        if (m_transport.Send(request)) {
            acceptedBaseline.CellId = acSnapshot.CellId;
            acceptedBaseline.WorldspaceId = acSnapshot.WorldspaceId;
            acceptedBaseline.Position = acSnapshot.Position;
            acceptedBaseline.ZRotation = acSnapshot.ZRotation;
        }
    }
    RequestActorValueChanges values{};
    RequestActorMaxValueChanges maximums{};
    values.Id = arOwned.ServerId;
    maximums.Id = arOwned.ServerId;
    for (std::size_t index = 0; index < kActorValues.size(); ++index) {
        if (acSnapshot.Values[index] != baseline.Values[index])
            values.Values.emplace(kActorValues[index], acSnapshot.Values[index]);
        if (acSnapshot.Maximums[index] != baseline.Maximums[index])
            maximums.Values.emplace(kActorValues[index], acSnapshot.Maximums[index]);
    }
    if (!values.Values.empty() && m_transport.Send(values)) {
        for (std::size_t index = 0; index < kActorValues.size(); ++index) {
            if (acSnapshot.Values[index] != baseline.Values[index])
                acceptedBaseline.Values[index] = acSnapshot.Values[index];
        }
    }
    if (!maximums.Values.empty() && m_transport.Send(maximums)) {
        for (std::size_t index = 0; index < kActorValues.size(); ++index) {
            if (acSnapshot.Maximums[index] != baseline.Maximums[index])
                acceptedBaseline.Maximums[index] = acSnapshot.Maximums[index];
        }
    }
    for (const auto& [localId, current] : acSnapshot.Inventory) {
        const auto previous = baseline.Inventory.find(localId);
        const auto previousCount = previous != baseline.Inventory.end() ? previous->second.Count : 0;
        if (current.Count == previousCount && previous != baseline.Inventory.end() && current.QuestItem == previous->second.QuestItem)
            continue;
        if (current.Count == previousCount && previous != baseline.Inventory.end()) {
            // Quest-item metadata does not have an independent wire operation;
            // accepting it locally avoids an unsendable zero-count retry loop.
            acceptedBaseline.Inventory.insert_or_assign(localId, current);
            continue;
        }
        RequestInventoryChanges request{};
        request.ServerId = arOwned.ServerId;
        request.Item.BaseId = current.Id;
        request.Item.Count = current.Count - previousCount;
        request.Item.IsQuestItem = current.QuestItem;
        if (request.Item.Count != 0 && m_transport.Send(request))
            acceptedBaseline.Inventory.insert_or_assign(localId, current);
    }
    for (const auto& [localId, previous] : baseline.Inventory) {
        if (acSnapshot.Inventory.contains(localId))
            continue;
        RequestInventoryChanges request{};
        request.ServerId = arOwned.ServerId;
        request.Item.BaseId = previous.Id;
        request.Item.Count = -previous.Count;
        request.Item.IsQuestItem = previous.QuestItem;
        if (m_transport.Send(request))
            acceptedBaseline.Inventory.erase(localId);
    }
    if (acSnapshot.FactionData != baseline.FactionData) {
        RequestFactionsChanges request{};
        request.Changes.emplace(arOwned.ServerId, acSnapshot.FactionData);
        if (m_transport.Send(request))
            acceptedBaseline.FactionData = acSnapshot.FactionData;
    }
    if (acSnapshot.Dead != baseline.Dead) {
        RequestDeathStateChange request{};
        request.Id = arOwned.ServerId;
        request.IsDead = acSnapshot.Dead;
        if (m_transport.Send(request))
            acceptedBaseline.Dead = acSnapshot.Dead;
    }
    if (acSnapshot.WeaponDrawn != baseline.WeaponDrawn) {
        DrawWeaponRequest request{};
        request.Id = arOwned.ServerId;
        request.IsWeaponDrawn = acSnapshot.WeaponDrawn;
        if (m_transport.Send(request))
            acceptedBaseline.WeaponDrawn = acSnapshot.WeaponDrawn;
    }
    if (!acSnapshot.PackageId || acSnapshot.PackageId == arOwned.LastRelayedPackageId) {
        arOwned.CandidatePackageId = {};
        arOwned.CandidatePackageObservations = 0;
    } else if (acSnapshot.PackageId == arOwned.CandidatePackageId) {
        if (arOwned.CandidatePackageObservations < std::numeric_limits<std::uint8_t>::max())
            ++arOwned.CandidatePackageObservations;
    } else {
        arOwned.CandidatePackageId = acSnapshot.PackageId;
        arOwned.CandidatePackageObservations = 1;
    }
    if (arOwned.CandidatePackageObservations >= 2) {
        NewPackageRequest request{};
        request.ActorId = arOwned.ServerId;
        request.PackageId = arOwned.CandidatePackageId;
        if (m_transport.Send(request)) {
            arOwned.LastRelayedPackageId = arOwned.CandidatePackageId;
            acceptedBaseline.PackageId = arOwned.CandidatePackageId;
            arOwned.CandidatePackageId = {};
            arOwned.CandidatePackageObservations = 0;
        }
    }
    arOwned.Baseline = std::move(acceptedBaseline);
}

void VRNpcOwnershipService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept
{
    m_pendingGrantByServer.erase(acMessage.ServerId);
    const auto reference = m_serverToReference.find(acMessage.ServerId);
    if (reference != m_serverToReference.end())
        ClearReference(reference->second);
}

void VRNpcOwnershipService::OnRelinquishControl(const NotifyRelinquishControl& acMessage) noexcept
{
    m_pendingGrantByServer.erase(acMessage.ServerId);
    const auto reference = m_serverToReference.find(acMessage.ServerId);
    if (reference != m_serverToReference.end())
        ClearReference(reference->second);
}

void VRNpcOwnershipService::ClearReference(const std::uint32_t aReferenceFormId) noexcept
{
    StopObservation(aReferenceFormId);
    const auto server = m_referenceToServer.find(aReferenceFormId);
    if (server != m_referenceToServer.end())
        m_pendingGrantByServer.erase(server->second);
    m_referenceToServer.erase(aReferenceFormId);
    for (auto it = m_serverToReference.begin(); it != m_serverToReference.end();) {
        if (it->second == aReferenceFormId)
            it = m_serverToReference.erase(it);
        else
            ++it;
    }
    m_ownedByReference.erase(aReferenceFormId);
    m_claimAfterSnapshot.erase(aReferenceFormId);
    m_latestSnapshots.erase(aReferenceFormId);
    m_partialSnapshots.erase(aReferenceFormId);
    for (auto it = m_pendingAssignments.begin(); it != m_pendingAssignments.end();) {
        if (it->second.ReferenceFormId == aReferenceFormId)
            it = m_pendingAssignments.erase(it);
        else
            ++it;
    }
}

void VRNpcOwnershipService::RelinquishOwned(const bool aSendTransfer) noexcept
{
    for (const auto& [referenceId, owned] : m_ownedByReference) {
        if (aSendTransfer && m_connected && m_transport.IsOnline() && !m_transport.IsGameplayCleanupRequired() &&
            m_transport.GetServerInstanceNonce() != 0 && m_transport.GetConnectionGeneration() != 0 && owned.ServerId != 0 &&
            owned.HasBaseline && owned.Baseline.CellId && IsValidPosition(owned.Baseline.Position) &&
            SkyrimTogether::Protocol::HasCapability(
                m_transport.GetNegotiatedGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::NpcOwnership)) {
            RequestOwnershipTransfer request{};
            request.ServerId = owned.ServerId;
            request.CellId = owned.Baseline.CellId;
            request.WorldSpaceId = owned.Baseline.WorldspaceId;
            request.Position = owned.Baseline.Position;
            m_transport.Send(request);
        }
        StopObservation(referenceId);
    }
    m_ownedByReference.clear();
    m_claimAfterSnapshot.clear();
    m_pendingGrantByServer.clear();
}

void VRNpcOwnershipService::ResetSessionState(const bool aSendTransfer) noexcept
{
    RelinquishOwned(aSendTransfer);
    m_partialSnapshots.clear();
    m_latestSnapshots.clear();
    m_pendingAssignments.clear();
    m_claimAfterSnapshot.clear();
    m_pendingGrantByServer.clear();
    m_serverToReference.clear();
    m_referenceToServer.clear();
    m_lastCompletedSnapshotActionId = 0;
    m_lastLocalProjectileSequence = 0;
    m_sessionServerInstanceNonce = 0;
    m_sessionConnectionGeneration = 0;
    m_bridgeLifecycleEpoch = 0;
}

void VRNpcOwnershipService::ExpireStaleState(const double aDelta) noexcept
{
    if (!std::isfinite(aDelta) || aDelta <= 0.0)
        return;

    for (auto it = m_partialSnapshots.begin(); it != m_partialSnapshots.end();) {
        it->second.Age += aDelta;
        if (it->second.Age >= kPartialSnapshotLifetime)
            it = m_partialSnapshots.erase(it);
        else
            ++it;
    }
    for (auto& [referenceId, snapshot] : m_latestSnapshots) {
        TP_UNUSED(referenceId);
        snapshot.Age += aDelta;
    }
    for (auto& [cookie, pending] : m_pendingAssignments) {
        TP_UNUSED(cookie);
        pending.Age += aDelta;
    }
    for (auto& [referenceId, claim] : m_claimAfterSnapshot) {
        TP_UNUSED(referenceId);
        claim.Age += aDelta;
        claim.RetryDelay = std::max(0.0, claim.RetryDelay - aDelta);
    }
    for (auto& [serverId, grant] : m_pendingGrantByServer) {
        TP_UNUSED(serverId);
        grant.Age += aDelta;
        grant.RetryDelay = std::max(0.0, grant.RetryDelay - aDelta);
    }

    for (auto it = m_latestSnapshots.begin(); it != m_latestSnapshots.end();) {
        if (it->second.Age < kPendingAssignmentLifetime) {
            ++it;
            continue;
        }
        const auto referenceId = it->first;
        ClearReference(referenceId);
        it = m_latestSnapshots.begin();
    }
    for (auto it = m_pendingAssignments.begin(); it != m_pendingAssignments.end();) {
        if (it->second.Age < kPendingAssignmentLifetime) {
            ++it;
            continue;
        }
        const auto referenceId = it->second.ReferenceFormId;
        ClearReference(referenceId);
        it = m_pendingAssignments.begin();
    }
    for (auto it = m_claimAfterSnapshot.begin(); it != m_claimAfterSnapshot.end();) {
        if (it->second.Age < kPendingClaimLifetime) {
            ++it;
            continue;
        }
        const auto referenceId = it->first;
        ClearReference(referenceId);
        it = m_claimAfterSnapshot.begin();
    }
    for (auto it = m_pendingGrantByServer.begin(); it != m_pendingGrantByServer.end();) {
        if (it->second.Age < kPendingClaimLifetime) {
            ++it;
            continue;
        }
        it = m_pendingGrantByServer.erase(it);
    }
}
