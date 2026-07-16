#include <TiltedOnlinePCH.h>

#include <Services/VRActorReplicationService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/LocalGameplayBridgeEvent.h>
#include <Events/RemoteGameplayBridgeResultEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/ClientActorActionRequest.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyMount.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifyProjectileLaunch.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/NotifyRemoveSpell.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifySpawnData.h>
#include <Messages/NotifySpellCast.h>
#include <Messages/NotifyVRCombatHitEvent.h>
#include <Messages/NotifyVREquipmentUpdate.h>
#include <Messages/NotifyVRGrabEvent.h>
#include <Messages/NotifyVRHiggsState.h>
#include <Messages/NotifyVRAppearance.h>
#include <Messages/NotifyVRMagicEffectEvent.h>
#include <Messages/NotifyVRPoseUpdate.h>
#include <Messages/NotifyVRProjectileEvent.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Services/TransportService.h>
#include <Services/VRAvatarService.h>
#include <Services/VRNpcOwnershipService.h>
#include <Structs/GameplayCapabilities.h>
#include <World.h>
#include <VRGameplayBridge.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace GameplayBridge = SkyrimTogetherVR::GameplayBridge;

namespace
{
constexpr std::uint32_t kFlagBool0 = 1u << 0;
constexpr std::uint32_t kFlagBool1 = 1u << 1;
constexpr std::uint32_t kFlagBool2 = 1u << 2;
constexpr std::uint32_t kFlagBool3 = 1u << 3;
constexpr std::uint32_t kFlagLeftHand = 1u << 4;
constexpr std::uint32_t kFlagNodeShift = 8;
constexpr std::int32_t kCastingSourceOther = 2;
constexpr std::uint32_t kActorValueCount = 164;
constexpr std::uint32_t kHealthActorValue = 24;
constexpr std::uint32_t kDragonSoulsActorValue = 133;
constexpr std::size_t kMaximumPendingMagicEffects = 32;
constexpr std::uint8_t kMaximumPendingMagicEffectAttempts = 3;
constexpr std::uint32_t kRightHandEquipSlotFormId = 0x00013F42;
constexpr std::uint32_t kLeftHandEquipSlotFormId = 0x00013F43;
constexpr std::size_t kMaximumFactionEntries = 0x1FF;
constexpr std::size_t kMaximumActorActionTransactions = 64;
constexpr std::size_t kMaximumPendingRemoteActorActions = 64;
constexpr std::size_t kMaximumActorActionStringBytes = 127;
constexpr std::size_t kMaximumPendingEquipmentApplications = 128;
constexpr std::uint8_t kMaximumEquipmentResultFailures = 3;
constexpr double kEquipmentResultTimeoutSeconds = 2.0;
constexpr double kSpawnResultTimeoutSeconds = 2.0;

[[nodiscard]] bool IsNewer(const std::uint32_t aCandidate, const std::uint32_t aPrevious) noexcept
{
    return static_cast<std::int32_t>(aCandidate - aPrevious) > 0;
}

[[nodiscard]] bool IsFinite(const float aValue) noexcept
{
    return std::isfinite(aValue);
}

[[nodiscard]] bool IsFinite(const Vector3_NetQuantize& acValue) noexcept
{
    return IsFinite(acValue.x) && IsFinite(acValue.y) && IsFinite(acValue.z);
}

[[nodiscard]] std::uint64_t Mix(std::uint64_t aSeed, const std::uint64_t aValue) noexcept
{
    return (aSeed ^ (aValue + 0x9e3779b97f4a7c15ull + (aSeed << 6) + (aSeed >> 2)));
}

template <class... Ts> [[nodiscard]] std::uint64_t Signature(const Ts... aValues) noexcept
{
    std::uint64_t seed = 0xcbf29ce484222325ull;
    ((seed = Mix(seed, static_cast<std::uint64_t>(aValues))), ...);
    return seed;
}

[[nodiscard]] std::uint32_t FloatBits(const float aValue) noexcept
{
    return std::bit_cast<std::uint32_t>(aValue);
}

[[nodiscard]] std::uint64_t AppearanceSignature(const VRAppearance& acAppearance) noexcept
{
    std::uint64_t signature = Signature(acAppearance.RaceId.LogFormat(), acAppearance.Sex, FloatBits(acAppearance.Weight),
                                        acAppearance.Level, acAppearance.Essential, acAppearance.NameLength,
                                        acAppearance.HeadPartCount, acAppearance.TintCount);
    for (std::uint8_t index = 0; index < acAppearance.NameLength; ++index)
        signature = Mix(signature, static_cast<std::uint8_t>(acAppearance.Name[index]));
    for (std::uint8_t index = 0; index < acAppearance.HeadPartCount; ++index)
    {
        signature = Mix(signature, acAppearance.HeadParts[index].Slot);
        signature = Mix(signature, acAppearance.HeadParts[index].FormId.LogFormat());
    }
    for (std::uint8_t index = 0; index < acAppearance.TintCount; ++index)
    {
        signature = Mix(signature, acAppearance.Tints[index].Type);
        signature = Mix(signature, acAppearance.Tints[index].Color);
        signature = Mix(signature, FloatBits(acAppearance.Tints[index].Alpha));
    }
    return signature;
}

[[nodiscard]] GameplayBridge::GameplayActionPayload Payload() noexcept
{
    return {};
}

[[nodiscard]] std::uint32_t ToLocal(World& aWorld, const GameId& acId) noexcept
{
    return acId ? aWorld.GetModSystem().GetGameId(acId) : 0;
}

[[nodiscard]] bool IsRemotePlayer(const TransportService& acTransport, const std::uint32_t aPlayerId) noexcept
{
    return aPlayerId != 0 && aPlayerId != acTransport.GetLocalPlayerId();
}
} // namespace

VRActorReplicationService::VRActorReplicationService(
    World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, VRAvatarService& aAvatars,
    VRNpcOwnershipService& aNpcOwnership) noexcept
    : m_world(aWorld), m_transport(aTransport), m_avatars(aAvatars), m_npcOwnership(aNpcOwnership)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRActorReplicationService::OnUpdate>(this);
    m_characterSpawnConnection = aDispatcher.sink<CharacterSpawnRequest>().connect<&VRActorReplicationService::OnCharacterSpawn>(this);
    m_referencesMoveConnection = aDispatcher.sink<ServerReferencesMoveRequest>().connect<&VRActorReplicationService::OnReferencesMove>(this);
    m_drawWeaponConnection = aDispatcher.sink<NotifyDrawWeapon>().connect<&VRActorReplicationService::OnDrawWeapon>(this);
    m_equipmentConnection = aDispatcher.sink<NotifyEquipmentChanges>().connect<&VRActorReplicationService::OnEquipment>(this);
    m_factionsConnection = aDispatcher.sink<NotifyFactionsChanges>().connect<&VRActorReplicationService::OnFactionsChanges>(this);
    m_inventoryConnection = aDispatcher.sink<NotifyInventoryChanges>().connect<&VRActorReplicationService::OnInventory>(this);
    m_actorValuesConnection = aDispatcher.sink<NotifyActorValueChanges>().connect<&VRActorReplicationService::OnActorValues>(this);
    m_actorMaximumsConnection = aDispatcher.sink<NotifyActorMaxValueChanges>().connect<&VRActorReplicationService::OnActorMaximums>(this);
    m_healthChangeConnection = aDispatcher.sink<NotifyHealthChangeBroadcast>().connect<&VRActorReplicationService::OnHealthChangeBroadcast>(this);
    m_deathConnection = aDispatcher.sink<NotifyDeathStateChange>().connect<&VRActorReplicationService::OnDeath>(this);
    m_respawnConnection = aDispatcher.sink<NotifyRespawn>().connect<&VRActorReplicationService::OnRespawn>(this);
    m_mountConnection = aDispatcher.sink<NotifyMount>().connect<&VRActorReplicationService::OnMount>(this);
    m_projectileConnection = aDispatcher.sink<NotifyProjectileLaunch>().connect<&VRActorReplicationService::OnProjectile>(this);
    m_spawnDataConnection = aDispatcher.sink<NotifySpawnData>().connect<&VRActorReplicationService::OnSpawnData>(this);
    m_spellCastConnection = aDispatcher.sink<NotifySpellCast>().connect<&VRActorReplicationService::OnSpellCast>(this);
    m_interruptCastConnection = aDispatcher.sink<NotifyInterruptCast>().connect<&VRActorReplicationService::OnInterruptCast>(this);
    m_removeSpellConnection = aDispatcher.sink<NotifyRemoveSpell>().connect<&VRActorReplicationService::OnNotifyRemoveSpell>(this);
    m_removeCharacterConnection = aDispatcher.sink<NotifyRemoveCharacter>().connect<&VRActorReplicationService::OnRemoveCharacter>(this);
    m_addTargetConnection = aDispatcher.sink<NotifyAddTarget>().connect<&VRActorReplicationService::OnNotifyAddTarget>(this);
    m_vrEquipmentConnection = aDispatcher.sink<NotifyVREquipmentUpdate>().connect<&VRActorReplicationService::OnVrEquipment>(this);
    m_vrCombatConnection = aDispatcher.sink<NotifyVRCombatHitEvent>().connect<&VRActorReplicationService::OnVrCombat>(this);
    m_vrMagicConnection = aDispatcher.sink<NotifyVRMagicEffectEvent>().connect<&VRActorReplicationService::OnVrMagic>(this);
    m_vrProjectileConnection = aDispatcher.sink<NotifyVRProjectileEvent>().connect<&VRActorReplicationService::OnVrProjectile>(this);
    m_vrPoseConnection = aDispatcher.sink<NotifyVRPoseUpdate>().connect<&VRActorReplicationService::OnVrPose>(this);
    m_vrHiggsConnection = aDispatcher.sink<NotifyVRHiggsState>().connect<&VRActorReplicationService::OnVrHiggs>(this);
    m_vrAppearanceConnection = aDispatcher.sink<NotifyVRAppearance>().connect<&VRActorReplicationService::OnVrAppearance>(this);
    m_vrGrabConnection = aDispatcher.sink<NotifyVRGrabEvent>().connect<&VRActorReplicationService::OnVrGrab>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&VRActorReplicationService::OnPlayerLeft>(this);
    m_playerLevelConnection = aDispatcher.sink<NotifyPlayerLevel>().connect<&VRActorReplicationService::OnPlayerLevel>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRActorReplicationService::OnDisconnected>(this);
    m_gameplayResultConnection = aDispatcher.sink<SkyrimTogetherVR::RemoteGameplayBridgeResultEvent>()
        .connect<&VRActorReplicationService::OnGameplayResult>(this);
    m_localGameplayConnection = aDispatcher.sink<SkyrimTogetherVR::LocalGameplayBridgeEvent>()
        .connect<&VRActorReplicationService::OnLocalGameplay>(this);
}

bool VRActorReplicationService::Accept(const std::uint32_t aPlayerId, const GameplayBridge::GameplayDomain aDomain,
                                       const std::uint32_t aSequence, const std::uint64_t aSignature,
                                       const std::uint8_t aChannel) noexcept
{
    if (aPlayerId == 0)
        return false;
    constexpr std::size_t kDomainStride = 18;
    const auto domainIndex = static_cast<std::size_t>(aDomain);
    if (domainIndex == 0 || domainIndex >= kDomainStride || aChannel > 1)
        return false;
    const auto index = domainIndex + static_cast<std::size_t>(aChannel) * kDomainStride;

    auto& ledger = m_ledgers[aPlayerId][index];
    if (aSequence != 0)
    {
        if (ledger.HasSequence && !IsNewer(aSequence, ledger.LastSequence))
            return false;
        ledger.LastSequence = aSequence;
        ledger.HasSequence = true;
    }
    else if (ledger.HasSignature && ledger.LastSignature == aSignature)
        return false;

    ledger.LastSignature = aSignature;
    ledger.HasSignature = true;
    return true;
}

bool VRActorReplicationService::ApplyForPlayer(const std::uint32_t aPlayerId, const GameplayBridge::GameplayDomain aDomain,
                                                const GameplayBridge::GameplayAction aAction,
                                                const GameplayBridge::GameplayActionPayload& acPayload) noexcept
{
    if (!IsRemotePlayer(m_transport, aPlayerId))
        return false;
    GameplayBridge::CommandRecord command{};
    if (!m_avatars.BuildRemoteGameplayCommand(aPlayerId, aDomain, aAction, command))
        return false;
    const auto target = command.Payload.ApplyGameplayAction.TargetHandle;
    command.Payload.ApplyGameplayAction = acPayload;
    command.Payload.ApplyGameplayAction.TargetHandle = target;
    command.Payload.ApplyGameplayAction.Domain = static_cast<std::uint16_t>(aDomain);
    command.Payload.ApplyGameplayAction.Action = static_cast<std::uint16_t>(aAction);
    return SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command);
}

bool VRActorReplicationService::ApplyForServer(const std::uint32_t aServerId, const GameplayBridge::GameplayDomain aDomain,
                                                const GameplayBridge::GameplayAction aAction,
                                                const GameplayBridge::GameplayActionPayload& acPayload) noexcept
{
    GameplayBridge::CommandRecord command{};
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(aServerId, aDomain, aAction, command))
        return false;
    const auto target = command.Payload.ApplyGameplayAction.TargetHandle;
    command.Payload.ApplyGameplayAction = acPayload;
    command.Payload.ApplyGameplayAction.TargetHandle = target;
    command.Payload.ApplyGameplayAction.Domain = static_cast<std::uint16_t>(aDomain);
    command.Payload.ApplyGameplayAction.Action = static_cast<std::uint16_t>(aAction);
    const auto submitted = SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command);
    if (submitted && m_recordingSpawnServerId == aServerId && command.Header.Identity.ActionId != 0)
        m_spawnActionOwners[command.Header.Identity.ActionId] = SpawnActionTracking{aServerId, 1};
    return submitted;
}

bool VRActorReplicationService::ApplyForTarget(const std::uint32_t aServerId, const GameplayBridge::GameplayDomain aDomain,
                                                const GameplayBridge::GameplayAction aAction,
                                                const GameplayBridge::GameplayActionPayload& acPayload) noexcept
{
    if (aServerId == 0)
        return false;

    GameplayBridge::CommandRecord command{};
    const bool built = aServerId == m_localServerId ?
        m_avatars.BuildLocalGameplayCommand(aDomain, aAction, command) :
        m_avatars.BuildRemoteGameplayCommandForServerId(aServerId, aDomain, aAction, command);
    if (!built)
        return false;

    const auto target = command.Payload.ApplyGameplayAction.TargetHandle;
    command.Payload.ApplyGameplayAction = acPayload;
    command.Payload.ApplyGameplayAction.TargetHandle = target;
    command.Payload.ApplyGameplayAction.Domain = static_cast<std::uint16_t>(aDomain);
    command.Payload.ApplyGameplayAction.Action = static_cast<std::uint16_t>(aAction);
    return SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command);
}

bool VRActorReplicationService::ApplyTextForPlayer(
    const std::uint32_t aPlayerId, const GameplayBridge::GameplayDomain aDomain,
    const GameplayBridge::GameplayAction aAction, const std::uint64_t aTextId,
    const std::string_view acText) noexcept
{
    const auto maximumBytes = static_cast<std::size_t>(GameplayBridge::kGameplayTextBytesPerChunk) *
                              GameplayBridge::kMaximumGameplayTextChunks;
    if (aTextId == 0 || acText.empty() || acText.size() > maximumBytes)
        return false;
    GameplayBridge::CommandRecord base{};
    if (!m_avatars.BuildRemoteGameplayCommand(aPlayerId, aDomain, aAction, base))
        return false;
    const auto target = base.Payload.ApplyGameplayAction.TargetHandle;
    const auto chunkCount = static_cast<std::uint16_t>(
        (acText.size() + GameplayBridge::kGameplayTextBytesPerChunk - 1) /
        GameplayBridge::kGameplayTextBytesPerChunk);
    std::vector<GameplayBridge::CommandRecord> commands(chunkCount);
    for (std::uint16_t index = 0; index < chunkCount; ++index) {
        auto& command = commands[index];
        command.Header = base.Header;
        command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyGameplayTextChunk);
        auto& payload = command.Payload.ApplyGameplayTextChunk;
        payload.TargetHandle = target;
        payload.Domain = static_cast<std::uint16_t>(aDomain);
        payload.Action = static_cast<std::uint16_t>(aAction);
        payload.TextId = aTextId;
        payload.ChunkIndex = index;
        payload.ChunkCount = chunkCount;
        const auto offset = static_cast<std::size_t>(index) * GameplayBridge::kGameplayTextBytesPerChunk;
        payload.ByteCount = static_cast<std::uint16_t>(std::min<std::size_t>(
            GameplayBridge::kGameplayTextBytesPerChunk, acText.size() - offset));
        std::memcpy(payload.Utf8Bytes, acText.data() + offset, payload.ByteCount);
    }
    return SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommandBatch(commands.data(), commands.size());
}

bool VRActorReplicationService::ApplyTextForServer(
    const std::uint32_t aServerId, const GameplayBridge::GameplayDomain aDomain,
    const GameplayBridge::GameplayAction aAction, const std::uint64_t aTextId,
    const std::string_view acText) noexcept
{
    const auto maximumBytes = static_cast<std::size_t>(GameplayBridge::kGameplayTextBytesPerChunk) *
                              GameplayBridge::kMaximumGameplayTextChunks;
    if (aTextId == 0 || acText.empty() || acText.size() > maximumBytes)
        return false;
    GameplayBridge::CommandRecord base{};
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(aServerId, aDomain, aAction, base))
        return false;
    const auto target = base.Payload.ApplyGameplayAction.TargetHandle;
    const auto chunkCount = static_cast<std::uint16_t>(
        (acText.size() + GameplayBridge::kGameplayTextBytesPerChunk - 1) /
        GameplayBridge::kGameplayTextBytesPerChunk);
    std::vector<GameplayBridge::CommandRecord> commands(chunkCount);
    for (std::uint16_t index = 0; index < chunkCount; ++index) {
        auto& command = commands[index];
        command.Header = base.Header;
        command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyGameplayTextChunk);
        auto& payload = command.Payload.ApplyGameplayTextChunk;
        payload.TargetHandle = target;
        payload.Domain = static_cast<std::uint16_t>(aDomain);
        payload.Action = static_cast<std::uint16_t>(aAction);
        payload.TextId = aTextId;
        payload.ChunkIndex = index;
        payload.ChunkCount = chunkCount;
        const auto offset = static_cast<std::size_t>(index) * GameplayBridge::kGameplayTextBytesPerChunk;
        payload.ByteCount = static_cast<std::uint16_t>(std::min<std::size_t>(
            GameplayBridge::kGameplayTextBytesPerChunk, acText.size() - offset));
        std::memcpy(payload.Utf8Bytes, acText.data() + offset, payload.ByteCount);
    }
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommandBatch(commands.data(), commands.size()))
        return false;
    if (m_recordingSpawnServerId == aServerId && !commands.empty()) {
        const auto actionId = commands.front().Header.Identity.ActionId;
        auto& tracking = m_spawnActionOwners[actionId];
        tracking.ServerId = aServerId;
        tracking.RemainingResults += static_cast<std::uint16_t>(commands.size());
    }
    return true;
}

std::uint32_t VRActorReplicationService::PlayerForServer(const std::uint32_t aServerId) const noexcept
{
    const auto it = m_serverPlayers.find(aServerId);
    return it != m_serverPlayers.end() ? it->second : 0;
}

bool VRActorReplicationService::HasExactActorActionCapability() const noexcept
{
    return m_transport.IsOnline() && !m_transport.IsGameplayCleanupRequired() &&
           SkyrimTogether::Protocol::HasCapability(
               m_transport.GetNegotiatedGameplayCapabilities(),
               SkyrimTogether::Protocol::GameplayCapability::ExactAnimationActions) &&
           SkyrimTogetherVR::GameplayBridgeClient::IsReady() &&
           GameplayBridge::HasCapability(SkyrimTogetherVR::GameplayBridgeClient::GetActiveCapabilities(),
                                         GameplayBridge::Capability::ExactAnimationActions);
}

bool VRActorReplicationService::IsCurrentActorActionRecord(
    const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) const noexcept
{
    const auto& record = acEvent.Record;
    const auto& header = record.Header;
    if (!HasExactActorActionCapability() || header.PayloadSize != GameplayBridge::kFixedPayloadBytes ||
        header.Flags != 0 || header.Identity.ServerInstanceNonce != m_transport.GetServerInstanceNonce() ||
        header.Identity.ConnectionGeneration != m_transport.GetConnectionGeneration() ||
        header.Identity.LifecycleEpoch != SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() ||
        header.Identity.EntityId != 0 || header.Identity.EntityGeneration != 0 || header.Identity.Reserved0 != 0 ||
        header.Identity.SequenceId != 0 || header.Identity.ActionId == 0)
        return false;

    switch (static_cast<GameplayBridge::EventKind>(header.Kind))
    {
    case GameplayBridge::EventKind::LocalActorActionMetadata:
    {
        const auto& payload = record.Payload.LocalActorActionMetadata;
        return (payload.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value || payload.TargetHandle.Value == 0) &&
               payload.ActorLocalFormId != 0 && payload.ActionLocalFormId != 0 &&
               payload.SnapshotId == header.Identity.ActionId && payload.TextId == header.Identity.ActionId &&
               payload.ActionFlags == 0 && (payload.Type & ~0x7u) == 0 &&
               std::all_of(std::begin(payload.Reserved), std::end(payload.Reserved),
                           [](const std::uint8_t aValue) noexcept { return aValue == 0; });
    }
    case GameplayBridge::EventKind::LocalActorActionGraphChunk:
    {
        const auto& payload = record.Payload.LocalActorActionGraphChunk;
        const auto type = static_cast<SkyrimTogetherVR::AnimationGraphProtocol::ValueType>(payload.ValueType);
        return (payload.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value || payload.TargetHandle.Value == 0) &&
               payload.ActorLocalFormId != 0 && payload.Reserved0 == 0 &&
               payload.SnapshotId == header.Identity.ActionId &&
               payload.DescriptorVersion == SkyrimTogetherVR::AnimationGraphProtocol::kDescriptorVersion &&
               payload.Reserved1 == 0 &&
               payload.ChunkFlags == SkyrimTogetherVR::AnimationGraphProtocol::FullSnapshot && IsFinite(payload.Direction) &&
               SkyrimTogetherVR::AnimationGraphProtocol::IsValidChunk(type, payload.StartIndex, payload.ValueCount,
                                                                        payload.TotalCount) &&
               SkyrimTogetherVR::AnimationGraphProtocol::AreChunkValuesValid(type, payload.ValueCount, payload.Values) &&
               std::all_of(std::begin(payload.ReservedTail), std::end(payload.ReservedTail),
                           [](const std::uint8_t aValue) noexcept { return aValue == 0; });
    }
    case GameplayBridge::EventKind::LocalActorActionTextChunk:
    {
        const auto& payload = record.Payload.LocalActorActionTextChunk;
        return (payload.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value || payload.TargetHandle.Value == 0) &&
               payload.TargetLocalFormId != 0 &&
               payload.Domain == static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Animation) &&
               payload.Action == static_cast<std::uint16_t>(GameplayBridge::GameplayAction::ActorAction) &&
               payload.TextId == header.Identity.ActionId && payload.ChunkCount != 0 &&
               payload.ChunkCount <= GameplayBridge::kMaximumGameplayTextChunks && payload.ChunkIndex < payload.ChunkCount &&
               payload.ByteCount <= GameplayBridge::kGameplayTextBytesPerChunk && payload.Reserved0 == 0 &&
               payload.AuxiliaryLocalFormId == 0 &&
               std::all_of(payload.Utf8Bytes + payload.ByteCount,
                           payload.Utf8Bytes + GameplayBridge::kGameplayTextBytesPerChunk,
                           [](const char aValue) noexcept { return aValue == '\0'; });
    }
    default:
        return false;
    }
}

VRActorReplicationService::LocalActorActionTransaction&
VRActorReplicationService::GetOrCreateLocalActorAction(const std::uint64_t aActionId) noexcept
{
    if (const auto existing = m_localActorActions.find(aActionId); existing != m_localActorActions.end())
        return existing->second;

    if (m_localActorActions.size() >= kMaximumActorActionTransactions) {
        const auto oldest = std::min_element(m_localActorActions.begin(), m_localActorActions.end(),
            [](const auto& acLeft, const auto& acRight) noexcept {
                return acLeft.second.Order != acRight.second.Order ? acLeft.second.Order < acRight.second.Order :
                       acLeft.first < acRight.first;
            });
        if (oldest != m_localActorActions.end())
            m_localActorActions.erase(oldest);
    }

    auto [created, inserted] = m_localActorActions.emplace(aActionId, LocalActorActionTransaction{});
    TP_UNUSED(inserted);
    created->second.Order = m_nextLocalActorActionOrder++;
    if (m_nextLocalActorActionOrder == 0)
        m_nextLocalActorActionOrder = 1;
    return created->second;
}

bool VRActorReplicationService::TryCommitLocalActorAction(const GameplayBridge::EventRecord& acRecord) noexcept
{
    const auto transactionIt = m_localActorActions.find(acRecord.Header.Identity.ActionId);
    if (transactionIt == m_localActorActions.end())
        return false;
    const auto& transaction = transactionIt->second;
    if (!transaction.HasMetadata || !transaction.Snapshot.IsComplete() || transaction.TextChunkCount == 0 ||
        transaction.TextReceived.count() != transaction.TextChunkCount ||
        transaction.Metadata.SnapshotId != acRecord.Header.Identity.ActionId ||
        transaction.Metadata.TextId != acRecord.Header.Identity.ActionId)
        return false;

    std::string text;
    for (std::uint16_t index = 0; index < transaction.TextChunkCount; ++index)
        text.append(transaction.TextChunks[index].data(), transaction.TextLengths[index]);
    const auto separator = text.find('\0');
    if (separator == std::string::npos || text.find('\0', separator + 1) != std::string::npos ||
        separator > kMaximumActorActionStringBytes || text.size() - separator - 1 > kMaximumActorActionStringBytes)
    {
        m_localActorActions.erase(transactionIt);
        return false;
    }

    std::uint32_t serverId{};
    if (transaction.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value)
        serverId = m_avatars.GetLocalServerId();
    else if (transaction.TargetHandle.Value == 0)
        serverId = m_npcOwnership.GetServerIdForLocalReference(transaction.ActorLocalFormId);
    if (serverId == 0)
        return false;

    ActionEvent action{};
    action.Tick = m_world.GetTick();
    action.ActorId = serverId;
    action.State1 = transaction.Metadata.State1;
    action.State2 = transaction.Metadata.State2;
    action.Type = transaction.Metadata.Type;
    // ActionEvent predates GameId and intentionally carries raw form IDs.
    // The session's enforced mod list keeps these IDs compatible on the wire.
    action.ActionId = transaction.Metadata.ActionLocalFormId;
    action.TargetId = transaction.Metadata.ActionTargetLocalFormId;
    action.IdleId = transaction.Metadata.IdleLocalFormId;
    action.EventName = TiltedPhoques::String{text.data(), separator};
    action.TargetEventName = TiltedPhoques::String{text.data() + separator + 1, text.size() - separator - 1};
    action.Variables.Booleans.resize(transaction.Snapshot.Booleans.size());
    action.Variables.Floats.resize(transaction.Snapshot.Floats.size());
    action.Variables.Integers.resize(transaction.Snapshot.Integers.size());
    for (std::size_t index = 0; index < transaction.Snapshot.Booleans.size(); ++index)
        action.Variables.Booleans[index] = transaction.Snapshot.Booleans[index];
    for (std::size_t index = 0; index < transaction.Snapshot.Floats.size(); ++index)
        action.Variables.Floats[index] = transaction.Snapshot.Floats[index];
    for (std::size_t index = 0; index < transaction.Snapshot.Integers.size(); ++index)
        action.Variables.Integers[index] = std::bit_cast<std::uint32_t>(transaction.Snapshot.Integers[index]);

    ClientActorActionRequest request{};
    request.ServerId = serverId;
    request.Action = std::move(action);
    if (!m_transport.Send(request))
        return false;
    m_localActorActions.erase(transactionIt);
    return true;
}

bool VRActorReplicationService::HasHumanoidActorActionVariables(const ActionEvent& acAction) const noexcept
{
    const std::string_view eventName{acAction.EventName.c_str(), acAction.EventName.size()};
    const std::string_view targetEventName{acAction.TargetEventName.c_str(), acAction.TargetEventName.size()};
    return acAction.ActionId != 0 && (acAction.Type & ~0x7u) == 0 &&
           eventName.size() <= kMaximumActorActionStringBytes &&
           targetEventName.size() <= kMaximumActorActionStringBytes &&
           eventName.find('\0') == std::string_view::npos && targetEventName.find('\0') == std::string_view::npos &&
           acAction.Variables.Booleans.size() == SkyrimTogetherVR::AnimationGraphProtocol::kBooleanCount &&
           acAction.Variables.Floats.size() == SkyrimTogetherVR::AnimationGraphProtocol::kFloatCount &&
           acAction.Variables.Integers.size() == SkyrimTogetherVR::AnimationGraphProtocol::kIntegerCount &&
           std::all_of(acAction.Variables.Floats.begin(), acAction.Variables.Floats.end(),
                       [](const float aValue) noexcept { return IsFinite(aValue); });
}

std::uint64_t VRActorReplicationService::NextRemoteActorActionId() noexcept
{
    const auto actionId = m_nextRemoteActorActionId++;
    if (m_nextRemoteActorActionId == 0)
        m_nextRemoteActorActionId = 1;
    return actionId == 0 ? NextRemoteActorActionId() : actionId;
}

bool VRActorReplicationService::SubmitRemoteActorAction(const std::uint32_t aServerId,
                                                         const ActionEvent& acAction) noexcept
{
    if (!HasExactActorActionCapability() || aServerId == 0 || !HasHumanoidActorActionVariables(acAction))
        return false;

    const auto actionForm = acAction.ActionId;
    const auto targetForm = acAction.TargetId;
    const auto idleForm = acAction.IdleId;

    GameplayBridge::CommandRecord base{};
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(aServerId, GameplayBridge::GameplayDomain::Animation,
                                                         GameplayBridge::GameplayAction::ActorAction, base))
        return false;
    const auto target = base.Payload.ApplyGameplayAction.TargetHandle;
    if (target.Value == 0)
        return false;
    const auto transactionId = NextRemoteActorActionId();
    std::vector<GameplayBridge::CommandRecord> commands;
    commands.reserve(12);

    const auto submitGraph = [&](const SkyrimTogetherVR::AnimationGraphProtocol::ValueType aType,
                                 const std::uint16_t aStart, const std::uint16_t aCount,
                                 const auto& acValues) noexcept {
        commands.emplace_back();
        auto& command = commands.back();
        command.Header = base.Header;
        command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::StageActorActionGraphChunk);
        auto& payload = command.Payload.StageActorActionGraphChunk;
        payload.TargetHandle = target;
        payload.SnapshotId = transactionId;
        payload.DescriptorVersion = SkyrimTogetherVR::AnimationGraphProtocol::kDescriptorVersion;
        payload.ValueType = static_cast<std::uint16_t>(aType);
        payload.StartIndex = aStart;
        payload.ValueCount = aCount;
        payload.TotalCount = SkyrimTogetherVR::AnimationGraphProtocol::ExpectedCount(aType);
        payload.ChunkFlags = SkyrimTogetherVR::AnimationGraphProtocol::FullSnapshot;
        payload.Direction = acAction.Variables.Floats[1];
        for (std::uint16_t index = 0; index < aCount; ++index)
            payload.Values[index] = std::bit_cast<std::uint32_t>(acValues[aStart + index]);
        return true;
    };

    commands.emplace_back();
    auto& booleanCommand = commands.back();
    booleanCommand.Header = base.Header;
    booleanCommand.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::StageActorActionGraphChunk);
    auto& booleanPayload = booleanCommand.Payload.StageActorActionGraphChunk;
    booleanPayload.TargetHandle = target;
    booleanPayload.SnapshotId = transactionId;
    booleanPayload.DescriptorVersion = SkyrimTogetherVR::AnimationGraphProtocol::kDescriptorVersion;
    booleanPayload.ValueType = static_cast<std::uint16_t>(SkyrimTogetherVR::AnimationGraphProtocol::ValueType::BooleanBits);
    booleanPayload.ValueCount = SkyrimTogetherVR::AnimationGraphProtocol::kBooleanCount;
    booleanPayload.TotalCount = SkyrimTogetherVR::AnimationGraphProtocol::kBooleanCount;
    booleanPayload.ChunkFlags = SkyrimTogetherVR::AnimationGraphProtocol::FullSnapshot;
    booleanPayload.Direction = acAction.Variables.Floats[1];
    for (std::size_t index = 0; index < acAction.Variables.Booleans.size(); ++index) {
        if (acAction.Variables.Booleans[index])
            booleanPayload.Values[index / 32] |= 1u << (index % 32);
    }
    for (std::uint16_t start = 0; start < acAction.Variables.Floats.size(); start += SkyrimTogetherVR::AnimationGraphProtocol::kValuesPerChunk) {
        const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(
            SkyrimTogetherVR::AnimationGraphProtocol::kValuesPerChunk, acAction.Variables.Floats.size() - start));
        if (!submitGraph(SkyrimTogetherVR::AnimationGraphProtocol::ValueType::Float, start, count,
                         acAction.Variables.Floats))
            return false;
    }
    for (std::uint16_t start = 0; start < acAction.Variables.Integers.size(); start += SkyrimTogetherVR::AnimationGraphProtocol::kValuesPerChunk) {
        const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(
            SkyrimTogetherVR::AnimationGraphProtocol::kValuesPerChunk, acAction.Variables.Integers.size() - start));
        if (!submitGraph(SkyrimTogetherVR::AnimationGraphProtocol::ValueType::Integer, start, count,
                         acAction.Variables.Integers))
            return false;
    }

    std::string text{acAction.EventName.c_str(), acAction.EventName.size()};
    text.push_back('\0');
    text.append(acAction.TargetEventName.c_str(), acAction.TargetEventName.size());
    const auto textChunkCount = static_cast<std::uint16_t>(
        (text.size() + GameplayBridge::kGameplayTextBytesPerChunk - 1) / GameplayBridge::kGameplayTextBytesPerChunk);
    for (std::uint16_t index = 0; index < textChunkCount; ++index) {
        commands.emplace_back();
        auto& command = commands.back();
        command.Header = base.Header;
        command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::StageActorActionTextChunk);
        auto& payload = command.Payload.StageActorActionTextChunk;
        payload.TargetHandle = target;
        payload.Domain = static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Animation);
        payload.Action = static_cast<std::uint16_t>(GameplayBridge::GameplayAction::ActorAction);
        payload.TextId = transactionId;
        payload.ChunkIndex = index;
        payload.ChunkCount = textChunkCount;
        const auto offset = static_cast<std::size_t>(index) * GameplayBridge::kGameplayTextBytesPerChunk;
        payload.ByteCount = static_cast<std::uint16_t>(std::min<std::size_t>(
            GameplayBridge::kGameplayTextBytesPerChunk, text.size() - offset));
        std::memcpy(payload.Utf8Bytes, text.data() + offset, payload.ByteCount);
    }

    commands.emplace_back();
    auto& command = commands.back();
    command.Header = base.Header;
    command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyActorAction);
    auto& payload = command.Payload.ApplyActorAction;
    payload.TargetHandle = target;
    payload.ActionLocalFormId = actionForm;
    payload.ActionTargetLocalFormId = targetForm;
    payload.IdleLocalFormId = idleForm;
    payload.State1 = acAction.State1;
    payload.State2 = acAction.State2;
    payload.Type = acAction.Type;
    payload.SnapshotId = transactionId;
    payload.TextId = transactionId;
    return SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommandBatch(commands.data(), commands.size());
}

void VRActorReplicationService::QueueRemoteActorAction(const std::uint32_t aServerId,
                                                        const ActionEvent& acAction) noexcept
{
    if (aServerId == 0 || !HasHumanoidActorActionVariables(acAction))
        return;
    if (std::any_of(m_pendingRemoteActorActions.begin(), m_pendingRemoteActorActions.end(),
                    [aServerId, &acAction](const PendingRemoteActorAction& acPending) noexcept {
                        return acPending.ServerId == aServerId && acPending.Action == acAction;
                    }))
        return;
    if (m_pendingRemoteActorActions.size() >= kMaximumPendingRemoteActorActions)
        m_pendingRemoteActorActions.erase(m_pendingRemoteActorActions.begin());
    m_pendingRemoteActorActions.push_back(PendingRemoteActorAction{aServerId, acAction});
}

void VRActorReplicationService::ForgetPlayer(const std::uint32_t aPlayerId) noexcept
{
    m_ledgers.erase(aPlayerId);
    std::erase_if(m_pendingRemoteActorActions, [this, aPlayerId](const PendingRemoteActorAction& acPending) noexcept {
        return PlayerForServer(acPending.ServerId) == aPlayerId;
    });
    for (auto it = m_serverPlayers.begin(); it != m_serverPlayers.end();) {
        if (it->second == aPlayerId) {
            const auto serverId = it->first;
            m_spawnSnapshots.erase(it->first);
            m_resyncAttempts.erase(it->first);
            m_lastEquipmentTransactionByServer.erase(serverId);
            ForgetEquipmentApplication(serverId);
            ForgetSpawnActionIds(serverId);
            m_pendingMagicEffects.erase(std::remove_if(m_pendingMagicEffects.begin(), m_pendingMagicEffects.end(),
                [serverId](const PendingMagicEffect& acPending) noexcept {
                    return acPending.Message.TargetId == serverId || acPending.Message.CasterId == serverId;
                }), m_pendingMagicEffects.end());
            it = m_serverPlayers.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_pendingSpawns.begin(); it != m_pendingSpawns.end();)
        it = it->second.PlayerId == aPlayerId ? m_pendingSpawns.erase(it) : std::next(it);
}

void VRActorReplicationService::ForgetServer(const std::uint32_t aServerId) noexcept
{
    if (aServerId == 0)
        return;
    const auto playerId = PlayerForServer(aServerId);
    if (playerId != 0)
        m_ledgers.erase(playerId);
    m_serverPlayers.erase(aServerId);
    m_pendingSpawns.erase(aServerId);
    m_spawnSnapshots.erase(aServerId);
    m_resyncAttempts.erase(aServerId);
    ForgetSpawnActionIds(aServerId);
    m_lastEquipmentTransactionByServer.erase(aServerId);
    ForgetEquipmentApplication(aServerId);
    m_pendingMagicEffects.erase(std::remove_if(m_pendingMagicEffects.begin(), m_pendingMagicEffects.end(),
        [aServerId](const PendingMagicEffect& acPending) noexcept {
            return acPending.Message.TargetId == aServerId || acPending.Message.CasterId == aServerId;
        }), m_pendingMagicEffects.end());
    m_pendingMounts.erase(aServerId);
    std::erase_if(m_pendingMounts, [aServerId](const auto& acEntry) {
        return acEntry.second == aServerId;
    });
    std::erase_if(m_pendingRemoteActorActions, [aServerId](const PendingRemoteActorAction& acPending) noexcept {
        return acPending.ServerId == aServerId;
    });
    std::erase_if(m_localActorActions, [this, aServerId](const auto& acEntry) noexcept {
        const auto& transaction = acEntry.second;
        if (!transaction.HasMetadata)
            return false;
        return (transaction.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value &&
                m_localServerId == aServerId) ||
               (transaction.TargetHandle.Value == 0 &&
                m_npcOwnership.GetServerIdForLocalReference(transaction.ActorLocalFormId) == aServerId);
    });
    if (m_localServerId == aServerId)
        m_localServerId = 0;
}

void VRActorReplicationService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept
{
    ForgetServer(acMessage.ServerId);
}

void VRActorReplicationService::OnCharacterSpawn(const CharacterSpawnRequest& acMessage) noexcept
{
    if (acMessage.ServerId == 0)
        return;
    if (acMessage.IsPlayer && acMessage.PlayerId == m_transport.GetLocalPlayerId()) {
        m_localServerId = acMessage.ServerId;
        return;
    }
    if (acMessage.IsPlayer && !IsRemotePlayer(m_transport, acMessage.PlayerId))
        return;
    m_serverPlayers[acMessage.ServerId] = acMessage.PlayerId;
    ForgetSpawnActionIds(acMessage.ServerId);
    m_resyncAttempts.erase(acMessage.ServerId);
    m_spawnSnapshots[acMessage.ServerId] = acMessage;
    m_pendingSpawns[acMessage.ServerId] = acMessage;
}

void VRActorReplicationService::OnReferencesMove(const ServerReferencesMoveRequest& acMessage) noexcept
{
    for (const auto& [serverId, update] : acMessage.Updates)
    {
        const auto playerId = PlayerForServer(serverId);
        const bool hasRemoteAvatar = m_avatars.GetRemoteAvatarHandleForServerId(serverId).Value != 0;
        std::uint32_t index{};
        for (const auto& action : update.ActionEvents)
        {
            if (HasExactActorActionCapability() && HasHumanoidActorActionVariables(action)) {
                if (!SubmitRemoteActorAction(serverId, action))
                    QueueRemoteActorAction(serverId, action);
                continue;
            }
            if (!hasRemoteAvatar)
                continue;
            const std::string_view eventName{action.EventName.c_str()};
            if (eventName.empty() || eventName.size() > 127 ||
                !GameplayBridge::IsSupportedLegacyAnimationEvent(eventName))
                continue;
            auto textId = Signature(acMessage.Tick, playerId, index++, action.Tick);
            if (textId == 0)
                textId = 1;
            ApplyTextForServer(serverId, GameplayBridge::GameplayDomain::Animation,
                               GameplayBridge::GameplayAction::AnimationEvent, textId, eventName);
        }
    }
}

void VRActorReplicationService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    const auto lifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    if (lifecycleEpoch == 0 && m_observedLifecycleEpoch != 0)
    {
        m_pendingSpawns.clear();
        m_pendingMounts.clear();
        m_pendingMagicEffects.clear();
        m_resyncAttempts.clear();
        m_ledgers.clear();
        m_lastEquipmentTransactionByServer.clear();
        m_pendingEquipmentApplications.clear();
        m_equipmentActionOwners.clear();
        m_localActorActions.clear();
        m_pendingRemoteActorActions.clear();
        m_spawnActionOwners.clear();
        m_recordingSpawnServerId = 0;
        m_observedLifecycleEpoch = 0;
        m_replayAfterLifecycleBoundary = true;
        return;
    }
    if (lifecycleEpoch != 0 && m_observedLifecycleEpoch != 0 && lifecycleEpoch != m_observedLifecycleEpoch)
    {
        m_ledgers.clear();
        m_lastEquipmentTransactionByServer.clear();
        m_pendingEquipmentApplications.clear();
        m_equipmentActionOwners.clear();
        m_pendingMounts.clear();
        m_pendingMagicEffects.clear();
        m_resyncAttempts.clear();
        m_localActorActions.clear();
        m_pendingRemoteActorActions.clear();
        m_spawnActionOwners.clear();
        m_recordingSpawnServerId = 0;
        m_replayAfterLifecycleBoundary = true;
        m_observedLifecycleEpoch = lifecycleEpoch;
        return;
    }
    if (lifecycleEpoch != 0)
        m_observedLifecycleEpoch = lifecycleEpoch;
    if (lifecycleEpoch != 0 && m_replayAfterLifecycleBoundary)
    {
        m_pendingSpawns = m_spawnSnapshots;
        m_replayAfterLifecycleBoundary = false;
    }

    m_localServerId = m_avatars.GetLocalServerId();
    if (std::isfinite(acEvent.Delta) && acEvent.Delta > 0.0) {
        std::unordered_set<std::uint32_t> timedOutSpawns;
        for (auto& [actionId, tracking] : m_spawnActionOwners) {
            TP_UNUSED(actionId);
            tracking.ResultWaitElapsed += acEvent.Delta;
            if (tracking.ResultWaitElapsed >= kSpawnResultTimeoutSeconds)
                timedOutSpawns.insert(tracking.ServerId);
        }
        for (const auto serverId : timedOutSpawns) {
            ForgetSpawnActionIds(serverId);
            auto& attempts = m_resyncAttempts[serverId];
            const auto snapshot = m_spawnSnapshots.find(serverId);
            if (snapshot != m_spawnSnapshots.end() && attempts < 3) {
                ++attempts;
                m_pendingSpawns[serverId] = snapshot->second;
            }
        }
    }
    for (auto& [serverId, pending] : m_pendingEquipmentApplications) {
        if (pending.AwaitingResult) {
            if (std::isfinite(acEvent.Delta) && acEvent.Delta > 0.0)
                pending.ResultWaitElapsed += acEvent.Delta;
            if (pending.ResultWaitElapsed < kEquipmentResultTimeoutSeconds)
                continue;

            if (pending.ActionId != 0)
                m_equipmentActionOwners.erase(pending.ActionId);
            pending.ActionId = 0;
            pending.AwaitingResult = false;
            pending.ResultWaitElapsed = 0.0;
            if (pending.ResultFailures < kMaximumEquipmentResultFailures)
                ++pending.ResultFailures;
        }
        if (!pending.AwaitingResult && pending.ResultFailures < kMaximumEquipmentResultFailures)
            TP_UNUSED(TrySubmitEquipmentApplication(serverId, pending));
    }
    for (auto it = m_pendingSpawns.begin(); it != m_pendingSpawns.end();)
    {
        if (!SubmitSpawn(it->second))
        {
            ++it;
            continue;
        }
        if (const auto appearance = m_latestAppearances.find(it->second.PlayerId);
            appearance != m_latestAppearances.end())
            ApplyVRAppearance(it->second.PlayerId, appearance->second);
        it = m_pendingSpawns.erase(it);
    }
    for (auto it = m_pendingRemoteActorActions.begin(); it != m_pendingRemoteActorActions.end();) {
        if (SubmitRemoteActorAction(it->ServerId, it->Action))
            it = m_pendingRemoteActorActions.erase(it);
        else
            ++it;
    }
    std::vector<std::uint64_t> localActorActionIds;
    localActorActionIds.reserve(m_localActorActions.size());
    for (const auto& [actionId, transaction] : m_localActorActions) {
        if (transaction.HasMetadata)
            localActorActionIds.push_back(actionId);
    }
    for (const auto actionId : localActorActionIds) {
        GameplayBridge::EventRecord record{};
        record.Header.Identity.ActionId = actionId;
        TryCommitLocalActorAction(record);
    }
    for (auto it = m_pendingMagicEffects.begin(); it != m_pendingMagicEffects.end();) {
        const auto result = SubmitMagicEffect(it->Message);
        if (result != MagicEffectSubmitResult::AwaitingActor || ++it->Attempts >= kMaximumPendingMagicEffectAttempts)
            it = m_pendingMagicEffects.erase(it);
        else
            ++it;
    }
    for (auto it = m_pendingMounts.begin(); it != m_pendingMounts.end();) {
        if (TryApplyMount(it->first, it->second))
            it = m_pendingMounts.erase(it);
        else
            ++it;
    }
}

bool VRActorReplicationService::SubmitSpawn(const CharacterSpawnRequest& acMessage) noexcept
{
    GameplayBridge::CommandRecord probe{};
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(acMessage.ServerId, GameplayBridge::GameplayDomain::Animation,
                                                         GameplayBridge::GameplayAction::AnimationEvent, probe))
        return false;

    ForgetSpawnActionIds(acMessage.ServerId);
    struct ScopedSpawnActionRecording
    {
        std::uint32_t& RecordingServerId;
        ~ScopedSpawnActionRecording() { RecordingServerId = 0; }
    } recording{m_recordingSpawnServerId};
    m_recordingSpawnServerId = acMessage.ServerId;
    bool submitted = true;
    const auto retainSubmission = [&submitted](const bool aAccepted) noexcept {
        submitted = aAccepted && submitted;
    };

    // Initial inventory/equipment precedes draw state so bridge-side equip work is visible to DrawWeapon.
    retainSubmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Inventory,
                                    GameplayBridge::GameplayAction::InventoryReset, Payload()));
    for (const auto& entry : acMessage.InventoryContent.Entries)
    {
        const auto formId = ToLocal(m_world, entry.BaseId);
        if (formId == 0)
            continue;
        auto payload = Payload();
        payload.LocalFormIdA = formId;
        payload.ValueA = entry.Count;
        payload.ActionFlags = GameplayBridge::kInventorySnapshotEntry |
                              (entry.IsQuestItem ? GameplayBridge::kInventoryQuestItem : 0u);
        retainSubmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Inventory,
                                        GameplayBridge::GameplayAction::InventoryDelta, payload));
        if (entry.IsWorn())
        {
            payload.ValueA = std::max(1, entry.Count);
            retainSubmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Equipment,
                                            GameplayBridge::GameplayAction::EquipForm, payload));
        }
    }
    const auto& magicEquipment = acMessage.InventoryContent.CurrentMagicEquipment;
    const std::array<GameId, 3> magicForms{
        magicEquipment.LeftHandSpell, magicEquipment.RightHandSpell, magicEquipment.Shout};
    for (std::size_t index = 0; index < magicForms.size(); ++index)
    {
        const auto formId = ToLocal(m_world, magicForms[index]);
        if (formId == 0)
            continue;
        auto payload = Payload();
        payload.LocalFormIdA = formId;
        payload.LocalFormIdB = index == 0 ? kLeftHandEquipSlotFormId :
                               index == 1 ? kRightHandEquipSlotFormId : 0;
        payload.ValueA = 1;
        payload.ActionFlags = index < 2 ? kFlagBool0 : kFlagBool1;
        retainSubmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Equipment,
                                        GameplayBridge::GameplayAction::EquipForm, payload));
    }
    retainSubmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::ActorState,
                                    GameplayBridge::GameplayAction::ResetFactions, Payload()));
    const auto applyFactions = [this, serverId = acMessage.ServerId, &retainSubmission](const auto& acFactions) noexcept {
        for (const auto& faction : acFactions)
        {
            auto payload = Payload();
            payload.LocalFormIdA = ToLocal(m_world, faction.Id);
            payload.ValueA = faction.Rank;
            if (payload.LocalFormIdA != 0)
                retainSubmission(ApplyForServer(serverId, GameplayBridge::GameplayDomain::ActorState,
                                                GameplayBridge::GameplayAction::SetFactionRank, payload));
        }
    };
    applyFactions(acMessage.FactionsContent.NpcFactions);
    applyFactions(acMessage.FactionsContent.ExtraFactions);
    std::uint32_t replayIndex{};
    for (const auto& action : acMessage.ActionsToReplay.Actions)
    {
        if (HasExactActorActionCapability() && HasHumanoidActorActionVariables(action)) {
            if (!SubmitRemoteActorAction(acMessage.ServerId, action))
                QueueRemoteActorAction(acMessage.ServerId, action);
            continue;
        }
        const std::string_view eventName{action.EventName.c_str()};
        if (eventName.empty() || eventName.size() > 127 ||
            !GameplayBridge::IsSupportedLegacyAnimationEvent(eventName))
            continue;
        auto textId = Signature(acMessage.ServerId, acMessage.PlayerId, action.Tick, replayIndex++);
        if (textId == 0)
            textId = 1;
        retainSubmission(ApplyTextForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Animation,
                                            GameplayBridge::GameplayAction::AnimationEvent, textId, eventName));
    }
    std::vector<std::pair<std::uint32_t, float>> values(acMessage.InitialActorValues.ActorValuesList.begin(),
                                                          acMessage.InitialActorValues.ActorValuesList.end());
    std::sort(values.begin(), values.end());
    for (const auto& [id, value] : values)
    {
        if (!IsFinite(value) || id >= kActorValueCount || id == kDragonSoulsActorValue)
            continue;
        auto payload = Payload();
        payload.LocalFormIdA = id;
        payload.ScalarA = value;
        retainSubmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::ActorState,
                                        GameplayBridge::GameplayAction::SetActorValue, payload));
    }
    values.assign(acMessage.InitialActorValues.ActorMaxValuesList.begin(), acMessage.InitialActorValues.ActorMaxValuesList.end());
    std::sort(values.begin(), values.end());
    for (const auto& [id, value] : values)
    {
        if (!IsFinite(value) || id >= kActorValueCount || id == kDragonSoulsActorValue)
            continue;
        auto payload = Payload();
        payload.LocalFormIdA = id;
        payload.ScalarA = value;
        retainSubmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::ActorState,
                                        GameplayBridge::GameplayAction::SetActorMaximum, payload));
    }
    for (const auto& tint : acMessage.FaceTints.Entries)
    {
        if (!IsFinite(tint.Alpha) || tint.Type != GameplayBridge::kSupportedSkinTintType)
            continue;
        auto payload = Payload();
        payload.LocalFormIdB = tint.Color;
        payload.ValueA = static_cast<std::int32_t>(tint.Type);
        payload.ScalarA = tint.Alpha;
        retainSubmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Appearance,
                                        GameplayBridge::GameplayAction::SetTint, payload));
    }
    auto payload = Payload();
    payload.ValueA = acMessage.IsDead ? 1 : 0;
    retainSubmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::ActorState,
                                    GameplayBridge::GameplayAction::SetDeathState, payload));
    payload = Payload();
    payload.ValueA = acMessage.IsWeaponDrawn ? 1 : 0;
    retainSubmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Animation,
                                    GameplayBridge::GameplayAction::DrawWeapon, payload));
    return submitted;
}

void VRActorReplicationService::OnDrawWeapon(const NotifyDrawWeapon& acMessage) noexcept
{
    if (!Accept(acMessage.Id, GameplayBridge::GameplayDomain::Animation, 0,
                Signature(acMessage.Id, acMessage.IsWeaponDrawn)))
        return;
    auto payload = Payload();
    payload.ValueA = acMessage.IsWeaponDrawn ? 1 : 0;
    ApplyForServer(acMessage.Id, GameplayBridge::GameplayDomain::Animation, GameplayBridge::GameplayAction::DrawWeapon, payload);
}

bool VRActorReplicationService::TrySubmitEquipmentApplication(
    const std::uint32_t aServerId, PendingEquipmentApplication& arPending) noexcept
{
    if (aServerId == 0 || arPending.TransactionId == 0 || arPending.AwaitingResult ||
        arPending.ResultFailures >= kMaximumEquipmentResultFailures || arPending.Entries.size() > 64)
        return false;

    const auto transactionLow = static_cast<std::uint32_t>(arPending.TransactionId);
    const auto transactionHigh = static_cast<std::uint32_t>(arPending.TransactionId >> 32);
    std::vector<GameplayBridge::CommandRecord> commands;
    commands.reserve(arPending.Entries.size() + 2);
    const auto append = [this, aServerId, &commands](const GameplayBridge::GameplayAction aAction,
                                                     const GameplayBridge::GameplayActionPayload& acPayload) {
        GameplayBridge::CommandRecord command{};
        if (!m_avatars.BuildRemoteGameplayCommandForServerId(
                aServerId, GameplayBridge::GameplayDomain::Equipment, aAction, command))
            return false;
        const auto target = command.Payload.ApplyGameplayAction.TargetHandle;
        command.Payload.ApplyGameplayAction = acPayload;
        command.Payload.ApplyGameplayAction.TargetHandle = target;
        command.Payload.ApplyGameplayAction.Domain =
            static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Equipment);
        command.Payload.ApplyGameplayAction.Action = static_cast<std::uint16_t>(aAction);
        commands.push_back(command);
        return true;
    };

    auto payload = Payload();
    payload.LocalFormIdA = arPending.LeftSpell;
    payload.LocalFormIdB = arPending.RightSpell;
    payload.LocalFormIdC = arPending.Shout;
    payload.LocalFormIdD = transactionHigh;
    payload.ValueA = static_cast<std::int32_t>(arPending.Entries.size());
    payload.ValueB = static_cast<std::int32_t>(transactionLow);
    if (!append(GameplayBridge::GameplayAction::EquipmentSnapshotBegin, payload))
        return false;
    for (const auto& entry : arPending.Entries) {
        payload = Payload();
        payload.LocalFormIdA = entry.LocalFormId;
        payload.LocalFormIdB = transactionHigh;
        payload.LocalFormIdC = transactionLow;
        payload.ValueA = entry.Count;
        payload.ActionFlags = entry.Flags;
        if (!append(GameplayBridge::GameplayAction::EquipmentSnapshotItem, payload))
            return false;
    }
    payload = Payload();
    payload.LocalFormIdA = transactionHigh;
    payload.LocalFormIdB = transactionLow;
    if (!append(GameplayBridge::GameplayAction::EquipmentSnapshotEnd, payload) ||
        !SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommandBatch(commands.data(), commands.size()))
        return false;

    const auto actionId = commands.front().Header.Identity.ActionId;
    if (actionId == 0)
        return false;
    arPending.ActionId = actionId;
    arPending.AwaitingResult = true;
    arPending.ResultWaitElapsed = 0.0;
    m_equipmentActionOwners[actionId] = EquipmentActionTracking{aServerId, arPending.TransactionId};
    return true;
}

void VRActorReplicationService::ForgetEquipmentApplication(const std::uint32_t aServerId) noexcept
{
    if (const auto pending = m_pendingEquipmentApplications.find(aServerId);
        pending != m_pendingEquipmentApplications.end()) {
        if (pending->second.ActionId != 0)
            m_equipmentActionOwners.erase(pending->second.ActionId);
        m_pendingEquipmentApplications.erase(pending);
    }
    std::erase_if(m_equipmentActionOwners, [aServerId](const auto& acEntry) noexcept {
        return acEntry.second.ServerId == aServerId;
    });
}

void VRActorReplicationService::OnEquipment(const NotifyEquipmentChanges& acMessage) noexcept
{
    if (acMessage.TransactionId != 0) {
        if (acMessage.ServerId == 0 || acMessage.ItemId || acMessage.EquipSlotId || acMessage.Count != 0 ||
            acMessage.Unequip || acMessage.IsSpell || acMessage.IsShout ||
            acMessage.FinalEquipment.Entries.size() > 64)
            return;
        const auto known = m_lastEquipmentTransactionByServer.find(acMessage.ServerId);
        if (known != m_lastEquipmentTransactionByServer.end() && acMessage.TransactionId <= known->second)
            return;

        std::vector<PendingEquipmentEntry> entries;
        entries.reserve(acMessage.FinalEquipment.Entries.size());
        std::unordered_set<GameId> seen;
        for (const auto& entry : acMessage.FinalEquipment.Entries) {
            const auto formId = ToLocal(m_world, entry.BaseId);
            if (!entry.BaseId || entry.Count <= 0 || entry.Count > 10'000 || !entry.IsWorn() || formId == 0 ||
                !seen.emplace(entry.BaseId).second)
                return;
            entries.push_back({formId, entry.Count,
                               (entry.ExtraWorn ? GameplayBridge::kEquipmentSnapshotWorn : 0u) |
                               (entry.ExtraWornLeft ? GameplayBridge::kEquipmentSnapshotWornLeft : 0u)});
        }
        std::sort(entries.begin(), entries.end(), [](const PendingEquipmentEntry& acLeft,
                                                     const PendingEquipmentEntry& acRight) {
            return acLeft.LocalFormId < acRight.LocalFormId;
        });

        const auto& magic = acMessage.FinalEquipment.CurrentMagicEquipment;
        const auto leftSpell = ToLocal(m_world, magic.LeftHandSpell);
        const auto rightSpell = ToLocal(m_world, magic.RightHandSpell);
        const auto shout = ToLocal(m_world, magic.Shout);
        if ((magic.LeftHandSpell && leftSpell == 0) || (magic.RightHandSpell && rightSpell == 0) ||
            (magic.Shout && shout == 0))
            return;

        auto pending = m_pendingEquipmentApplications.find(acMessage.ServerId);
        if (pending != m_pendingEquipmentApplications.end() &&
            acMessage.TransactionId <= pending->second.TransactionId)
            return;
        if (pending == m_pendingEquipmentApplications.end() &&
            m_pendingEquipmentApplications.size() >= kMaximumPendingEquipmentApplications)
            return;
        ForgetEquipmentApplication(acMessage.ServerId);
        auto [inserted, created] = m_pendingEquipmentApplications.emplace(
            acMessage.ServerId,
            PendingEquipmentApplication{acMessage.TransactionId, leftSpell, rightSpell, shout,
                                        std::move(entries), 0, 0, 0.0, false});
        TP_UNUSED(created);
        TP_UNUSED(TrySubmitEquipmentApplication(acMessage.ServerId, inserted->second));
        return;
    }

    const auto item = ToLocal(m_world, acMessage.ItemId);
    const auto slot = ToLocal(m_world, acMessage.EquipSlotId);
    if (item == 0 || !Accept(acMessage.ServerId, GameplayBridge::GameplayDomain::Equipment, 0,
                             Signature(acMessage.ServerId, item, slot, acMessage.Count, acMessage.Unequip)))
        return;
    auto payload = Payload();
    payload.LocalFormIdA = item;
    payload.LocalFormIdB = slot;
    payload.ValueA = static_cast<std::int32_t>(acMessage.Count);
    payload.ActionFlags = (acMessage.IsSpell ? kFlagBool0 : 0) | (acMessage.IsShout ? kFlagBool1 : 0);
    ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Equipment,
                   acMessage.Unequip ? GameplayBridge::GameplayAction::UnequipForm : GameplayBridge::GameplayAction::EquipForm, payload);
}

void VRActorReplicationService::OnFactionsChanges(const NotifyFactionsChanges& acMessage) noexcept
{
    for (const auto& [serverId, factions] : acMessage.Changes)
    {
        if (serverId == 0 || factions.NpcFactions.size() > kMaximumFactionEntries ||
            factions.ExtraFactions.size() > kMaximumFactionEntries)
            continue;

        auto signature = Signature(serverId, factions.NpcFactions.size(), factions.ExtraFactions.size());
        const auto mixFactions = [&signature](const auto& acFactions, const std::uint32_t aKind) noexcept {
            for (const auto& faction : acFactions)
                signature = Mix(signature, Signature(faction.Id.LogFormat(), faction.Rank, aKind));
        };
        mixFactions(factions.NpcFactions, 0);
        mixFactions(factions.ExtraFactions, 1);
        if (!Accept(serverId, GameplayBridge::GameplayDomain::ActorState, 0, signature))
            continue;

        ApplyForServer(serverId, GameplayBridge::GameplayDomain::ActorState,
                       GameplayBridge::GameplayAction::ResetFactions, Payload());
        const auto applyFactions = [this, serverId](const auto& acFactions) noexcept {
            for (const auto& faction : acFactions)
            {
                const auto formId = ToLocal(m_world, faction.Id);
                const auto rank = static_cast<std::int32_t>(faction.Rank);
                if (formId == 0 || rank < std::numeric_limits<std::int8_t>::min() ||
                    rank > std::numeric_limits<std::int8_t>::max())
                    continue;

                auto payload = Payload();
                payload.LocalFormIdA = formId;
                payload.ValueA = rank;
                ApplyForServer(serverId, GameplayBridge::GameplayDomain::ActorState,
                               GameplayBridge::GameplayAction::SetFactionRank, payload);
            }
        };
        applyFactions(factions.NpcFactions);
        applyFactions(factions.ExtraFactions);
    }
}

void VRActorReplicationService::OnInventory(const NotifyInventoryChanges& acMessage) noexcept
{
    const auto item = ToLocal(m_world, acMessage.Item.BaseId);
    if (item == 0 || !Accept(acMessage.ServerId, GameplayBridge::GameplayDomain::Inventory, 0,
                             Signature(acMessage.ServerId, item, acMessage.Item.Count, acMessage.Drop)))
        return;
    auto payload = Payload();
    payload.LocalFormIdA = item;
    payload.ValueA = acMessage.Item.Count;
    payload.ActionFlags = (acMessage.Item.IsQuestItem ? GameplayBridge::kInventoryQuestItem : 0) |
                          (acMessage.Drop ? GameplayBridge::kInventoryDrop : 0);
    ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Inventory, GameplayBridge::GameplayAction::InventoryDelta, payload);
}

void VRActorReplicationService::OnActorValues(const NotifyActorValueChanges& acMessage) noexcept
{
    std::vector<std::pair<std::uint32_t, float>> values(acMessage.Values.begin(), acMessage.Values.end());
    std::sort(values.begin(), values.end());
    for (const auto& [id, value] : values)
    {
        if (!IsFinite(value) || id >= kActorValueCount ||
            id == kDragonSoulsActorValue || !Accept(acMessage.Id, GameplayBridge::GameplayDomain::ActorState, 0,
                                        Signature(acMessage.Id, id, FloatBits(value))))
            continue;
        auto payload = Payload();
        payload.LocalFormIdA = id;
        payload.ScalarA = value;
        ApplyForServer(acMessage.Id, GameplayBridge::GameplayDomain::ActorState, GameplayBridge::GameplayAction::SetActorValue, payload);
    }
}

void VRActorReplicationService::OnActorMaximums(const NotifyActorMaxValueChanges& acMessage) noexcept
{
    std::vector<std::pair<std::uint32_t, float>> values(acMessage.Values.begin(), acMessage.Values.end());
    std::sort(values.begin(), values.end());
    for (const auto& [id, value] : values)
    {
        if (!IsFinite(value) || id >= kActorValueCount || id == kDragonSoulsActorValue ||
            !Accept(acMessage.Id, GameplayBridge::GameplayDomain::ActorState, 0,
                                        Signature(acMessage.Id, id, FloatBits(value), 1)))
            continue;
        auto payload = Payload();
        payload.LocalFormIdA = id;
        payload.ScalarA = value;
        ApplyForServer(acMessage.Id, GameplayBridge::GameplayDomain::ActorState, GameplayBridge::GameplayAction::SetActorMaximum, payload);
    }
}

void VRActorReplicationService::OnHealthChangeBroadcast(const NotifyHealthChangeBroadcast& acMessage) noexcept
{
    if (acMessage.Id == 0 || !IsFinite(acMessage.DeltaHealth))
        return;

    auto payload = Payload();
    payload.LocalFormIdA = kHealthActorValue;
    payload.ScalarA = acMessage.DeltaHealth;
    ApplyForTarget(acMessage.Id, GameplayBridge::GameplayDomain::ActorState,
                   GameplayBridge::GameplayAction::ModifyActorValue, payload);
}

void VRActorReplicationService::OnDeath(const NotifyDeathStateChange& acMessage) noexcept
{
    if (!Accept(acMessage.Id, GameplayBridge::GameplayDomain::ActorState, 0,
                Signature(acMessage.Id, acMessage.IsDead, 2)))
        return;
    auto payload = Payload();
    payload.ValueA = acMessage.IsDead ? 1 : 0;
    ApplyForServer(acMessage.Id, GameplayBridge::GameplayDomain::ActorState, GameplayBridge::GameplayAction::SetDeathState, payload);
}

void VRActorReplicationService::OnRespawn(const NotifyRespawn& acMessage) noexcept
{
    if (!Accept(acMessage.ActorId, GameplayBridge::GameplayDomain::ActorState, 0,
                Signature(acMessage.ActorId, 3)))
        return;
    ApplyForServer(acMessage.ActorId, GameplayBridge::GameplayDomain::ActorState, GameplayBridge::GameplayAction::Respawn, Payload());
}

void VRActorReplicationService::OnMount(const NotifyMount& acMessage) noexcept
{
    if (!Accept(acMessage.RiderId, GameplayBridge::GameplayDomain::ActorState, 0,
                Signature(acMessage.RiderId, acMessage.MountId)))
        return;
    if (acMessage.MountId == 0)
        return;
    if (TryApplyMount(acMessage.RiderId, acMessage.MountId)) {
        m_pendingMounts.erase(acMessage.RiderId);
        return;
    }
    constexpr std::size_t kMaximumPendingMounts = 64;
    if (m_pendingMounts.contains(acMessage.RiderId) || m_pendingMounts.size() < kMaximumPendingMounts)
        m_pendingMounts[acMessage.RiderId] = acMessage.MountId;
}

bool VRActorReplicationService::TryApplyMount(
    const std::uint32_t aRiderServerId, const std::uint32_t aMountServerId) noexcept
{
    if (aRiderServerId == 0 || aMountServerId == 0)
        return false;
    const auto mountHandle = m_avatars.GetRemoteAvatarHandleForServerId(aMountServerId);
    if (mountHandle.Value == 0)
        return false;
    GameplayBridge::CommandRecord command{};
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(
            aRiderServerId, GameplayBridge::GameplayDomain::ActorState,
            GameplayBridge::GameplayAction::Mount, command))
        return false;
    command.Payload.ApplyGameplayAction.SecondaryHandle = mountHandle;
    return SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command);
}

void VRActorReplicationService::OnProjectile(const NotifyProjectileLaunch& acMessage) noexcept
{
    const auto projectile = ToLocal(m_world, acMessage.ProjectileBaseID);
    const auto weapon = ToLocal(m_world, acMessage.WeaponID);
    const auto ammo = ToLocal(m_world, acMessage.AmmoID);
    const auto spell = ToLocal(m_world, acMessage.SpellID);
    const auto parentCell = ToLocal(m_world, acMessage.ParentCellID);
    const auto bounded = [](const float aValue, const float aLimit) noexcept {
        return IsFinite(aValue) && aValue >= -aLimit && aValue <= aLimit;
    };
    if (projectile == 0 || parentCell == 0 || (acMessage.WeaponID && weapon == 0) ||
        (acMessage.AmmoID && ammo == 0) || (acMessage.SpellID && spell == 0) ||
        !bounded(acMessage.OriginX, GameplayBridge::kMaximumProjectileCoordinate) ||
        !bounded(acMessage.OriginY, GameplayBridge::kMaximumProjectileCoordinate) ||
        !bounded(acMessage.OriginZ, GameplayBridge::kMaximumProjectileCoordinate) ||
        !bounded(acMessage.XAngle, GameplayBridge::kMaximumProjectileAngle) ||
        !bounded(acMessage.ZAngle, GameplayBridge::kMaximumProjectileAngle) ||
        !IsFinite(acMessage.Power) || acMessage.Power < 0.0F || acMessage.Power > GameplayBridge::kMaximumProjectilePower ||
        !IsFinite(acMessage.Scale) || acMessage.Scale < 0.0F || acMessage.Scale > GameplayBridge::kMaximumProjectileScale ||
        acMessage.CastingSource < 0 || acMessage.CastingSource > 3 || acMessage.Area < 0 ||
        acMessage.Area > GameplayBridge::kMaximumProjectileArea ||
        !Accept(acMessage.ShooterID, GameplayBridge::GameplayDomain::Projectile, 0,
                Signature(acMessage.ShooterID, projectile, FloatBits(acMessage.OriginX),
                          FloatBits(acMessage.OriginY), FloatBits(acMessage.OriginZ))))
        return;

    GameplayBridge::CommandRecord command{};
    const auto shooter = m_avatars.GetRemoteAvatarHandleForServerId(acMessage.ShooterID);
    if (shooter.Value == 0)
        return;
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(
            acMessage.ShooterID, GameplayBridge::GameplayDomain::Projectile,
            GameplayBridge::GameplayAction::LaunchProjectile, command))
        return;

    command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyProjectileLaunch);
    auto& payload = command.Payload.ApplyProjectileLaunch;
    payload = {};
    payload.TargetHandle = shooter;
    payload.LocalProjectileBaseFormId = projectile;
    payload.LocalWeaponFormId = weapon;
    payload.LocalAmmoFormId = ammo;
    payload.LocalSpellFormId = spell;
    payload.LocalParentCellFormId = parentCell;
    payload.OriginX = acMessage.OriginX;
    payload.OriginY = acMessage.OriginY;
    payload.OriginZ = acMessage.OriginZ;
    payload.AngleX = acMessage.XAngle;
    payload.AngleZ = acMessage.ZAngle;
    payload.Power = acMessage.Power;
    payload.Scale = acMessage.Scale;
    payload.CastingSource = acMessage.CastingSource;
    payload.Area = acMessage.Area;
    payload.LaunchFlags = (acMessage.AlwaysHit ? GameplayBridge::ProjectileAlwaysHit : 0) |
                          (acMessage.NoDamageOutsideCombat ? GameplayBridge::ProjectileNoDamageOutsideCombat : 0) |
                          (acMessage.AutoAim ? GameplayBridge::ProjectileAutoAim : 0) |
                          (acMessage.UnkBool2 ? GameplayBridge::ProjectileChainShatter : 0) |
                          (acMessage.DeferInitialization ? GameplayBridge::ProjectileDeferInitialization : 0) |
                          (acMessage.ForceConeOfFire ? GameplayBridge::ProjectileForceConeOfFire : 0);
    (void)SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command);
}

void VRActorReplicationService::OnSpawnData(const NotifySpawnData& acMessage) noexcept
{
    if (acMessage.Id == 0)
        return;

    const auto snapshot = m_spawnSnapshots.find(acMessage.Id);
    if (snapshot == m_spawnSnapshots.end())
        return;

    snapshot->second.InitialActorValues = acMessage.NewActorData.InitialActorValues;
    snapshot->second.InventoryContent = acMessage.NewActorData.InitialInventory;
    snapshot->second.IsDead = acMessage.NewActorData.IsDead;
    snapshot->second.IsWeaponDrawn = acMessage.NewActorData.IsWeaponDrawn;
    ForgetSpawnActionIds(acMessage.Id);
    m_resyncAttempts.erase(acMessage.Id);
    m_pendingSpawns[acMessage.Id] = snapshot->second;
}

void VRActorReplicationService::OnSpellCast(const NotifySpellCast& acMessage) noexcept
{
    const auto spell = ToLocal(m_world, acMessage.SpellFormId);
    if (spell == 0 || !Accept(acMessage.CasterId, GameplayBridge::GameplayDomain::Magic, 0,
                              Signature(acMessage.CasterId, spell, acMessage.CastingSource,
                                        acMessage.DesiredTarget)))
        return;
    auto payload = Payload();
    payload.LocalFormIdA = spell;
    payload.ValueA = acMessage.CastingSource;
    payload.ScalarA = 1.0f;
    payload.ActionFlags = acMessage.IsDualCasting ? (1u << 2) : 0;
    ApplyForServer(acMessage.CasterId, GameplayBridge::GameplayDomain::Magic, GameplayBridge::GameplayAction::CastSpell, payload);
}

void VRActorReplicationService::OnInterruptCast(const NotifyInterruptCast& acMessage) noexcept
{
    if (!Accept(acMessage.CasterId, GameplayBridge::GameplayDomain::Magic, 0,
                Signature(acMessage.CasterId, acMessage.CastingSource, 1)))
        return;
    auto payload = Payload();
    payload.ValueA = acMessage.CastingSource;
    ApplyForServer(acMessage.CasterId, GameplayBridge::GameplayDomain::Magic, GameplayBridge::GameplayAction::InterruptCast, payload);
}

void VRActorReplicationService::OnNotifyRemoveSpell(const NotifyRemoveSpell& acMessage) noexcept
{
    const auto spell = ToLocal(m_world, acMessage.SpellId);
    if (acMessage.TargetId == 0 || spell == 0)
        return;

    auto payload = Payload();
    payload.LocalFormIdA = spell;
    ApplyForTarget(acMessage.TargetId, GameplayBridge::GameplayDomain::Magic,
                   GameplayBridge::GameplayAction::RemoveSpell, payload);
}

void VRActorReplicationService::OnNotifyAddTarget(const NotifyAddTarget& acMessage) noexcept
{
    if (SubmitMagicEffect(acMessage) != MagicEffectSubmitResult::AwaitingActor ||
        m_pendingMagicEffects.size() >= kMaximumPendingMagicEffects)
        return;

    m_pendingMagicEffects.push_back({acMessage, 0});
}

VRActorReplicationService::MagicEffectSubmitResult VRActorReplicationService::SubmitMagicEffect(
    const NotifyAddTarget& acMessage) noexcept
{
    if (acMessage.TargetId == 0 || !IsFinite(acMessage.Magnitude) || acMessage.Magnitude < 0.0F ||
        acMessage.ApplyHealPerkBonus || acMessage.ApplyStaminaPerkBonus)
        return MagicEffectSubmitResult::Rejected;

    const auto spell = ToLocal(m_world, acMessage.SpellId);
    const auto effect = ToLocal(m_world, acMessage.EffectId);
    if (spell == 0 || effect == 0)
        return MagicEffectSubmitResult::Rejected;

    const bool localTarget = acMessage.TargetId == m_localServerId;
    const auto targetPlayer = m_serverPlayers.find(acMessage.TargetId);
    if (!localTarget && targetPlayer == m_serverPlayers.end())
        return MagicEffectSubmitResult::Rejected;
    if (!localTarget && m_avatars.GetRemoteAvatarHandleForServerId(acMessage.TargetId).Value == 0)
        return MagicEffectSubmitResult::AwaitingActor;

    auto payload = Payload();
    payload.LocalFormIdA = spell;
    payload.LocalFormIdB = effect;
    payload.ValueA = kCastingSourceOther;
    payload.ScalarA = acMessage.Magnitude;
    payload.ScalarB = 1.0F;
    payload.ActionFlags = acMessage.IsDualCasting ? kFlagBool1 : 0;

    if (acMessage.CasterId != 0) {
        if (acMessage.CasterId == m_localServerId) {
            payload.SecondaryHandle = GameplayBridge::kLocalPlayerHandle;
        } else {
            const auto casterPlayer = m_serverPlayers.find(acMessage.CasterId);
            if (casterPlayer == m_serverPlayers.end())
                return MagicEffectSubmitResult::Rejected;
            payload.SecondaryHandle = m_avatars.GetRemoteAvatarHandleForServerId(acMessage.CasterId);
            if (payload.SecondaryHandle.Value == 0)
                return MagicEffectSubmitResult::AwaitingActor;
        }
    }

    return ApplyForTarget(acMessage.TargetId, GameplayBridge::GameplayDomain::Magic,
                          GameplayBridge::GameplayAction::ApplyMagicEffect, payload) ?
        MagicEffectSubmitResult::Submitted : MagicEffectSubmitResult::Rejected;
}

void VRActorReplicationService::OnVrEquipment(const NotifyVREquipmentUpdate& acMessage) noexcept
{
    // VREquipmentUpdate is status-only for bridge-capable clients. In
    // particular, do not advance the equipment/draw ledgers here: canonical
    // NotifyEquipmentChanges and NotifyDrawWeapon own those apply paths.
    TP_UNUSED(acMessage);
}

void VRActorReplicationService::OnVrCombat(const NotifyVRCombatHitEvent& acMessage) noexcept
{
    // The VR hit snapshot has no authoritative damage amount. Original
    // actor-value/combat messages own mutation; treating this as MeleeHit would
    // report a successful zero-damage action and obscure PLANCK deduplication.
    TP_UNUSED(acMessage);
}

void VRActorReplicationService::OnVrMagic(const NotifyVRMagicEffectEvent& acMessage) noexcept
{
    const auto& magic = acMessage.MagicEffect;
    if (!IsFinite(magic.CasterPosition) || !IsFinite(magic.TargetPosition) ||
        !Accept(acMessage.PlayerId, GameplayBridge::GameplayDomain::Magic, magic.Sequence,
                Signature(magic.EffectId.LogFormat(), magic.CasterId.LogFormat(), magic.TargetId.LogFormat())))
        return;
    // This diagnostic packet identifies only an MGEF. The canonical adapter
    // action requires its owning MagicItem and effect index, so damage/effects
    // continue through original spell and actor-value messages.
    TP_UNUSED(magic);
}

void VRActorReplicationService::OnVrProjectile(const NotifyVRProjectileEvent& acMessage) noexcept
{
    const auto& projectile = acMessage.Projectile;
    if (!IsFinite(projectile.Origin) || !IsFinite(projectile.Destination) || !IsFinite(projectile.Power) ||
        !Accept(acMessage.PlayerId, GameplayBridge::GameplayDomain::Projectile, projectile.Sequence,
                Signature(projectile.SourceId.LogFormat(), projectile.WeaponId.LogFormat(), projectile.SpellId.LogFormat(), projectile.EventKind)))
        return;
    // VR bow/spell events expose intent and controller pose only. They do not
    // carry the complete Projectile::LaunchData required for deterministic
    // replay, so retain them as diagnostics and let the original full launch
    // protocol remain the only native projectile application path.
    TP_UNUSED(projectile);
}

void VRActorReplicationService::OnVrPose(const NotifyVRPoseUpdate& acMessage) noexcept
{
    const auto& pose = acMessage.Pose;
    if (!Accept(acMessage.PlayerId, GameplayBridge::GameplayDomain::VrBodyPose, pose.Sequence,
                Signature(pose.Body.CaptureSequence, pose.Body.RootGeneration, pose.Hmd.Valid, pose.LeftHand.Valid, pose.RightHand.Valid)))
        return;
    const std::array<const VRPoseNodeData*, static_cast<std::size_t>(GameplayBridge::GameplayPoseNode::Count)> nodes{
        &pose.Hmd,
        &pose.LeftHand,
        &pose.RightHand,
        &pose.Body.Pelvis,
        &pose.Body.LeftThigh,
        &pose.Body.LeftCalf,
        &pose.Body.LeftFoot,
        &pose.Body.RightThigh,
        &pose.Body.RightCalf,
        &pose.Body.RightFoot,
    };
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        const auto& node = *nodes[index];
        if (!node.Valid || !IsFinite(node.Position.x) || !IsFinite(node.Position.y) || !IsFinite(node.Position.z) ||
            !IsFinite(node.AxisX.x) || !IsFinite(node.AxisX.y) || !IsFinite(node.AxisX.z) ||
            !IsFinite(node.AxisY.x) || !IsFinite(node.AxisY.y) || !IsFinite(node.AxisY.z) ||
            !IsFinite(node.AxisZ.x) || !IsFinite(node.AxisZ.y) || !IsFinite(node.AxisZ.z) || !IsFinite(node.Scale))
            continue;

        auto payload = Payload();
        payload.ValueA = static_cast<std::int32_t>(pose.Sequence);
        payload.ValueB = static_cast<std::int32_t>(pose.Body.RootGeneration);
        payload.ScalarA = node.Position.x;
        payload.ScalarB = node.Position.y;
        payload.ScalarC = node.Position.z;
        payload.ScalarD = node.Scale;
        payload.ActionFlags = GameplayBridge::kPoseChunkPresent |
                              (static_cast<std::uint32_t>(index) << GameplayBridge::kPoseChunkNodeShift);
        ApplyForPlayer(acMessage.PlayerId, GameplayBridge::GameplayDomain::VrBodyPose, GameplayBridge::GameplayAction::VrPoseChunk, payload);

        const std::array<glm::vec3, 3> axes{node.AxisX, node.AxisY, node.AxisZ};
        for (std::uint32_t axis = 0; axis < axes.size(); ++axis)
        {
            payload.ScalarA = axes[axis].x;
            payload.ScalarB = axes[axis].y;
            payload.ScalarC = axes[axis].z;
            payload.ScalarD = 0.0F;
            payload.ActionFlags = GameplayBridge::kPoseChunkPresent | GameplayBridge::kPoseChunkBasis |
                                  (axis << GameplayBridge::kPoseChunkAxisShift) |
                                  (static_cast<std::uint32_t>(index) << GameplayBridge::kPoseChunkNodeShift);
            ApplyForPlayer(acMessage.PlayerId, GameplayBridge::GameplayDomain::VrBodyPose,
                           GameplayBridge::GameplayAction::VrPoseChunk, payload);
        }
    }
}

void VRActorReplicationService::OnVrHiggs(const NotifyVRHiggsState& acMessage) noexcept
{
    const auto& state = acMessage.State;
    if (!Accept(acMessage.PlayerId, GameplayBridge::GameplayDomain::Higgs, state.Sequence,
                Signature(state.LastEvent.Sequence, state.LastEvent.EventKind, state.LastEvent.ObjectId.LogFormat())))
        return;
    if (!state.LastEventValid)
        return;
    GameplayBridge::GameplayAction action{};
    switch (state.LastEvent.EventKind)
    {
    case VRHiggsEventSnapshot::Kind::kPulled: action = GameplayBridge::GameplayAction::HiggsPull; break;
    case VRHiggsEventSnapshot::Kind::kGrabbed: action = GameplayBridge::GameplayAction::HiggsGrab; break;
    case VRHiggsEventSnapshot::Kind::kDropped: action = GameplayBridge::GameplayAction::HiggsDrop; break;
    case VRHiggsEventSnapshot::Kind::kStashed: action = GameplayBridge::GameplayAction::HiggsStash; break;
    case VRHiggsEventSnapshot::Kind::kConsumed: action = GameplayBridge::GameplayAction::HiggsConsume; break;
    default: return;
    }
    if (!IsFinite(state.LastEvent.Mass) || !IsFinite(state.LastEvent.SeparatingVelocity))
        return;
    auto payload = Payload();
    payload.LocalFormIdA = ToLocal(m_world, state.LastEvent.ObjectId);
    if (payload.LocalFormIdA == 0)
        return;
    payload.ScalarA = state.LastEvent.Mass;
    payload.ScalarB = state.LastEvent.SeparatingVelocity;
    payload.ActionFlags = (state.LastEvent.HasHand ? kFlagBool0 : 0) | (state.LastEvent.IsLeft ? kFlagLeftHand : 0) |
                          (state.TwoHanding ? kFlagBool1 : 0);
    ApplyForPlayer(acMessage.PlayerId, GameplayBridge::GameplayDomain::Higgs, action, payload);
}

void VRActorReplicationService::OnVrAppearance(const NotifyVRAppearance& acMessage) noexcept
{
    const auto& appearance = acMessage.Appearance;
    if (!IsRemotePlayer(m_transport, acMessage.PlayerId) || !appearance.IsValid() ||
        !Accept(acMessage.PlayerId, GameplayBridge::GameplayDomain::Appearance, appearance.Sequence,
                AppearanceSignature(appearance)))
        return;

    m_latestAppearances[acMessage.PlayerId] = appearance;
    ApplyVRAppearance(acMessage.PlayerId, appearance);
}

bool VRActorReplicationService::ApplyVRAppearance(
    const std::uint32_t aPlayerId, const VRAppearance& acAppearance) noexcept
{
    const auto& appearance = acAppearance;

    const auto race = ToLocal(m_world, appearance.RaceId);
    if (race == 0)
        return false;
    auto payload = Payload();
    payload.LocalFormIdA = race;
    bool submitted = ApplyForPlayer(aPlayerId, GameplayBridge::GameplayDomain::Appearance,
                                    GameplayBridge::GameplayAction::SetRace, payload);

    payload = Payload();
    payload.ValueA = appearance.Sex;
    submitted = ApplyForPlayer(aPlayerId, GameplayBridge::GameplayDomain::Appearance,
                               GameplayBridge::GameplayAction::SetSex, payload) && submitted;

    payload = Payload();
    payload.ScalarA = appearance.Weight;
    submitted = ApplyForPlayer(aPlayerId, GameplayBridge::GameplayDomain::Appearance,
                               GameplayBridge::GameplayAction::SetWeight, payload) && submitted;

    payload = Payload();
    payload.ValueA = appearance.Level;
    submitted = ApplyForPlayer(aPlayerId, GameplayBridge::GameplayDomain::ActorState,
                               GameplayBridge::GameplayAction::SetLevel, payload) && submitted;

    payload = Payload();
    payload.ValueA = appearance.Essential ? 1 : 0;
    submitted = ApplyForPlayer(aPlayerId, GameplayBridge::GameplayDomain::ActorState,
                               GameplayBridge::GameplayAction::SetEssential, payload) && submitted;

    for (std::uint8_t index = 0; index < appearance.HeadPartCount; ++index)
    {
        const auto formId = ToLocal(m_world, appearance.HeadParts[index].FormId);
        if (formId == 0)
            continue;
        payload = Payload();
        payload.LocalFormIdA = formId;
        payload.ValueA = appearance.HeadParts[index].Slot;
        submitted = ApplyForPlayer(aPlayerId, GameplayBridge::GameplayDomain::Appearance,
                                   GameplayBridge::GameplayAction::SetHeadPart, payload) && submitted;
    }
    for (std::uint8_t index = 0; index < appearance.TintCount; ++index)
    {
        if (appearance.Tints[index].Type != GameplayBridge::kSupportedSkinTintType)
            continue;
        payload = Payload();
        payload.LocalFormIdB = appearance.Tints[index].Color;
        payload.ValueA = appearance.Tints[index].Type;
        payload.ScalarA = appearance.Tints[index].Alpha;
        submitted = ApplyForPlayer(aPlayerId, GameplayBridge::GameplayDomain::Appearance,
                                   GameplayBridge::GameplayAction::SetTint, payload) && submitted;
    }

    if (appearance.NameLength == 0)
        return submitted;
    const auto textId = (static_cast<std::uint64_t>(appearance.Sequence) << 32) | aPlayerId;
    submitted = ApplyTextForPlayer(
                    aPlayerId, GameplayBridge::GameplayDomain::Appearance,
                    GameplayBridge::GameplayAction::SetName, textId,
                    std::string_view{appearance.Name.data(), appearance.NameLength}) && submitted;
    return submitted;
}

void VRActorReplicationService::OnVrGrab(const NotifyVRGrabEvent& acMessage) noexcept
{
    const auto& grab = acMessage.Grab;
    if (!Accept(acMessage.PlayerId, GameplayBridge::GameplayDomain::Higgs, grab.Sequence,
                Signature(grab.ObjectId.LogFormat(), grab.Grabbed, FloatBits(grab.Position.x), FloatBits(grab.Position.y), FloatBits(grab.Position.z)),
                1))
        return;
    const auto object = ToLocal(m_world, grab.ObjectId);
    if (object == 0 || !IsFinite(grab.Position))
        return;
    auto payload = Payload();
    payload.LocalFormIdA = object;
    payload.LocalFormIdB = ToLocal(m_world, grab.CellId);
    payload.LocalFormIdC = ToLocal(m_world, grab.WorldSpaceId);
    payload.ValueA = static_cast<std::int32_t>(grab.FormType);
    payload.ScalarA = grab.Position.x;
    payload.ScalarB = grab.Position.y;
    payload.ScalarC = grab.Position.z;
    ApplyForPlayer(acMessage.PlayerId, GameplayBridge::GameplayDomain::Higgs,
                   grab.Grabbed ? GameplayBridge::GameplayAction::HiggsGrab : GameplayBridge::GameplayAction::HiggsDrop, payload);
}

void VRActorReplicationService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    m_latestAppearances.erase(acMessage.PlayerId);
    ForgetPlayer(acMessage.PlayerId);
}

void VRActorReplicationService::OnPlayerLevel(const NotifyPlayerLevel& acMessage) noexcept
{
    if (acMessage.PlayerId == 0 || acMessage.NewLevel == 0)
        return;

    auto payload = Payload();
    payload.ValueA = acMessage.NewLevel;
    TP_UNUSED(ApplyForPlayer(acMessage.PlayerId, GameplayBridge::GameplayDomain::ActorState,
                             GameplayBridge::GameplayAction::SetLevel, payload));
}

void VRActorReplicationService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);
    m_serverPlayers.clear();
    m_pendingSpawns.clear();
    m_spawnSnapshots.clear();
    m_latestAppearances.clear();
    m_pendingMounts.clear();
    m_resyncAttempts.clear();
    m_ledgers.clear();
    m_lastEquipmentTransactionByServer.clear();
    m_pendingEquipmentApplications.clear();
    m_equipmentActionOwners.clear();
    m_pendingMagicEffects.clear();
    m_localActorActions.clear();
    m_pendingRemoteActorActions.clear();
    m_spawnActionOwners.clear();
    m_recordingSpawnServerId = 0;
    m_localServerId = 0;
    m_observedLifecycleEpoch = 0;
    m_replayAfterLifecycleBoundary = false;
}

void VRActorReplicationService::OnGameplayResult(
    const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept
{
    const auto& record = acEvent.Record;
    if (record.Header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::RemoteGameplayActionState))
        return;
    const auto& result = record.Payload.RemoteGameplayActionState;
    if (const auto equipment = m_equipmentActionOwners.find(record.Header.Identity.ActionId);
        equipment != m_equipmentActionOwners.end()) {
        const auto owner = equipment->second;
        const auto pending = m_pendingEquipmentApplications.find(owner.ServerId);
        if (pending == m_pendingEquipmentApplications.end() ||
            pending->second.TransactionId != owner.TransactionId ||
            pending->second.ActionId != record.Header.Identity.ActionId)
        {
            m_equipmentActionOwners.erase(equipment);
            return;
        }

        auto& application = pending->second;
        const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
        const auto domain = static_cast<GameplayBridge::GameplayDomain>(result.Domain);
        const auto action = static_cast<GameplayBridge::GameplayAction>(result.Action);
        if (status == GameplayBridge::CommandStatus::Success &&
            domain == GameplayBridge::GameplayDomain::Equipment) {
            application.ResultWaitElapsed = 0.0;
            if (action != GameplayBridge::GameplayAction::EquipmentSnapshotEnd)
                return;
            m_lastEquipmentTransactionByServer[owner.ServerId] = owner.TransactionId;
            m_equipmentActionOwners.erase(equipment);
            m_pendingEquipmentApplications.erase(pending);
            return;
        }

        m_equipmentActionOwners.erase(equipment);
        application.ActionId = 0;
        application.AwaitingResult = false;
        application.ResultWaitElapsed = 0.0;
        if (application.ResultFailures < kMaximumEquipmentResultFailures)
            ++application.ResultFailures;
        return;
    }

    const auto serverId = m_avatars.GetRemoteServerIdForHandle(result.TargetHandle);
    if (serverId == 0)
        return;

    const auto tracked = m_spawnActionOwners.find(record.Header.Identity.ActionId);
    if (tracked == m_spawnActionOwners.end() || tracked->second.ServerId != serverId)
        return;

    const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
    if (status == GameplayBridge::CommandStatus::Success) {
        tracked->second.ResultWaitElapsed = 0.0;
        if (tracked->second.RemainingResults > 1)
            --tracked->second.RemainingResults;
        else
            m_spawnActionOwners.erase(tracked);
        if (!HasSpawnActionIds(serverId))
            m_resyncAttempts.erase(serverId);
        return;
    }
    m_spawnActionOwners.erase(tracked);
    if (status != GameplayBridge::CommandStatus::Inactive &&
        status != GameplayBridge::CommandStatus::MissingForm &&
        status != GameplayBridge::CommandStatus::MissingCell &&
        status != GameplayBridge::CommandStatus::EngineRejected &&
        status != GameplayBridge::CommandStatus::QueueOverflow) {
        if (!HasSpawnActionIds(serverId))
            m_resyncAttempts.erase(serverId);
        return;
    }

    auto& attempts = m_resyncAttempts[serverId];
    const auto snapshot = m_spawnSnapshots.find(serverId);
    if (snapshot == m_spawnSnapshots.end() || attempts >= 3)
        return;
    ForgetSpawnActionIds(serverId);
    ++attempts;
    m_pendingSpawns[serverId] = snapshot->second;
}

void VRActorReplicationService::ForgetSpawnActionIds(const std::uint32_t aServerId) noexcept
{
    for (auto it = m_spawnActionOwners.begin(); it != m_spawnActionOwners.end();) {
        if (it->second.ServerId == aServerId)
            it = m_spawnActionOwners.erase(it);
        else
            ++it;
    }
}

bool VRActorReplicationService::HasSpawnActionIds(const std::uint32_t aServerId) const noexcept
{
    return std::any_of(
        m_spawnActionOwners.begin(),
        m_spawnActionOwners.end(),
        [aServerId](const auto& a_entry) noexcept { return a_entry.second.ServerId == aServerId; });
}

void VRActorReplicationService::OnLocalGameplay(
    const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept
{
    if (!IsCurrentActorActionRecord(acEvent))
        return;

    const auto& record = acEvent.Record;
    const auto actionId = record.Header.Identity.ActionId;
    auto& transaction = GetOrCreateLocalActorAction(actionId);
    const auto coherentActor = [&transaction](const GameplayBridge::AdapterHandle aTargetHandle,
                                              const std::uint32_t aActorLocalFormId) noexcept {
        return transaction.ActorLocalFormId == 0 ||
               (transaction.TargetHandle.Value == aTargetHandle.Value &&
                transaction.ActorLocalFormId == aActorLocalFormId);
    };

    switch (static_cast<GameplayBridge::EventKind>(record.Header.Kind))
    {
    case GameplayBridge::EventKind::LocalActorActionMetadata:
    {
        const auto& payload = record.Payload.LocalActorActionMetadata;
        if (transaction.HasMetadata || !coherentActor(payload.TargetHandle, payload.ActorLocalFormId) ||
            (transaction.Snapshot.SnapshotId != 0 && transaction.Snapshot.SnapshotId != payload.SnapshotId) ||
            (transaction.TextId != 0 && transaction.TextId != payload.TextId))
            return;
        transaction.TargetHandle = payload.TargetHandle;
        transaction.ActorLocalFormId = payload.ActorLocalFormId;
        transaction.Metadata = payload;
        transaction.TextId = payload.TextId;
        transaction.HasMetadata = true;
        TP_UNUSED(TryCommitLocalActorAction(record));
        return;
    }
    case GameplayBridge::EventKind::LocalActorActionGraphChunk:
    {
        const auto& payload = record.Payload.LocalActorActionGraphChunk;
        if (!coherentActor(payload.TargetHandle, payload.ActorLocalFormId) ||
            (transaction.Snapshot.SnapshotId != 0 && transaction.Snapshot.SnapshotId != payload.SnapshotId))
            return;
        const auto type = static_cast<SkyrimTogetherVR::AnimationGraphProtocol::ValueType>(payload.ValueType);
        const auto chunkBit = type == SkyrimTogetherVR::AnimationGraphProtocol::ValueType::BooleanBits ? 1u :
                              1u << (payload.StartIndex / SkyrimTogetherVR::AnimationGraphProtocol::kValuesPerChunk);
        const auto receivedMask = type == SkyrimTogetherVR::AnimationGraphProtocol::ValueType::BooleanBits ?
                                      transaction.Snapshot.BooleanChunkMask :
                                  type == SkyrimTogetherVR::AnimationGraphProtocol::ValueType::Float ?
                                      transaction.Snapshot.FloatChunkMask : transaction.Snapshot.IntegerChunkMask;
        if ((receivedMask & chunkBit) != 0)
            return;
        transaction.TargetHandle = payload.TargetHandle;
        transaction.ActorLocalFormId = payload.ActorLocalFormId;
        const auto accepted = SkyrimTogetherVR::AnimationGraphProtocol::AcceptChunk(
            transaction.Snapshot, payload.SnapshotId, type, payload.StartIndex, payload.ValueCount,
            payload.TotalCount, payload.Direction, payload.Values);
        if (accepted == SkyrimTogetherVR::AnimationGraphProtocol::ChunkAcceptResult::Malformed ||
            accepted == SkyrimTogetherVR::AnimationGraphProtocol::ChunkAcceptResult::Stale)
            return;
        if (transaction.HasMetadata)
            TP_UNUSED(TryCommitLocalActorAction(record));
        return;
    }
    case GameplayBridge::EventKind::LocalActorActionTextChunk:
    {
        const auto& payload = record.Payload.LocalActorActionTextChunk;
        if (!coherentActor(payload.TargetHandle, payload.TargetLocalFormId) ||
            (transaction.TextId != 0 && transaction.TextId != payload.TextId) ||
            (transaction.TextChunkCount != 0 && transaction.TextChunkCount != payload.ChunkCount) ||
            transaction.TextReceived.test(payload.ChunkIndex))
            return;
        transaction.TargetHandle = payload.TargetHandle;
        transaction.ActorLocalFormId = payload.TargetLocalFormId;
        transaction.TextId = payload.TextId;
        transaction.TextChunkCount = payload.ChunkCount;
        std::copy_n(payload.Utf8Bytes, payload.ByteCount, transaction.TextChunks[payload.ChunkIndex].begin());
        transaction.TextLengths[payload.ChunkIndex] = payload.ByteCount;
        transaction.TextReceived.set(payload.ChunkIndex);
        if (transaction.HasMetadata)
            TP_UNUSED(TryCommitLocalActorAction(record));
        return;
    }
    default:
        return;
    }
}
