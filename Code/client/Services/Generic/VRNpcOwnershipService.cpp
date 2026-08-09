#include <TiltedOnlinePCH.h>

#include <Services/VRNpcOwnershipService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/LocalGameplayBridgeEvent.h>
#include <Events/RemoteGameplayBridgeResultEvent.h>
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
#include <Services/VRActorReplicationService.h>
#include <Structs/GameplayCapabilities.h>
#include <VRGameplayBridge.h>
#include <World.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>

namespace GameplayBridge = SkyrimTogetherVR::GameplayBridge;
namespace AnimationGraphProtocol = SkyrimTogetherVR::AnimationGraphProtocol;

namespace
{
constexpr auto kActorValues = [] {
    std::array<std::uint32_t, GameplayBridge::kSkyrimActorValueCount> values{};
    for (std::size_t index = 0; index < values.size(); ++index)
        values[index] = static_cast<std::uint32_t>(index);
    return values;
}();
constexpr std::size_t kMaximumNpcItems = GameplayBridge::kMaximumNpcSnapshotItems;
constexpr std::size_t kMaximumNpcFactions = GameplayBridge::kMaximumNpcSnapshotFactions;
constexpr std::size_t kMaximumTrackedNpcs = 64;
constexpr float kMaximumNetworkPosition = 10000000.0F;
constexpr double kPartialSnapshotLifetime = 2.0;
constexpr double kPendingAssignmentLifetime = 15.0;
constexpr double kPendingClaimLifetime = 15.0;
constexpr double kPendingObservationLifetime = 5.0;

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
    return aValue < kActorValues.size() ? static_cast<std::size_t>(aValue) : kActorValues.size();
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

[[nodiscard]] bool SameIdentity(const GameplayBridge::BridgeIdentity& acLeft,
                                const GameplayBridge::BridgeIdentity& acRight) noexcept
{
    return acLeft.ServerInstanceNonce == acRight.ServerInstanceNonce &&
           acLeft.ConnectionGeneration == acRight.ConnectionGeneration &&
           acLeft.LifecycleEpoch == acRight.LifecycleEpoch && acLeft.EntityId == acRight.EntityId &&
           acLeft.EntityGeneration == acRight.EntityGeneration && acLeft.Reserved0 == acRight.Reserved0 &&
           acLeft.SequenceId == acRight.SequenceId && acLeft.ActionId == acRight.ActionId;
}

[[nodiscard]] bool SameAnimationSnapshot(const AnimationGraphProtocol::SnapshotBuffer& acLeft,
                                         const AnimationGraphProtocol::SnapshotBuffer& acRight) noexcept
{
    if (std::bit_cast<std::uint32_t>(acLeft.Direction) != std::bit_cast<std::uint32_t>(acRight.Direction) ||
        acLeft.BooleanCount != acRight.BooleanCount || acLeft.FloatCount != acRight.FloatCount ||
        acLeft.IntegerCount != acRight.IntegerCount)
        return false;
    for (std::size_t index = 0; index < acLeft.BooleanCount; ++index) {
        if (acLeft.Booleans[index] != acRight.Booleans[index])
            return false;
    }
    for (std::size_t index = 0; index < acLeft.FloatCount; ++index) {
        if (std::bit_cast<std::uint32_t>(acLeft.Floats[index]) !=
            std::bit_cast<std::uint32_t>(acRight.Floats[index]))
            return false;
    }
    for (std::size_t index = 0; index < acLeft.IntegerCount; ++index) {
        if (acLeft.Integers[index] != acRight.Integers[index])
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
    m_gameplayResultConnection = aDispatcher.sink<SkyrimTogetherVR::RemoteGameplayBridgeResultEvent>()
        .connect<&VRNpcOwnershipService::OnGameplayResult>(this);
    m_assignCharacterConnection = aDispatcher.sink<AssignCharacterResponse>().connect<&VRNpcOwnershipService::OnAssignCharacter>(this);
    m_characterSpawnConnection = aDispatcher.sink<CharacterSpawnRequest>().connect<&VRNpcOwnershipService::OnCharacterSpawn>(this);
    m_ownershipTransferConnection = aDispatcher.sink<NotifyOwnershipTransfer>().connect<&VRNpcOwnershipService::OnOwnershipTransfer>(this);
    m_removeCharacterConnection = aDispatcher.sink<NotifyRemoveCharacter>().connect<&VRNpcOwnershipService::OnRemoveCharacter>(this);
    m_relinquishConnection = aDispatcher.sink<NotifyRelinquishControl>().connect<&VRNpcOwnershipService::OnRelinquishControl>(this);
}

std::uint32_t VRNpcOwnershipService::GetServerIdForLocalReference(
    const std::uint32_t aReferenceFormId) const noexcept
{
    if (!IsSessionCurrent() || aReferenceFormId == 0)
        return 0;
    const auto reference = m_referenceToServer.find(aReferenceFormId);
    const auto owned = m_ownedByReference.find(aReferenceFormId);
    if (reference == m_referenceToServer.end() || owned == m_ownedByReference.end() || reference->second == 0 ||
        owned->second.ServerId == 0 || owned->second.ServerId != reference->second)
        return 0;
    return reference->second;
}

std::uint32_t VRNpcOwnershipService::GetLocalReferenceForOwnedServerId(
    const std::uint32_t aServerId) const noexcept
{
    if (!IsSessionCurrent() || aServerId == 0)
        return 0;
    const auto server = m_serverToReference.find(aServerId);
    if (server == m_serverToReference.end() || server->second == 0)
        return 0;
    const auto reference = m_referenceToServer.find(server->second);
    const auto owned = m_ownedByReference.find(server->second);
    if (reference == m_referenceToServer.end() || owned == m_ownedByReference.end() ||
        reference->second != aServerId || owned->second.ServerId != aServerId)
        return 0;
    return server->second;
}

void VRNpcOwnershipService::CommitRemoteInventoryTransaction(
    const std::uint32_t aServerId, const std::vector<Inventory::Entry>& acEntries,
    const bool aReset) noexcept try
{
    if (!IsSessionCurrent() || aServerId == 0)
        return;

    const auto server = m_serverToReference.find(aServerId);
    if (server == m_serverToReference.end() || server->second == 0)
        return;
    const auto reference = m_referenceToServer.find(server->second);
    const auto owned = m_ownedByReference.find(server->second);
    if (reference == m_referenceToServer.end() || reference->second != aServerId ||
        owned == m_ownedByReference.end() || owned->second.ServerId != aServerId ||
        !owned->second.HasBaseline)
        return;

    for (const auto& entry : acEntries) {
        if (!entry.IsValidMutation() || (aReset && entry.Count < 0))
            return;
    }

    auto baseline = owned->second.Baseline;
    const auto sameMetadata = [](const Inventory::Entry& acLeft,
                                 const Inventory::Entry& acRight) noexcept {
        // The native reset path coalesces equivalent stacks and retains quest
        // status separately. Mirror that semantics instead of preserving wire
        // ordering as duplicate baseline entries.
        auto left = acLeft;
        auto right = acRight;
        left.IsQuestItem = false;
        right.IsQuestItem = false;
        return left.BaseId == right.BaseId && left.IsExtraDataEquals(right);
    };
    const auto coalesce = [&sameMetadata](const std::vector<Inventory::Entry>& acSource,
                                          const bool aReset,
                                          std::vector<InventoryEntry>& arEntries) {
        arEntries.clear();
        arEntries.reserve(acSource.size());
        for (const auto& source : acSource) {
            const auto existing = std::find_if(arEntries.begin(), arEntries.end(),
                [&sameMetadata, &source](const InventoryEntry& acEntry) noexcept {
                    return sameMetadata(acEntry.Item, source);
                });
            if (existing == arEntries.end()) {
                arEntries.push_back({0, source});
                continue;
            }
            const auto count = static_cast<std::int64_t>(existing->Item.Count) + source.Count;
            if (count < std::numeric_limits<std::int32_t>::min() ||
                count > std::numeric_limits<std::int32_t>::max())
                return false;
            existing->Item.Count = static_cast<std::int32_t>(count);
            existing->Item.IsQuestItem = existing->Item.IsQuestItem || source.IsQuestItem;
        }
        std::erase_if(arEntries, [aReset](const InventoryEntry& acEntry) noexcept {
            return acEntry.Item.Count == 0 || (aReset && acEntry.Item.Count < 0);
        });
        return true;
    };
    std::vector<InventoryEntry> coalesced;
    if (!coalesce(acEntries, aReset, coalesced))
        return;
    if (aReset) {
        std::vector<InventoryEntry> replacement;
        replacement = std::move(coalesced);
        replacement.reserve(replacement.size() + baseline.Inventory.size());

        // Native reset transactions retain local quest stacks. Retain their
        // semantic baseline too so the next owned capture cannot echo them.
        for (const auto& previous : baseline.Inventory) {
            if (!previous.Item.IsQuestItem || previous.Item.Count <= 0)
                continue;
            const auto matching = std::find_if(replacement.begin(), replacement.end(),
                [&sameMetadata, &previous](const InventoryEntry& acEntry) {
                    return sameMetadata(previous.Item, acEntry.Item);
                });
            if (matching == replacement.end()) {
                replacement.push_back(previous);
                continue;
            }
            matching->Item.Count = std::max(matching->Item.Count, previous.Item.Count);
            matching->Item.IsQuestItem = true;
        }
        baseline.Inventory = std::move(replacement);
    } else {
        for (const auto& mutationEntry : coalesced) {
            const auto& mutation = mutationEntry.Item;
            const auto existing = std::find_if(baseline.Inventory.begin(), baseline.Inventory.end(),
                [&sameMetadata, &mutation](const InventoryEntry& acEntry) {
                    return sameMetadata(acEntry.Item, mutation);
                });
            if (existing == baseline.Inventory.end()) {
                if (mutation.Count > 0)
                    baseline.Inventory.push_back({0, mutation});
                continue;
            }
            const auto count = static_cast<std::int64_t>(existing->Item.Count) + mutation.Count;
            if (count <= 0)
                baseline.Inventory.erase(existing);
            else if (count <= std::numeric_limits<std::int32_t>::max())
                existing->Item.Count = static_cast<std::int32_t>(count);
        }
    }
    owned->second.Baseline = std::move(baseline);
}
catch (...)
{
    const auto server = m_serverToReference.find(aServerId);
    if (server != m_serverToReference.end()) {
        const auto owned = m_ownedByReference.find(server->second);
        if (owned != m_ownedByReference.end())
            owned->second.HasBaseline = false;
    }
    spdlog::error("VR NPC remote inventory baseline update failed; invalidated the owned baseline");
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

void VRNpcOwnershipService::OnUpdate(const UpdateEvent& acEvent) noexcept try
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
catch (...)
{
    spdlog::error("VR NPC ownership update failed; rebasing the gameplay epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
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

void VRNpcOwnershipService::OnGameplayResult(
    const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept try
{
    const auto& record = acEvent.Record;
    if (record.Header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::RemoteGameplayActionState))
        return;

    auto* pending = FindPendingObservation(record.Header.Identity.ActionId);
    if (!pending)
        return;

    const auto expected = *pending;
    const auto& result = record.Payload.RemoteGameplayActionState;
    const bool matches = record.Header.PayloadSize == GameplayBridge::kFixedPayloadBytes &&
                         record.Header.Flags == 0 && SameIdentity(record.Header.Identity, expected.Identity) &&
                         result.TargetHandle.Value == 0 &&
                         result.Domain == static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::NpcOwnership) &&
                         result.Action == static_cast<std::uint16_t>(expected.Action) &&
                         result.TargetLocalFormId == expected.ReferenceFormId &&
                         IsZero(result.Reserved, sizeof(result.Reserved));
    if (!matches) {
        RequestObservationLifecycleRetirement();
        return;
    }

    if (result.Status > static_cast<std::uint32_t>(GameplayBridge::CommandStatus::QueueOverflow)) {
        RequestObservationLifecycleRetirement();
        return;
    }
    const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
    if (status != GameplayBridge::CommandStatus::Success) {
        if (expected.Action == GameplayBridge::GameplayAction::StartNpcObservation)
            *pending = {};
        else
            RequestObservationLifecycleRetirement();
        return;
    }

    if (expected.Action == GameplayBridge::GameplayAction::StopNpcObservation) {
        ClearPendingObservationsForReference(expected.ReferenceFormId);
        m_observedReferences.erase(expected.ReferenceFormId);
        return;
    }

    const auto reference = m_referenceToServer.find(expected.ReferenceFormId);
    const auto server = m_serverToReference.find(expected.ServerId);
    if (reference == m_referenceToServer.end() || server == m_serverToReference.end() ||
        reference->second != expected.ServerId || server->second != expected.ReferenceFormId ||
        HasPendingObservation(expected.ReferenceFormId, GameplayBridge::GameplayAction::StopNpcObservation) ||
        m_observationLifecycleRetirementRequested) {
        if (!HasPendingObservation(expected.ReferenceFormId, GameplayBridge::GameplayAction::StopNpcObservation))
            RequestObservationLifecycleRetirement();
        return;
    }
    if (!m_observedReferences.contains(expected.ReferenceFormId) &&
        m_observedReferences.size() >= kMaximumTrackedNpcs) {
        RequestObservationLifecycleRetirement();
        return;
    }
    m_observedReferences.insert(expected.ReferenceFormId);
    *pending = {};
}
catch (...)
{
    spdlog::error("VR NPC observation result handling failed; rebasing the gameplay epoch");
    RequestObservationLifecycleRetirement();
}

bool VRNpcOwnershipService::IsBridgeReady() const noexcept
{
    return m_connected && m_transport.IsOnline() && !m_transport.IsGameplayCleanupRequired() &&
           m_transport.GetServerInstanceNonce() != 0 && m_transport.GetConnectionGeneration() != 0 &&
           SkyrimTogetherVR::GameplayBridgeClient::IsReady() &&
           SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() != 0 &&
           GameplayBridge::HasCapability(SkyrimTogetherVR::GameplayBridgeClient::GetActiveCapabilities(), GameplayBridge::Capability::NpcOwnership) &&
           GameplayBridge::HasCapability(SkyrimTogetherVR::GameplayBridgeClient::GetActiveCapabilities(), GameplayBridge::Capability::InventoryStackTransactions) &&
           (m_transport.GetNegotiatedGameplayCapabilities() &
                SkyrimTogether::Protocol::kVRNpcOwnershipCapabilities) ==
               SkyrimTogether::Protocol::kVRNpcOwnershipCapabilities;
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
    const auto kind = static_cast<GameplayBridge::EventKind>(record.Header.Kind);
    return IsSessionCurrent() &&
           (kind == GameplayBridge::EventKind::LocalGameplayAction ||
            kind == GameplayBridge::EventKind::LocalActorActionGraphChunk) &&
           record.Header.PayloadSize == GameplayBridge::kFixedPayloadBytes && record.Header.Flags == 0 &&
           record.Header.Identity.ServerInstanceNonce == m_transport.GetServerInstanceNonce() &&
           record.Header.Identity.ConnectionGeneration == m_transport.GetConnectionGeneration() &&
           record.Header.Identity.LifecycleEpoch == SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() &&
           record.Header.Identity.EntityId == 0 && record.Header.Identity.EntityGeneration == 0 &&
           record.Header.Identity.Reserved0 == 0 && record.Header.Identity.SequenceId == 0 &&
           GameplayBridge::IsNpcSnapshotActionId(record.Header.Identity.ActionId);
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

    const auto kind = static_cast<GameplayBridge::EventKind>(record.Header.Kind);
    const auto referenceId = kind == GameplayBridge::EventKind::LocalActorActionGraphChunk ?
                                 record.Payload.LocalActorActionGraphChunk.ActorLocalFormId :
                                 record.Payload.LocalGameplayAction.TargetLocalFormId;
    try {
        if (kind == GameplayBridge::EventKind::LocalActorActionGraphChunk) {
            const auto& payload = record.Payload.LocalActorActionGraphChunk;
            if (payload.TargetHandle.Value != 0 || payload.ActorLocalFormId == 0 || payload.Reserved0 != 0 ||
                payload.SnapshotId != record.Header.Identity.ActionId ||
                payload.DescriptorVersion != AnimationGraphProtocol::kDescriptorVersion || payload.Reserved1 != 0 ||
                payload.ChunkFlags != AnimationGraphProtocol::FullSnapshot || !IsFinite(payload.Direction) ||
                !IsZero(payload.ReservedTail, sizeof(payload.ReservedTail)))
                return;

            const auto partialIt = m_partialSnapshots.find(payload.ActorLocalFormId);
            if (partialIt == m_partialSnapshots.end())
                return;
            auto& partial = partialIt->second;
            const auto discard = [this, referenceId] { m_partialSnapshots.erase(referenceId); };
            const auto type = static_cast<AnimationGraphProtocol::ValueType>(payload.ValueType);
            if (!partial.Begun || partial.ActionId != record.Header.Identity.ActionId ||
                !SameIdentity(partial.Identity, record.Header.Identity) ||
                !partial.ExpectsAnimationGraph ||
                partial.NextActorValueIndex != kActorValues.size() ||
                !AnimationGraphProtocol::IsValidChunk(type, payload.StartIndex, payload.ValueCount, payload.TotalCount) ||
                !AnimationGraphProtocol::AreChunkValuesValid(type, payload.ValueCount, payload.TotalCount, payload.Values)) {
                discard();
                return;
            }

            const auto accepted = AnimationGraphProtocol::AcceptChunk(
                partial.Data.Animation, payload.SnapshotId, type, payload.StartIndex, payload.ValueCount,
                payload.TotalCount, payload.Direction, payload.Values);
            if (accepted == AnimationGraphProtocol::ChunkAcceptResult::Malformed ||
                accepted == AnimationGraphProtocol::ChunkAcceptResult::Stale) {
                discard();
                return;
            }
            ++partial.NextGraphChunk;
            return;
        }

        if (kind != GameplayBridge::EventKind::LocalGameplayAction)
            return;
        const auto& payload = record.Payload.LocalGameplayAction;
        if (static_cast<GameplayBridge::GameplayDomain>(payload.Domain) != GameplayBridge::GameplayDomain::NpcOwnership ||
            payload.TargetHandle.Value != GameplayBridge::kLocalPlayerHandle.Value || payload.TargetLocalFormId == 0 ||
            payload.SecondaryHandle.Value != 0 || payload.Reserved0 != 0 ||
            !IsZero(payload.ReservedTail, sizeof(payload.ReservedTail)))
            return;

        const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
        PartialSnapshot* partial{};
        if (action != GameplayBridge::GameplayAction::NpcSnapshotBegin) {
            const auto partialIt = m_partialSnapshots.find(payload.TargetLocalFormId);
            if (partialIt == m_partialSnapshots.end())
                return;
            partial = &partialIt->second;
            if (!partial->Begun || partial->ActionId != record.Header.Identity.ActionId ||
                !SameIdentity(partial->Identity, record.Header.Identity))
                return;
        }
        switch (action) {
    case GameplayBridge::GameplayAction::NpcSnapshotBegin:
    {
        if (record.Header.Identity.ActionId <= m_lastCompletedSnapshotActionId)
            return;
        if (payload.LocalFormIdA == 0 || payload.LocalFormIdB == 0 || !IsFinite(payload.ScalarA) ||
            !IsFinite(payload.ScalarB) || !IsFinite(payload.ScalarC) || !IsFinite(payload.ScalarD) ||
            payload.ValueA < 0 || payload.ValueA > static_cast<std::int32_t>(kMaximumNpcItems) ||
            payload.ValueB < 0 || payload.ValueB > static_cast<std::int32_t>(kMaximumNpcFactions) ||
            (payload.ActionFlags & ~GameplayBridge::kNpcSnapshotKnownFlags) != 0)
            return;
        if (const auto existing = m_partialSnapshots.find(payload.TargetLocalFormId);
            existing != m_partialSnapshots.end()) {
            if (existing->second.ActionId == record.Header.Identity.ActionId) {
                m_partialSnapshots.erase(existing);
                return;
            }
            if (existing->second.ActionId > record.Header.Identity.ActionId)
                return;
            m_partialSnapshots.erase(existing);
        }
        if (std::any_of(m_partialSnapshots.begin(), m_partialSnapshots.end(), [&record](const auto& acEntry) noexcept {
                return acEntry.second.ActionId == record.Header.Identity.ActionId;
            }))
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
        newPartial.Identity = record.Header.Identity;
        newPartial.ExpectsAnimationGraph = (payload.ActionFlags & GameplayBridge::kNpcSnapshotHasAnimationGraph) != 0;
        newPartial.Data.HasAnimationGraph = newPartial.ExpectsAnimationGraph;
        newPartial.ExpectedInventoryCount = static_cast<std::uint16_t>(payload.ValueA);
        newPartial.ExpectedFactionCount = static_cast<std::uint16_t>(payload.ValueB);
        newPartial.Data.ReferenceFormId = payload.TargetLocalFormId;
        newPartial.Data.Position = {payload.ScalarA, payload.ScalarB, payload.ScalarC};
        newPartial.Data.ZRotation = payload.ScalarD;
        newPartial.Data.Dead = (payload.ActionFlags & GameplayBridge::kNpcSnapshotDead) != 0;
        newPartial.Data.WeaponDrawn = (payload.ActionFlags & GameplayBridge::kNpcSnapshotWeaponDrawn) != 0;
        newPartial.Data.IsDragon = (payload.ActionFlags & GameplayBridge::kNpcSnapshotIsDragon) != 0;
        newPartial.Data.IsMount = (payload.ActionFlags & GameplayBridge::kNpcSnapshotIsMount) != 0;
        newPartial.Data.IsPlayerSummon = (payload.ActionFlags & GameplayBridge::kNpcSnapshotIsPlayerSummon) != 0;
        // Keep raw local IDs in BaseId/BaseId.ModId until the complete record
        // is translated as one all-or-nothing snapshot.
        newPartial.Data.BaseId.BaseId = payload.LocalFormIdA;
        newPartial.Data.CellId.BaseId = payload.LocalFormIdB;
        newPartial.Data.WorldspaceId.BaseId = payload.LocalFormIdC;
        newPartial.Data.PackageId.BaseId = payload.LocalFormIdD;
        return;
    }
    case GameplayBridge::GameplayAction::NpcSnapshotAppearance:
    {
        const auto headPartCount = static_cast<std::uint8_t>(
            (payload.ActionFlags & GameplayBridge::kNpcSnapshotAppearanceHeadPartCountMask) >>
            GameplayBridge::kNpcSnapshotAppearanceHeadPartCountShift);
        const auto knownFlags = GameplayBridge::kNpcSnapshotAppearanceHasFaceData |
                                GameplayBridge::kNpcSnapshotAppearanceEssential |
                                GameplayBridge::kNpcSnapshotAppearanceHeadPartCountMask;
        if (partial->HasAppearance || partial->NextActorValueIndex != 0 || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdD == 0 || payload.LocalFormIdD > VRAppearance::kMaximumNameBytes ||
            payload.ValueA < 0 || payload.ValueA > 1 || payload.ValueB <= 0 ||
            payload.ValueB > std::numeric_limits<std::uint16_t>::max() || !IsFinite(payload.ScalarA) ||
            payload.ScalarA < 0.0F || payload.ScalarA > 100.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            (payload.ActionFlags & ~knownFlags) != 0 || headPartCount > VRAppearance::kMaximumHeadParts)
        {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        auto& appearance = partial->Data.Appearance;
        appearance = {};
        appearance.Sequence = 1;
        appearance.RaceId.BaseId = payload.LocalFormIdA;
        appearance.HairColorId.BaseId = payload.LocalFormIdB;
        appearance.FaceTextureId.BaseId = payload.LocalFormIdC;
        appearance.Sex = static_cast<std::uint8_t>(payload.ValueA);
        appearance.Weight = payload.ScalarA;
        appearance.Level = static_cast<std::uint16_t>(payload.ValueB);
        appearance.Essential = (payload.ActionFlags & GameplayBridge::kNpcSnapshotAppearanceEssential) != 0;
        appearance.HasFaceData =
            (payload.ActionFlags & GameplayBridge::kNpcSnapshotAppearanceHasFaceData) != 0;
        appearance.NameLength = static_cast<std::uint8_t>(payload.LocalFormIdD);
        partial->ExpectedAppearanceHeadPartCount = headPartCount;
        partial->ExpectedNameChunkCount = static_cast<std::uint8_t>(
            (appearance.NameLength + GameplayBridge::kNpcSnapshotNameChunkBytes - 1) /
            GameplayBridge::kNpcSnapshotNameChunkBytes);
        partial->HasAppearance = true;
        return;
    }
    case GameplayBridge::GameplayAction::NpcSnapshotHeadPart:
        if (!partial->HasAppearance || partial->NextActorValueIndex != 0 ||
            partial->Data.Appearance.HeadPartCount >= partial->ExpectedAppearanceHeadPartCount ||
            payload.LocalFormIdA == 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA < 0 ||
            payload.ValueA >= static_cast<std::int32_t>(VRAppearance::kMaximumHeadParts) ||
            payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0) {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        for (std::uint8_t index = 0; index < partial->Data.Appearance.HeadPartCount; ++index) {
            if (partial->Data.Appearance.HeadParts[index].Slot == static_cast<std::uint8_t>(payload.ValueA)) {
                m_partialSnapshots.erase(payload.TargetLocalFormId);
                return;
            }
        }
        partial->Data.Appearance.HeadParts[partial->Data.Appearance.HeadPartCount++] = {
            static_cast<std::uint8_t>(payload.ValueA), GameId{0, payload.LocalFormIdA}};
        return;
    case GameplayBridge::GameplayAction::NpcSnapshotFaceMorph:
        if (!partial->HasAppearance || !partial->Data.Appearance.HasFaceData ||
            partial->NextActorValueIndex != 0 || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA != partial->NextFaceMorphIndex || payload.ValueB != 0 ||
            !IsFinite(payload.ScalarA) || std::abs(payload.ScalarA) > VRAppearance::kMaximumFaceMorphMagnitude ||
            payload.ScalarB != 0.0F || payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            payload.ActionFlags != 0 || partial->NextFaceMorphIndex >= VRAppearance::kFaceMorphCount) {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        partial->Data.Appearance.FaceMorphs[partial->NextFaceMorphIndex++] = payload.ScalarA;
        return;
    case GameplayBridge::GameplayAction::NpcSnapshotFacePart:
        if (!partial->HasAppearance || !partial->Data.Appearance.HasFaceData ||
            partial->NextActorValueIndex != 0 || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA != partial->NextFacePartIndex ||
            (payload.ValueB != VRAppearance::kFacePartDefault &&
             (payload.ValueB < 0 || payload.ValueB > VRAppearance::kMaximumFacePartPreset)) ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0 ||
            partial->NextFacePartIndex >= VRAppearance::kFacePartCount) {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        partial->Data.Appearance.FaceParts[partial->NextFacePartIndex++] = payload.ValueB;
        return;
    case GameplayBridge::GameplayAction::NpcSnapshotNameChunk:
    {
        const auto byteCount = static_cast<std::uint8_t>(
            payload.ActionFlags & GameplayBridge::kNpcSnapshotNameChunkByteCountMask);
        const auto chunkIndex = static_cast<std::uint8_t>(
            (payload.ActionFlags & GameplayBridge::kNpcSnapshotNameChunkIndexMask) >>
            GameplayBridge::kNpcSnapshotNameChunkIndexShift);
        const auto knownFlags = GameplayBridge::kNpcSnapshotNameChunkByteCountMask |
                                GameplayBridge::kNpcSnapshotNameChunkIndexMask;
        const auto& appearance = partial->Data.Appearance;
        const auto offset = static_cast<std::size_t>(chunkIndex) * GameplayBridge::kNpcSnapshotNameChunkBytes;
        const auto expectedBytes = offset < appearance.NameLength ?
            std::min<std::size_t>(GameplayBridge::kNpcSnapshotNameChunkBytes, appearance.NameLength - offset) : 0;
        if (!partial->HasAppearance || partial->NextActorValueIndex != 0 ||
            partial->Data.Appearance.HeadPartCount != partial->ExpectedAppearanceHeadPartCount ||
            (appearance.HasFaceData &&
             (partial->NextFaceMorphIndex != VRAppearance::kFaceMorphCount ||
              partial->NextFacePartIndex != VRAppearance::kFacePartCount)) ||
            chunkIndex != partial->NextNameChunkIndex || chunkIndex >= partial->ExpectedNameChunkCount ||
            byteCount != expectedBytes || (payload.ActionFlags & ~knownFlags) != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F) {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        const std::array<std::uint32_t, 6> words{
            payload.LocalFormIdA, payload.LocalFormIdB, payload.LocalFormIdC, payload.LocalFormIdD,
            static_cast<std::uint32_t>(payload.ValueA), static_cast<std::uint32_t>(payload.ValueB)};
        std::array<std::uint8_t, GameplayBridge::kNpcSnapshotNameChunkBytes> bytes{};
        std::memcpy(bytes.data(), words.data(), bytes.size());
        if (!std::all_of(bytes.begin() + byteCount, bytes.end(), [](const std::uint8_t aByte) {
                return aByte == 0;
            })) {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        std::memcpy(partial->Data.Appearance.Name.data() + offset, words.data(), byteCount);
        ++partial->NextNameChunkIndex;
        return;
    }
    case GameplayBridge::GameplayAction::NpcSnapshotValue:
    {
        const auto index = ActorValueIndex(payload.LocalFormIdA);
        if (!partial->HasAppearance ||
            partial->Data.Appearance.HeadPartCount != partial->ExpectedAppearanceHeadPartCount ||
            partial->NextNameChunkIndex != partial->ExpectedNameChunkCount ||
            (partial->Data.Appearance.HasFaceData &&
             (partial->NextFaceMorphIndex != VRAppearance::kFaceMorphCount ||
              partial->NextFacePartIndex != VRAppearance::kFacePartCount)) ||
            index == kActorValues.size() || index != partial->NextActorValueIndex || partial->HasValue[index] ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA != 0 || payload.ValueB != 0 || !IsFinite(payload.ScalarA) || !IsFinite(payload.ScalarB) ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0) {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        partial->Data.Values[index] = payload.ScalarA;
        partial->Data.Maximums[index] = payload.ScalarB;
        partial->HasValue[index] = true;
        ++partial->NextActorValueIndex;
        return;
    }
    case GameplayBridge::GameplayAction::NpcSnapshotItem:
        if (partial->NextActorValueIndex != kActorValues.size() ||
            (partial->ExpectsAnimationGraph && !partial->Data.Animation.IsComplete()) ||
            partial->Data.Inventory.size() >= partial->ExpectedInventoryCount ||
            (partial->Data.Inventory.size() != 0 &&
             (!partial->HasInventoryExtra || partial->InventoryEffectsRemaining != 0)) ||
            payload.LocalFormIdA == 0 || payload.LocalFormIdB != partial->ExpectedInventoryCount ||
            payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueA <= 0 ||
            payload.ValueA > Inventory::Entry::kMaximumMutationCount ||
            payload.ValueB != static_cast<std::int32_t>(partial->Data.Inventory.size()) ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            (payload.ActionFlags & ~GameplayBridge::kInventoryTransactionItemKnownFlags) != 0) {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        {
            InventoryEntry entry{};
            entry.LocalFormId = payload.LocalFormIdA;
            entry.Item.Count = payload.ValueA;
            entry.Item.IsQuestItem = (payload.ActionFlags & GameplayBridge::kInventoryTransactionQuestItem) != 0;
            entry.Item.ExtraWorn = (payload.ActionFlags & GameplayBridge::kInventoryTransactionWorn) != 0;
            entry.Item.ExtraWornLeft = (payload.ActionFlags & GameplayBridge::kInventoryTransactionWornLeft) != 0;
            entry.Item.EquipmentFlags =
                ((payload.ActionFlags & GameplayBridge::kInventoryTransactionWeapon) != 0 ?
                     Inventory::Entry::kEquipmentWeapon : 0u) |
                ((payload.ActionFlags & GameplayBridge::kInventoryTransactionAmmo) != 0 ?
                     Inventory::Entry::kEquipmentAmmo : 0u);
            partial->Data.Inventory.push_back(std::move(entry));
            partial->OpenInventoryIndex = partial->Data.Inventory.size() - 1;
            partial->HasInventoryExtra = false;
        }
        return;
    case GameplayBridge::GameplayAction::NpcSnapshotItemExtra:
        if (partial->Data.Inventory.empty() ||
            (partial->ExpectsAnimationGraph && !partial->Data.Animation.IsComplete()) ||
            partial->HasInventoryExtra || partial->OpenInventoryIndex != partial->Data.Inventory.size() - 1 ||
            payload.LocalFormIdC > 5 || payload.LocalFormIdD > GameplayBridge::kMaximumInventoryTransactionEffects ||
            payload.ValueA < 0 || payload.ValueA > std::numeric_limits<std::uint16_t>::max() ||
            payload.ValueB < 0 || !IsFinite(payload.ScalarA) || !IsFinite(payload.ScalarB) ||
            payload.ScalarA < 0.0F || payload.ScalarA > Inventory::Entry::kMaximumMutationScalarMagnitude ||
            payload.ScalarB < 0.0F || payload.ScalarB > Inventory::Entry::kMaximumMutationScalarMagnitude ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F ||
            (payload.ActionFlags & ~GameplayBridge::kInventoryTransactionExtraKnownFlags) != 0 ||
            ((payload.LocalFormIdA == 0) &&
             (payload.ValueA != 0 || payload.LocalFormIdD != 0 || payload.ActionFlags != 0)) ||
            ((payload.LocalFormIdB == 0) != (payload.ValueB == 0)) ||
            partial->TotalInventoryEffects > GameplayBridge::kMaximumInventoryTransactionEffects - payload.LocalFormIdD) {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        {
            auto& entry = partial->Data.Inventory[partial->OpenInventoryIndex].Item;
            entry.ExtraEnchantId.BaseId = payload.LocalFormIdA;
            entry.ExtraPoisonId.BaseId = payload.LocalFormIdB;
            entry.ExtraSoulLevel = static_cast<std::int32_t>(payload.LocalFormIdC);
            entry.ExtraEnchantCharge = static_cast<std::uint16_t>(payload.ValueA);
            entry.ExtraPoisonCount = static_cast<std::uint32_t>(payload.ValueB);
            entry.ExtraCharge = payload.ScalarA;
            entry.ExtraHealth = payload.ScalarB;
            entry.ExtraEnchantRemoveUnequip =
                (payload.ActionFlags & GameplayBridge::kInventoryTransactionEnchantRemoveUnequip) != 0;
            entry.EnchantData.IsWeapon =
                (payload.ActionFlags & GameplayBridge::kInventoryTransactionEnchantIsWeapon) != 0;
            partial->InventoryEffectsRemaining = payload.LocalFormIdD;
            partial->TotalInventoryEffects += payload.LocalFormIdD;
            partial->HasInventoryExtra = true;
        }
        return;
    case GameplayBridge::GameplayAction::NpcSnapshotItemEffect:
        if (partial->Data.Inventory.empty() || !partial->HasInventoryExtra ||
            partial->InventoryEffectsRemaining == 0 || payload.LocalFormIdA == 0 ||
            payload.LocalFormIdB != partial->OpenInventoryIndex ||
            payload.LocalFormIdC != partial->Data.Inventory[partial->OpenInventoryIndex].Item.EnchantData.Effects.size() ||
            payload.LocalFormIdD != partial->Data.Inventory[partial->OpenInventoryIndex].Item.EnchantData.Effects.size() +
                partial->InventoryEffectsRemaining || payload.ValueA < 0 ||
            payload.ValueB < 0 || !IsFinite(payload.ScalarA) || !IsFinite(payload.ScalarB) ||
            std::abs(payload.ScalarA) > Inventory::Entry::kMaximumMutationScalarMagnitude ||
            payload.ScalarB < 0.0F || payload.ScalarB > Inventory::Entry::kMaximumMutationScalarMagnitude ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0) {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        partial->Data.Inventory[partial->OpenInventoryIndex].Item.EnchantData.Effects.push_back({
            payload.ScalarA, payload.ValueA, payload.ValueB, payload.ScalarB, {0, payload.LocalFormIdA}});
        --partial->InventoryEffectsRemaining;
        return;
    case GameplayBridge::GameplayAction::NpcSnapshotFaction:
        if (partial->NextActorValueIndex != kActorValues.size() ||
            (partial->ExpectsAnimationGraph && !partial->Data.Animation.IsComplete()) ||
            partial->Data.Inventory.size() != partial->ExpectedInventoryCount ||
            !partial->Data.Inventory.empty() &&
                (!partial->HasInventoryExtra || partial->InventoryEffectsRemaining != 0) ||
            partial->Data.FactionData.ExtraFactions.size() >= partial->ExpectedFactionCount ||
            payload.LocalFormIdA == 0 || payload.LocalFormIdA <= partial->LastFactionFormId ||
            payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 ||
            payload.ValueA < std::numeric_limits<std::int8_t>::min() ||
            payload.ValueA > std::numeric_limits<std::int8_t>::max() ||
            payload.ValueB != 0 || payload.ScalarA != 0.0F || payload.ScalarB != 0.0F ||
            payload.ScalarC != 0.0F || payload.ScalarD != 0.0F || payload.ActionFlags != 0 ||
            partial->Data.FactionData.ExtraFactions.size() >= kMaximumNpcFactions) {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        {
            Faction faction{};
            faction.Id.BaseId = payload.LocalFormIdA;
            faction.Rank = static_cast<std::int8_t>(payload.ValueA);
            partial->Data.FactionData.ExtraFactions.push_back(faction);
        }
        partial->LastFactionFormId = payload.LocalFormIdA;
        return;
    case GameplayBridge::GameplayAction::NpcSnapshotEnd:
    {
        if (!partial->HasAppearance ||
            partial->Data.Appearance.HeadPartCount != partial->ExpectedAppearanceHeadPartCount ||
            partial->NextNameChunkIndex != partial->ExpectedNameChunkCount ||
            (partial->Data.Appearance.HasFaceData &&
             (partial->NextFaceMorphIndex != VRAppearance::kFaceMorphCount ||
              partial->NextFacePartIndex != VRAppearance::kFacePartCount)) ||
            partial->NextActorValueIndex != kActorValues.size() ||
            (partial->ExpectsAnimationGraph && !partial->Data.Animation.IsComplete()) ||
            partial->Data.Inventory.size() != partial->ExpectedInventoryCount ||
            !partial->Data.Inventory.empty() &&
                (!partial->HasInventoryExtra || partial->InventoryEffectsRemaining != 0) ||
            partial->Data.FactionData.ExtraFactions.size() != partial->ExpectedFactionCount ||
            payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0 ||
            !std::all_of(partial->HasValue.begin(), partial->HasValue.end(), [](const bool aValue) { return aValue; }) ||
            !TranslateSnapshot(*partial)) {
            m_partialSnapshots.erase(payload.TargetLocalFormId);
            return;
        }
        auto snapshot = std::move(partial->Data);
        const auto actionId = partial->ActionId;
        m_partialSnapshots.erase(payload.TargetLocalFormId);
        HandleCompleteSnapshot(std::move(snapshot));
        if (IsSessionCurrent())
            m_lastCompletedSnapshotActionId = actionId;
        return;
    }
    default:
        m_partialSnapshots.erase(payload.TargetLocalFormId);
        return;
        }
    }
    catch (...) {
        if (referenceId != 0)
            m_partialSnapshots.erase(referenceId);
        spdlog::error("VR NPC snapshot assembly failed; discarded the partial snapshot and rebasing the gameplay epoch");
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
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
    GameId referenceId{};
    GameId baseId{};
    GameId cellId{};
    GameId worldspaceId{};
    GameId packageId{};
    if (!IsValidPosition(data.Position) || !IsFinite(data.ZRotation) ||
        (data.HasAnimationGraph &&
         (!data.Animation.IsComplete() || !IsFinite(data.Animation.Direction))) ||
        data.Inventory.size() > kMaximumNpcItems ||
        data.FactionData.ExtraFactions.size() > kMaximumNpcFactions ||
        !modSystem.GetServerModId(data.ReferenceFormId, referenceId) || !referenceId ||
        !modSystem.GetServerModId(rawBase, baseId) || !baseId ||
        !modSystem.GetServerModId(rawCell, cellId) || !cellId ||
        (rawWorldspace != 0 && (!modSystem.GetServerModId(rawWorldspace, worldspaceId) || !worldspaceId)) ||
        (rawPackage != 0 && (!modSystem.GetServerModId(rawPackage, packageId) || !packageId)))
        return false;

    std::array<GameId, kMaximumNpcFactions> factionIds{};
    std::size_t factionIndex{};
    for (const auto& faction : data.FactionData.ExtraFactions) {
        const auto localId = faction.Id.BaseId;
        if (!modSystem.GetServerModId(localId, factionIds[factionIndex]) || !factionIds[factionIndex])
            return false;
        ++factionIndex;
    }

    data.ReferenceId = referenceId;
    data.BaseId = baseId;
    data.CellId = cellId;
    data.WorldspaceId = worldspaceId;
    data.PackageId = packageId;
    for (auto& captured : data.Inventory) {
        auto& entry = captured.Item;
        const auto mapRequired = [&modSystem](const std::uint32_t aLocalId, GameId& arId) {
            return aLocalId != 0 && modSystem.GetServerModId(aLocalId, arId) && static_cast<bool>(arId);
        };
        const auto mapOptional = [&modSystem](const GameId& acLocalId, GameId& arId) {
            return !acLocalId || (modSystem.GetServerModId(acLocalId.BaseId, arId) && static_cast<bool>(arId));
        };
        GameId baseId{};
        GameId enchantId{};
        GameId poisonId{};
        if (!mapRequired(captured.LocalFormId, baseId) ||
            !mapOptional(entry.ExtraEnchantId, enchantId) ||
            !mapOptional(entry.ExtraPoisonId, poisonId))
            return false;
        entry.BaseId = baseId;
        entry.ExtraEnchantId = enchantId;
        entry.ExtraPoisonId = poisonId;
        for (auto& effect : entry.EnchantData.Effects) {
            GameId effectId{};
            if (!mapRequired(effect.EffectId.BaseId, effectId))
                return false;
            effect.EffectId = effectId;
        }
        if (!entry.IsValidMutation())
            return false;
    }
    factionIndex = 0;
    for (auto& faction : data.FactionData.ExtraFactions)
        faction.Id = factionIds[factionIndex++];

    auto& appearance = data.Appearance;
    GameId raceId{};
    GameId hairColorId{};
    GameId faceTextureId{};
    if (!appearance.RaceId || !modSystem.GetServerModId(appearance.RaceId.BaseId, raceId) || !raceId)
        return false;
    if ((appearance.HairColorId &&
         (!modSystem.GetServerModId(appearance.HairColorId.BaseId, hairColorId) || !hairColorId)) ||
        (appearance.FaceTextureId &&
         (!modSystem.GetServerModId(appearance.FaceTextureId.BaseId, faceTextureId) || !faceTextureId)))
        return false;
    std::array<GameId, VRAppearance::kMaximumHeadParts> headPartIds{};
    for (std::uint8_t index = 0; index < appearance.HeadPartCount; ++index) {
        if (!appearance.HeadParts[index].FormId ||
            !modSystem.GetServerModId(appearance.HeadParts[index].FormId.BaseId, headPartIds[index]) ||
            !headPartIds[index])
            return false;
    }
    appearance.RaceId = raceId;
    appearance.HairColorId = hairColorId;
    appearance.FaceTextureId = faceTextureId;
    for (std::uint8_t index = 0; index < appearance.HeadPartCount; ++index)
        appearance.HeadParts[index].FormId = headPartIds[index];
    return appearance.IsValid();
}

bool VRNpcOwnershipService::BuildActorData(const Snapshot& acSnapshot, ActorData& arActorData) const noexcept try
{
    ActorData actorData{};
    for (std::size_t index = 0; index < kActorValues.size(); ++index) {
        actorData.InitialActorValues.ActorValuesList.emplace(kActorValues[index], acSnapshot.Values[index]);
        actorData.InitialActorValues.ActorMaxValuesList.emplace(kActorValues[index], acSnapshot.Maximums[index]);
    }
    for (const auto& entry : acSnapshot.Inventory) {
        if (!entry.Item.IsValidMutation() || entry.Item.Count <= 0)
            return false;
        actorData.InitialInventory.Entries.push_back(entry.Item);
    }
    actorData.IsDead = acSnapshot.Dead;
    actorData.IsWeaponDrawn = acSnapshot.WeaponDrawn;
    arActorData = std::move(actorData);
    return true;
}
catch (...)
{
    spdlog::error("VR NPC actor-data construction failed; rebasing the gameplay epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
    return false;
}

void VRNpcOwnershipService::HandleCompleteSnapshot(Snapshot&& aSnapshot) noexcept
{
    const auto referenceId = aSnapshot.ReferenceFormId;
    try {
        if (const auto claim = m_claimAfterSnapshot.find(referenceId); claim != m_claimAfterSnapshot.end()) {
            if (!StoreLatestSnapshot(std::move(aSnapshot)))
                return;
            if (claim->second.ClaimSent || claim->second.RetryDelay > 0.0)
                return;
            const auto claimSent = RequestOwnershipClaim(claim->second.ServerId, claim->second.GrantToken,
                                                         m_latestSnapshots.at(referenceId));
            if (!IsSessionCurrent())
                return;
            if (claimSent)
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
    catch (...) {
        m_latestSnapshots.erase(referenceId);
        m_partialSnapshots.erase(referenceId);
        spdlog::error("VR NPC completed snapshot processing failed; discarded the snapshot and rebasing the gameplay epoch");
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
    }
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
        request.Cookie = SkyrimTogetherVR::AssignmentCookie::TakeNpc(m_nextAssignmentCookie);
    } while (m_pendingAssignments.contains(request.Cookie));
    request.ReferenceId = acSnapshot.ReferenceId;
    request.FormId = acSnapshot.BaseId;
    request.CellId = acSnapshot.CellId;
    request.WorldSpaceId = acSnapshot.WorldspaceId;
    request.Position = acSnapshot.Position;
    request.Rotation.x = 0.0F;
    request.Rotation.y = acSnapshot.ZRotation;
    request.IsDragon = acSnapshot.IsDragon;
    request.IsMount = acSnapshot.IsMount;
    request.IsPlayerSummon = acSnapshot.IsPlayerSummon;
    try {
        request.CurrentActorData = std::move(actorData);
        request.FactionsContent = acSnapshot.FactionData;
        request.HasVRAppearance = true;
        request.InitialVRAppearance = acSnapshot.Appearance;
        if (const auto* actorReplication = m_world.ctx().find<VRActorReplicationService>())
            TP_UNUSED(actorReplication->TryGetLatestLocalActorAction(acSnapshot.ReferenceFormId, request.LatestAction));
        m_pendingAssignments.emplace(
            request.Cookie, PendingAssignment{acSnapshot.ReferenceFormId, m_nextStateOrder, 0.0});
    }
    catch (...) {
        m_pendingAssignments.erase(request.Cookie);
        spdlog::error("VR NPC assignment construction failed; discarded the pending assignment and rebasing the gameplay epoch");
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
        return;
    }
    if (!m_transport.Send(request)) {
        m_pendingAssignments.erase(request.Cookie);
        return;
    }
    ++m_nextStateOrder;
}

void VRNpcOwnershipService::OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept
{
    if (!acMessage.IsDecodedValid || acMessage.PlayerId != 0)
        return;
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
    try {
        OwnedNpc owned{};
        owned.ServerId = acMessage.ServerId;
        owned.Baseline = snapshot->second;
        owned.HasBaseline = true;
        owned.LastRelayedPackageId = snapshot->second.PackageId;
        m_ownedByReference.insert_or_assign(referenceId, std::move(owned));
    }
    catch (...) {
        ClearReference(referenceId);
        spdlog::error("VR NPC assignment response failed; discarded the partial ownership mapping and rebasing the gameplay epoch");
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
        return;
    }
    m_latestSnapshots.erase(snapshot);
    StartObservation(referenceId);
}

void VRNpcOwnershipService::OnCharacterSpawn(const CharacterSpawnRequest& acMessage) noexcept
{
    if (!acMessage.IsDecodedValid || !IsSessionCurrent() || acMessage.ServerId == 0 || !acMessage.FormId)
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
    try {
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
            OwnedNpc owned{};
            owned.ServerId = acMessage.ServerId;
            owned.Baseline = snapshot->second;
            owned.HasBaseline = true;
            owned.LastRelayedPackageId = snapshot->second.PackageId;
            m_ownedByReference.insert_or_assign(reference->second, std::move(owned));
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
        if (!m_claimAfterSnapshot.contains(reference->second) &&
            m_claimAfterSnapshot.size() >= kMaximumTrackedNpcs) {
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
    catch (...) {
        const auto reference = m_serverToReference.find(acMessage.ServerId);
        if (reference != m_serverToReference.end())
            ClearReference(reference->second);
        else
            m_pendingGrantByServer.erase(acMessage.ServerId);
        spdlog::error("VR NPC ownership transfer failed; discarded the partial ownership state and rebasing the gameplay epoch");
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
    }
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
    ::RequestOwnershipClaim request{};
    request.ServerId = aServerId;
    request.GrantToken = aGrantToken;
    try {
        request.NewActorData = std::move(actorData);
    }
    catch (...) {
        spdlog::error("VR NPC ownership-claim construction failed; rebasing the gameplay epoch");
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
        return false;
    }
    return m_transport.Send(request);
}

bool VRNpcOwnershipService::StoreLatestSnapshot(Snapshot&& aSnapshot) noexcept
{
    const auto referenceId = aSnapshot.ReferenceFormId;
    if (referenceId == 0)
        return false;
    try {
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
    catch (...) {
        m_latestSnapshots.erase(referenceId);
        m_partialSnapshots.erase(referenceId);
        spdlog::error("VR NPC snapshot storage failed; discarded the partial snapshot and rebasing the gameplay epoch");
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
        return false;
    }
}

bool VRNpcOwnershipService::TrackServerReference(const std::uint32_t aServerId, const std::uint32_t aReferenceFormId) noexcept try
{
    if (!IsSessionCurrent() || aServerId == 0 || aReferenceFormId == 0)
        return false;
    if (const auto server = m_serverToReference.find(aServerId); server != m_serverToReference.end() &&
        server->second != aReferenceFormId) {
        ClearReference(server->second);
        spdlog::error("VR NPC server entity remapped to a different local reference; rebasing the gameplay epoch");
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
        return false;
    }
    if (const auto reference = m_referenceToServer.find(aReferenceFormId); reference != m_referenceToServer.end() &&
        reference->second != aServerId) {
        spdlog::error("VR NPC local reference remapped to a different server entity; rebasing the gameplay epoch");
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
        return false;
    }
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
    return IsSessionCurrent() && m_serverToReference.contains(aServerId) &&
           m_referenceToServer.contains(aReferenceFormId) &&
           m_serverToReference.at(aServerId) == aReferenceFormId &&
           m_referenceToServer.at(aReferenceFormId) == aServerId;
}
catch (...)
{
    ClearReference(aReferenceFormId);
    m_serverToReference.erase(aServerId);
    spdlog::error("VR NPC server-reference mapping failed; discarded the partial map pair and rebasing the gameplay epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
    return false;
}

void VRNpcOwnershipService::PromotePendingGrant(const std::uint32_t aServerId,
                                                const std::uint32_t aReferenceFormId) noexcept try
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
catch (...)
{
    ClearReference(aReferenceFormId);
    m_pendingGrantByServer.erase(aServerId);
    spdlog::error("VR NPC pending ownership grant failed; discarded the partial grant mapping and rebasing the gameplay epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

bool VRNpcOwnershipService::StartObservation(const std::uint32_t aReferenceFormId) noexcept
{
    if (!IsSessionCurrent() || aReferenceFormId == 0)
        return false;
    if (HasPendingObservation(aReferenceFormId, GameplayBridge::GameplayAction::StopNpcObservation))
        return false;
    if (m_observedReferences.contains(aReferenceFormId) ||
        HasPendingObservation(aReferenceFormId, GameplayBridge::GameplayAction::StartNpcObservation))
        return true;
    const auto reference = m_referenceToServer.find(aReferenceFormId);
    if (reference == m_referenceToServer.end() || reference->second == 0)
        return false;
    const auto server = m_serverToReference.find(reference->second);
    if (server == m_serverToReference.end() || server->second != aReferenceFormId)
        return false;
    if (!HasObservationCapacity(aReferenceFormId) || !HasPendingObservationSlot())
        return false;
    auto* avatars = m_world.ctx().find<VRAvatarService>();
    GameplayBridge::CommandRecord command{};
    if (!avatars || !avatars->BuildLocalNativeGameplayCommandForServerId(
                        reference->second,
                        aReferenceFormId,
                        GameplayBridge::GameplayDomain::NpcOwnership,
                        GameplayBridge::GameplayAction::StartNpcObservation,
                        command))
        return false;
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command))
        return false;
    if (!TrackPendingObservation(command)) {
        RequestObservationLifecycleRetirement();
        return false;
    }
    return true;
}

bool VRNpcOwnershipService::StopObservation(const std::uint32_t aReferenceFormId) noexcept
{
    if (aReferenceFormId == 0)
        return false;
    if (HasPendingObservation(aReferenceFormId, GameplayBridge::GameplayAction::StopNpcObservation))
        return true;
    if (!m_observedReferences.contains(aReferenceFormId) &&
        !HasPendingObservation(aReferenceFormId, GameplayBridge::GameplayAction::StartNpcObservation))
        return true;
    if (!IsSessionCurrent())
        return false;
    const auto reference = m_referenceToServer.find(aReferenceFormId);
    if (reference == m_referenceToServer.end() || reference->second == 0)
        return false;
    const auto server = m_serverToReference.find(reference->second);
    if (server == m_serverToReference.end() || server->second != aReferenceFormId)
        return false;
    if (!HasPendingObservationSlot())
        return false;
    auto* avatars = m_world.ctx().find<VRAvatarService>();
    GameplayBridge::CommandRecord command{};
    if (!avatars || !avatars->BuildLocalNativeGameplayCommandForServerId(
                        reference->second,
                        aReferenceFormId,
                        GameplayBridge::GameplayDomain::NpcOwnership,
                        GameplayBridge::GameplayAction::StopNpcObservation,
                        command))
        return false;
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command))
        return false;
    if (TrackPendingObservation(command))
        return true;
    RequestObservationLifecycleRetirement();
    return false;
}

VRNpcOwnershipService::PendingObservation* VRNpcOwnershipService::FindPendingObservation(
    const std::uint64_t aActionId) noexcept
{
    if (aActionId == 0)
        return nullptr;
    for (auto& pending : m_pendingObservations) {
        if (pending.Active && pending.Identity.ActionId == aActionId)
            return &pending;
    }
    return nullptr;
}

bool VRNpcOwnershipService::HasPendingObservation(
    const std::uint32_t aReferenceFormId, const GameplayBridge::GameplayAction aAction) const noexcept
{
    return std::any_of(m_pendingObservations.begin(), m_pendingObservations.end(),
                       [aReferenceFormId, aAction](const PendingObservation& acPending) noexcept {
                           return acPending.Active && acPending.ReferenceFormId == aReferenceFormId &&
                                  acPending.Action == aAction;
                       });
}

bool VRNpcOwnershipService::HasPendingObservationSlot() const noexcept
{
    return std::any_of(m_pendingObservations.begin(), m_pendingObservations.end(),
                       [](const PendingObservation& acPending) noexcept { return !acPending.Active; });
}

bool VRNpcOwnershipService::HasObservationCapacity(const std::uint32_t aReferenceFormId) const noexcept
{
    if (m_observedReferences.contains(aReferenceFormId) ||
        HasPendingObservation(aReferenceFormId, GameplayBridge::GameplayAction::StartNpcObservation))
        return true;

    std::size_t pendingStarts{};
    for (const auto& pending : m_pendingObservations) {
        if (!pending.Active || pending.Action != GameplayBridge::GameplayAction::StartNpcObservation ||
            m_observedReferences.contains(pending.ReferenceFormId))
            continue;
        ++pendingStarts;
    }
    return m_observedReferences.size() + pendingStarts < kMaximumTrackedNpcs;
}

bool VRNpcOwnershipService::TrackPendingObservation(const GameplayBridge::CommandRecord& acCommand) noexcept
{
    if (acCommand.Header.Kind != static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyGameplayAction) ||
        acCommand.Header.Identity.ActionId == 0 || FindPendingObservation(acCommand.Header.Identity.ActionId))
        return false;

    const auto& payload = acCommand.Payload.ApplyGameplayAction;
    const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
    if (static_cast<GameplayBridge::GameplayDomain>(payload.Domain) != GameplayBridge::GameplayDomain::NpcOwnership ||
        payload.TargetHandle.Value != 0 || payload.TargetLocalFormId == 0 ||
        (action != GameplayBridge::GameplayAction::StartNpcObservation &&
         action != GameplayBridge::GameplayAction::StopNpcObservation))
        return false;

    const auto reference = m_referenceToServer.find(payload.TargetLocalFormId);
    if (reference == m_referenceToServer.end() || reference->second == 0)
        return false;
    for (auto& pending : m_pendingObservations) {
        if (pending.Active)
            continue;
        pending = {acCommand.Header.Identity, payload.TargetLocalFormId, reference->second, action, 0.0, true};
        return true;
    }
    return false;
}

void VRNpcOwnershipService::ClearPendingObservationsForReference(
    const std::uint32_t aReferenceFormId) noexcept
{
    for (auto& pending : m_pendingObservations) {
        if (pending.Active && pending.ReferenceFormId == aReferenceFormId)
            pending = {};
    }
}

void VRNpcOwnershipService::ClearPendingObservations() noexcept
{
    for (auto& pending : m_pendingObservations)
        pending = {};
}

void VRNpcOwnershipService::RequestObservationLifecycleRetirement() noexcept
{
    if (m_observationLifecycleRetirementRequested || !IsSessionCurrent())
        return;
    m_observationLifecycleRetirementRequested = true;
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

void VRNpcOwnershipService::ReplicateOwnedSnapshot(OwnedNpc& arOwned, const Snapshot& acSnapshot) noexcept try
{
    if (!IsSessionCurrent() || arOwned.ServerId == 0)
        return;
    if (!arOwned.HasBaseline) {
        auto baseline = acSnapshot;
        arOwned.Baseline = std::move(baseline);
        arOwned.HasBaseline = true;
        arOwned.LastRelayedPackageId = acSnapshot.PackageId;
        return;
    }
    const auto& baseline = arOwned.Baseline;
    auto acceptedBaseline = baseline;
    if (baseline.CellId != acSnapshot.CellId || baseline.WorldspaceId != acSnapshot.WorldspaceId ||
        baseline.Position != acSnapshot.Position || baseline.ZRotation != acSnapshot.ZRotation ||
        (acSnapshot.HasAnimationGraph &&
         (!baseline.HasAnimationGraph || !SameAnimationSnapshot(baseline.Animation, acSnapshot.Animation)))) {
        ClientReferencesMoveRequest request{};
        request.Tick = m_world.GetTick();
        auto& movement = request.Updates[arOwned.ServerId].UpdatedMovement;
        movement.CellId = acSnapshot.CellId;
        movement.WorldSpaceId = acSnapshot.WorldspaceId;
        movement.Position = acSnapshot.Position;
        movement.Rotation.x = 0.0F;
        movement.Rotation.y = acSnapshot.ZRotation;
        if (acSnapshot.HasAnimationGraph) {
            movement.Direction = acSnapshot.Animation.Direction;
            auto& variables = movement.Variables;
            variables.Booleans.resize(acSnapshot.Animation.BooleanCount);
            variables.Floats.resize(acSnapshot.Animation.FloatCount);
            variables.Integers.resize(acSnapshot.Animation.IntegerCount);
            for (std::size_t index = 0; index < acSnapshot.Animation.BooleanCount; ++index)
                variables.Booleans[index] = acSnapshot.Animation.Booleans[index];
            for (std::size_t index = 0; index < acSnapshot.Animation.FloatCount; ++index)
                variables.Floats[index] = acSnapshot.Animation.Floats[index];
            for (std::size_t index = 0; index < acSnapshot.Animation.IntegerCount; ++index)
                variables.Integers[index] = std::bit_cast<std::uint32_t>(acSnapshot.Animation.Integers[index]);
        }
        if (m_transport.Send(request)) {
            acceptedBaseline.CellId = acSnapshot.CellId;
            acceptedBaseline.WorldspaceId = acSnapshot.WorldspaceId;
            acceptedBaseline.Position = acSnapshot.Position;
            acceptedBaseline.ZRotation = acSnapshot.ZRotation;
            if (acSnapshot.HasAnimationGraph) {
                acceptedBaseline.Animation = acSnapshot.Animation;
                acceptedBaseline.HasAnimationGraph = true;
            }
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
    const auto sameInventoryMetadata = [](const InventoryEntry& acLeft, const InventoryEntry& acRight) {
        return acLeft.Item.BaseId == acRight.Item.BaseId && acLeft.Item.IsExtraDataEquals(acRight.Item);
    };
    const auto applyAcceptedInventory = [&](const Inventory::Entry& acMutation) {
        const auto existing = std::find_if(acceptedBaseline.Inventory.begin(), acceptedBaseline.Inventory.end(),
            [&](const InventoryEntry& acEntry) {
                return acEntry.Item.BaseId == acMutation.BaseId &&
                       acEntry.Item.IsExtraDataEquals(acMutation);
            });
        if (existing == acceptedBaseline.Inventory.end()) {
            if (acMutation.Count > 0)
                acceptedBaseline.Inventory.push_back({0, acMutation});
            return;
        }
        const auto count = static_cast<std::int64_t>(existing->Item.Count) + acMutation.Count;
        if (count <= 0)
            acceptedBaseline.Inventory.erase(existing);
        else if (count <= std::numeric_limits<std::int32_t>::max())
            existing->Item.Count = static_cast<std::int32_t>(count);
    };
    const auto sendInventory = [&](Inventory::Entry aItem) {
        if (!aItem.IsValidMutation())
            return false;
        RequestInventoryChanges request{};
        request.ServerId = arOwned.ServerId;
        request.Item = std::move(aItem);
        if (!m_transport.Send(request))
            return false;
        applyAcceptedInventory(request.Item);
        return true;
    };
    std::vector<bool> baselineMatched(baseline.Inventory.size());
    std::vector<bool> currentMatched(acSnapshot.Inventory.size());
    for (std::size_t previousIndex = 0; previousIndex < baseline.Inventory.size(); ++previousIndex) {
        for (std::size_t currentIndex = 0; currentIndex < acSnapshot.Inventory.size(); ++currentIndex) {
            if (currentMatched[currentIndex] ||
                !sameInventoryMetadata(baseline.Inventory[previousIndex], acSnapshot.Inventory[currentIndex]))
                continue;
            baselineMatched[previousIndex] = true;
            currentMatched[currentIndex] = true;
            const auto delta = static_cast<std::int64_t>(acSnapshot.Inventory[currentIndex].Item.Count) -
                baseline.Inventory[previousIndex].Item.Count;
            if (delta != 0 && delta >= Inventory::Entry::kMaximumMutationCount * -1LL &&
                delta <= Inventory::Entry::kMaximumMutationCount) {
                auto item = acSnapshot.Inventory[currentIndex].Item;
                item.Count = static_cast<std::int32_t>(delta);
                static_cast<void>(sendInventory(std::move(item)));
            }
            break;
        }
    }
    for (std::size_t previousIndex = 0; previousIndex < baseline.Inventory.size(); ++previousIndex) {
        if (baselineMatched[previousIndex])
            continue;
        const auto current = std::find_if(acSnapshot.Inventory.begin(), acSnapshot.Inventory.end(),
            [&](const InventoryEntry& acEntry) {
                const auto index = static_cast<std::size_t>(std::addressof(acEntry) - acSnapshot.Inventory.data());
                return !currentMatched[index] && acEntry.Item.BaseId == baseline.Inventory[previousIndex].Item.BaseId;
            });
        if (current == acSnapshot.Inventory.end())
            continue;
        auto removal = baseline.Inventory[previousIndex].Item;
        removal.Count = -removal.Count;
        if (sendInventory(std::move(removal))) {
            baselineMatched[previousIndex] = true;
            currentMatched[static_cast<std::size_t>(current - acSnapshot.Inventory.begin())] =
                sendInventory(current->Item);
        }
    }
    for (std::size_t previousIndex = 0; previousIndex < baseline.Inventory.size(); ++previousIndex) {
        if (baselineMatched[previousIndex])
            continue;
        auto removal = baseline.Inventory[previousIndex].Item;
        removal.Count = -removal.Count;
        if (sendInventory(std::move(removal)))
            baselineMatched[previousIndex] = true;
    }
    for (std::size_t currentIndex = 0; currentIndex < acSnapshot.Inventory.size(); ++currentIndex) {
        if (!currentMatched[currentIndex] && sendInventory(acSnapshot.Inventory[currentIndex].Item))
            currentMatched[currentIndex] = true;
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
catch (...)
{
    arOwned.HasBaseline = false;
    arOwned.CandidatePackageId = {};
    arOwned.CandidatePackageObservations = 0;
    spdlog::error("VR NPC authoritative replication failed; invalidated the partial baseline and rebasing the gameplay epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
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
    const bool observationMayBeActive = m_observedReferences.contains(aReferenceFormId) ||
                                      HasPendingObservation(
                                          aReferenceFormId, GameplayBridge::GameplayAction::StartNpcObservation);
    if (observationMayBeActive && !StopObservation(aReferenceFormId))
        RequestObservationLifecycleRetirement();
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
        TP_UNUSED(referenceId);
        if (aSendTransfer && m_connected && m_transport.IsOnline() && !m_transport.IsGameplayCleanupRequired() &&
            m_transport.GetServerInstanceNonce() != 0 && m_transport.GetConnectionGeneration() != 0 && owned.ServerId != 0 &&
            owned.HasBaseline && owned.Baseline.CellId && IsValidPosition(owned.Baseline.Position) &&
            (m_transport.GetNegotiatedGameplayCapabilities() &
                SkyrimTogether::Protocol::kVRNpcOwnershipCapabilities) ==
                SkyrimTogether::Protocol::kVRNpcOwnershipCapabilities) {
            RequestOwnershipTransfer request{};
            request.ServerId = owned.ServerId;
            request.CellId = owned.Baseline.CellId;
            request.WorldSpaceId = owned.Baseline.WorldspaceId;
            request.Position = owned.Baseline.Position;
            m_transport.Send(request);
        }
    }
    for (const auto referenceId : m_observedReferences) {
        if (!StopObservation(referenceId))
            RequestObservationLifecycleRetirement();
    }
    for (const auto& pending : m_pendingObservations) {
        if (!pending.Active || pending.Action != GameplayBridge::GameplayAction::StartNpcObservation ||
            m_observedReferences.contains(pending.ReferenceFormId))
            continue;
        if (!StopObservation(pending.ReferenceFormId))
            RequestObservationLifecycleRetirement();
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
    m_observedReferences.clear();
    ClearPendingObservations();
    m_lastCompletedSnapshotActionId = 0;
    m_lastLocalProjectileSequence = 0;
    m_sessionServerInstanceNonce = 0;
    m_sessionConnectionGeneration = 0;
    m_bridgeLifecycleEpoch = 0;
    m_observationLifecycleRetirementRequested = false;
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
    for (auto& pending : m_pendingObservations) {
        if (!pending.Active)
            continue;
        pending.Age = std::min(kPendingObservationLifetime, pending.Age + aDelta);
        if (pending.Age < kPendingObservationLifetime)
            continue;
        RequestObservationLifecycleRetirement();
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
