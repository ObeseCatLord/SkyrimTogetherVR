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
#include <cstddef>
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
constexpr std::uint8_t kMaximumPendingMagicEffectAttempts = 8;
constexpr double kMagicActorRetryDelaySeconds = 0.25;
constexpr std::size_t kMaximumPendingGameplayWork = 192;
constexpr std::size_t kMaximumGameplayResultOwners = 512;
constexpr std::size_t kMaximumSemanticTombstones = 2048;
constexpr double kSemanticTombstoneRebaseRetrySeconds = 0.25;
constexpr std::uint8_t kMaximumGameplayWorkAttempts = 3;
constexpr double kGameplayAdmissionTimeoutSeconds = 10.0;
constexpr double kGameplayResultTimeoutSeconds = 10.0;
constexpr std::uint32_t kMagicEffectApplyHealPerkBonus = 1u << 3;
constexpr std::uint32_t kMagicEffectApplyStaminaPerkBonus = 1u << 4;
constexpr std::uint32_t kRightHandEquipSlotFormId = 0x00013F42;
constexpr std::uint32_t kLeftHandEquipSlotFormId = 0x00013F43;
constexpr std::uint32_t kLocalPlayerReferenceFormId = 0x14;
constexpr std::size_t kMaximumFactionEntries = 0x1FF;
constexpr std::size_t kMaximumActorActionTransactions = 64;
constexpr std::size_t kMaximumPendingRemoteActorActions = 64;
constexpr std::size_t kMaximumTrackedRemoteActorActions = 128;
constexpr double kActorActionResultTimeoutSeconds = 10.0;
constexpr std::uint8_t kMaximumActorActionAttempts = 3;
constexpr std::size_t kMaximumActorActionStringBytes = 127;
constexpr std::size_t kMaximumPendingEquipmentApplications = 128;
constexpr std::size_t kMaximumEquipmentResultOwners = 512;
constexpr std::uint8_t kMaximumEquipmentResultFailures = 3;
constexpr double kEquipmentResultTimeoutSeconds = 2.0;
constexpr std::size_t kMaximumPendingInventoryTransactions = 64;
constexpr double kInventoryTransactionRetryDelaySeconds = 0.25;
constexpr double kInventoryTransactionResultTimeoutSeconds = 10.0;
constexpr double kSpawnResultTimeoutSeconds = 2.0;
constexpr std::size_t kMaximumPendingAppearanceApplications = 128;
constexpr std::size_t kMaximumTrackedSpawnStates = 128;
constexpr std::size_t kMaximumSpawnActionOwners = 512;
constexpr std::uint8_t kMaximumAppearanceResultFailures = 8;
constexpr std::uint8_t kMaximumAppearanceSubmissionFailures = 8;
constexpr double kAppearanceResultTimeoutSeconds = 2.0;
constexpr std::uint64_t kNpcAppearanceTargetBit = 1ull << 63;

[[nodiscard]] constexpr std::uint64_t NpcAppearanceTargetKey(const std::uint32_t aServerId) noexcept
{
    return kNpcAppearanceTargetBit | aServerId;
}

[[nodiscard]] constexpr bool IsNpcAppearanceTarget(const std::uint64_t aTargetKey) noexcept
{
    return (aTargetKey & kNpcAppearanceTargetBit) != 0;
}

[[nodiscard]] constexpr std::uint32_t AppearanceTargetId(const std::uint64_t aTargetKey) noexcept
{
    return static_cast<std::uint32_t>(aTargetKey);
}

[[nodiscard]] bool IsNewer(const std::uint32_t aCandidate, const std::uint32_t aPrevious) noexcept
{
    return static_cast<std::int32_t>(aCandidate - aPrevious) > 0;
}

[[nodiscard]] double AppearanceRetryDelay(const std::uint8_t aFailures) noexcept
{
    const auto exponent = std::min<std::uint8_t>(aFailures, 4);
    return std::min(2.0, 0.125 * static_cast<double>(1u << exponent));
}

[[nodiscard]] bool MatchesTrackedResult(
    const GameplayBridge::EventRecord& acRecord,
    const GameplayBridge::BridgeIdentity& acExpectedIdentity,
    const GameplayBridge::AdapterHandle aExpectedTarget) noexcept
{
    const auto& identity = acRecord.Header.Identity;
    return identity.ServerInstanceNonce == acExpectedIdentity.ServerInstanceNonce &&
           identity.ConnectionGeneration == acExpectedIdentity.ConnectionGeneration &&
           identity.LifecycleEpoch == acExpectedIdentity.LifecycleEpoch &&
           identity.EntityId == acExpectedIdentity.EntityId &&
           identity.EntityGeneration == acExpectedIdentity.EntityGeneration &&
           identity.SequenceId == acExpectedIdentity.SequenceId &&
           identity.ActionId == acExpectedIdentity.ActionId &&
           acRecord.Payload.RemoteGameplayActionState.TargetHandle.Value == aExpectedTarget.Value;
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

[[nodiscard]] std::uint64_t InventoryEntrySignature(const Inventory::Entry& acEntry,
                                                     const bool aDrop) noexcept
{
    auto signature = Signature(acEntry.BaseId.LogFormat(), acEntry.Count, FloatBits(acEntry.ExtraCharge),
                               acEntry.ExtraEnchantId.LogFormat(), acEntry.ExtraEnchantCharge,
                               acEntry.EnchantData.IsWeapon, FloatBits(acEntry.ExtraHealth),
                               acEntry.ExtraPoisonId.LogFormat(), acEntry.ExtraPoisonCount,
                               acEntry.ExtraSoulLevel, acEntry.ExtraEnchantRemoveUnequip,
                               acEntry.ExtraWorn, acEntry.ExtraWornLeft, acEntry.IsQuestItem,
                               acEntry.EquipmentFlags, aDrop);
    for (const auto& effect : acEntry.EnchantData.Effects)
        signature = Mix(signature, Signature(effect.EffectId.LogFormat(), effect.Area, effect.Duration,
                                             FloatBits(effect.Magnitude), FloatBits(effect.RawCost)));
    return signature;
}

[[nodiscard]] std::uint64_t AppearanceSignature(const VRAppearance& acAppearance) noexcept
{
    std::uint64_t signature = Signature(acAppearance.RaceId.LogFormat(), acAppearance.Sex, FloatBits(acAppearance.Weight),
                                        acAppearance.Level, acAppearance.Essential, acAppearance.NameLength,
                                        acAppearance.HeadPartCount, acAppearance.TintCount,
                                        acAppearance.HairColorId.LogFormat(), acAppearance.FaceTextureId.LogFormat(),
                                        acAppearance.HasFaceData);
    for (const auto morph : acAppearance.FaceMorphs)
        signature = Mix(signature, FloatBits(morph));
    for (const auto part : acAppearance.FaceParts)
        signature = Mix(signature, static_cast<std::uint32_t>(part));
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
        signature = Mix(signature, acAppearance.Tints[index].TexturePathLength);
        for (std::uint8_t pathIndex = 0; pathIndex < acAppearance.Tints[index].TexturePathLength; ++pathIndex)
            signature = Mix(signature, static_cast<std::uint8_t>(acAppearance.Tints[index].TexturePath[pathIndex]));
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

void RememberBoundedServerId(std::unordered_set<std::uint32_t>& arIds, const std::uint32_t aServerId) noexcept
{
    if (aServerId == 0 || arIds.contains(aServerId))
        return;
    if (arIds.size() >= kMaximumTrackedSpawnStates) {
        const auto oldest = std::min_element(arIds.begin(), arIds.end());
        if (oldest != arIds.end())
            arIds.erase(oldest);
    }
    arIds.insert(aServerId);
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

VRActorReplicationService::AcceptanceToken VRActorReplicationService::PrepareAccept(
    const std::uint32_t aPlayerId, const GameplayBridge::GameplayDomain aDomain,
    const std::uint32_t aSequence, const std::uint64_t aSignature,
    const std::uint8_t aChannel) const noexcept
{
    AcceptanceToken token{};
    constexpr std::size_t kDomainStride = 18;
    const auto domainIndex = static_cast<std::size_t>(aDomain);
    if (aPlayerId == 0 || domainIndex == 0 || domainIndex >= kDomainStride || aChannel > 1)
        return token;

    const auto index = domainIndex + static_cast<std::size_t>(aChannel) * kDomainStride;
    const auto player = m_ledgers.find(aPlayerId);
    const auto* ledger = player != m_ledgers.end() ? &player->second[index] : nullptr;
    const AcceptanceToken candidate{aPlayerId, aDomain, aSequence, aSignature, 0, aChannel, true};
    if (HasSemanticTombstone(candidate))
        return token;
    if (ledger && ((aSequence != 0 && ledger->HasSequence && !IsNewer(aSequence, ledger->LastSequence)) ||
                   (aSequence == 0 && ledger->HasSignature && ledger->LastSignature == aSignature)))
        return token;

    token.PlayerId = aPlayerId;
    token.Domain = aDomain;
    token.Sequence = aSequence;
    token.Signature = aSignature;
    token.LedgerRevision = ledger ? ledger->Revision : 0;
    token.Channel = aChannel;
    token.Valid = true;
    return token;
}

bool VRActorReplicationService::CanCommitAccept(const AcceptanceToken& acToken) const noexcept
{
    if (!acToken.Valid)
        return false;
    constexpr std::size_t kDomainStride = 18;
    const auto domainIndex = static_cast<std::size_t>(acToken.Domain);
    if (acToken.PlayerId == 0 || domainIndex == 0 || domainIndex >= kDomainStride || acToken.Channel > 1)
        return false;
    const auto index = domainIndex + static_cast<std::size_t>(acToken.Channel) * kDomainStride;
    const auto player = m_ledgers.find(acToken.PlayerId);
    const auto revision = player != m_ledgers.end() ? player->second[index].Revision : 0;
    return revision == acToken.LedgerRevision;
}

bool VRActorReplicationService::CommitAccept(const AcceptanceToken& acToken) noexcept try
{
    if (!CanCommitAccept(acToken))
        return false;
    constexpr std::size_t kDomainStride = 18;
    const auto index = static_cast<std::size_t>(acToken.Domain) +
                       static_cast<std::size_t>(acToken.Channel) * kDomainStride;
    auto& ledger = m_ledgers[acToken.PlayerId][index];
    if ((acToken.Sequence != 0 && ledger.HasSequence && !IsNewer(acToken.Sequence, ledger.LastSequence)) ||
        (acToken.Sequence == 0 && ledger.HasSignature && ledger.LastSignature == acToken.Signature))
        return false;
    if (acToken.Sequence != 0) {
        ledger.LastSequence = acToken.Sequence;
        ledger.HasSequence = true;
    }
    ledger.LastSignature = acToken.Signature;
    ledger.HasSignature = true;
    ++ledger.Revision;
    return true;
}
catch (...)
{
    return false;
}

bool VRActorReplicationService::IsSameAcceptance(const AcceptanceToken& acLeft,
                                                  const AcceptanceToken& acRight) const noexcept
{
    return acLeft.Valid && acRight.Valid && acLeft.PlayerId == acRight.PlayerId &&
           acLeft.Domain == acRight.Domain && acLeft.Sequence == acRight.Sequence &&
           acLeft.Signature == acRight.Signature && acLeft.Channel == acRight.Channel;
}

bool VRActorReplicationService::HasSemanticTombstone(const AcceptanceToken& acToken) const noexcept
{
    if (!acToken.Valid)
        return false;
    return m_semanticTombstones.contains({acToken.PlayerId, acToken.Domain, acToken.Sequence,
                                          acToken.Signature, acToken.Channel});
}

bool VRActorReplicationService::RememberSemanticTombstone(
    const AcceptanceToken& acToken, const bool aAcceptanceCommitted) noexcept try
{
    if (!acToken.Valid)
        return false;
    // A committed monotonic sequence already prevents every older sequence
    // from being admitted. Sequence-zero traffic and a failed post-admission
    // ledger commit require an exact retained semantic identity instead.
    if (acToken.Sequence != 0 && aAcceptanceCommitted)
        return true;

    const SemanticTombstone tombstone{acToken.PlayerId, acToken.Domain, acToken.Sequence,
                                      acToken.Signature, acToken.Channel};
    if (m_semanticTombstones.contains(tombstone))
        return true;
    if (m_semanticTombstones.size() >= kMaximumSemanticTombstones) {
        RequestSemanticTombstoneRebase();
        return false;
    }
    return m_semanticTombstones.emplace(tombstone).second;
}
catch (...)
{
    RequestSemanticTombstoneRebase();
    return false;
}

void VRActorReplicationService::ForgetSemanticTombstones(const std::uint32_t aPlayerId) noexcept
{
    std::erase_if(m_semanticTombstones, [aPlayerId](const SemanticTombstone& acTombstone) noexcept {
        return acTombstone.PlayerId == aPlayerId;
    });
}

void VRActorReplicationService::RequestSemanticTombstoneRebase() noexcept
{
    if (m_semanticTombstoneRebaseRequested)
        return;
    m_semanticTombstoneRebaseRequested = true;
    m_semanticTombstoneRebaseEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    m_semanticTombstoneRebaseElapsed = 0.0;
    spdlog::error("VR semantic tombstone capacity exhausted; native capture epoch rebase is pending");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

bool VRActorReplicationService::QueueReliableForServer(
    const AcceptanceToken& acAcceptance, const std::uint32_t aServerId,
    const GameplayBridge::GameplayDomain aDomain, const GameplayBridge::GameplayAction aAction,
    const GameplayBridge::GameplayActionPayload& acPayload) noexcept
{
    return QueueReliableBatchForServer(acAcceptance, aServerId, aDomain, {aAction}, {acPayload});
}

bool VRActorReplicationService::QueueReliableForPlayer(
    const AcceptanceToken& acAcceptance, const std::uint32_t aPlayerId,
    const GameplayBridge::GameplayDomain aDomain, const GameplayBridge::GameplayAction aAction,
    const GameplayBridge::GameplayActionPayload& acPayload) noexcept
{
    return QueueReliableBatchForPlayer(acAcceptance, aPlayerId, aDomain, {aAction}, {acPayload});
}

bool VRActorReplicationService::QueueReliableBatchForServer(
    const AcceptanceToken& acAcceptance, const std::uint32_t aServerId,
    const GameplayBridge::GameplayDomain aDomain, std::vector<GameplayBridge::GameplayAction> aActions,
    std::vector<GameplayBridge::GameplayActionPayload> aPayloads) noexcept
{
    PendingGameplayWork work{};
    work.ServerId = aServerId;
    work.Domain = aDomain;
    work.Actions = std::move(aActions);
    work.Payloads = std::move(aPayloads);
    work.Acceptance = acAcceptance;
    return QueueReliableGameplayWork(std::move(work));
}

bool VRActorReplicationService::QueueReliableBatchForPlayer(
    const AcceptanceToken& acAcceptance, const std::uint32_t aPlayerId,
    const GameplayBridge::GameplayDomain aDomain, std::vector<GameplayBridge::GameplayAction> aActions,
    std::vector<GameplayBridge::GameplayActionPayload> aPayloads) noexcept
{
    PendingGameplayWork work{};
    work.PlayerId = aPlayerId;
    work.Domain = aDomain;
    work.Actions = std::move(aActions);
    work.Payloads = std::move(aPayloads);
    work.Acceptance = acAcceptance;
    work.TargetIsPlayer = true;
    return QueueReliableGameplayWork(std::move(work));
}

bool VRActorReplicationService::QueueReliableProjectile(
    const AcceptanceToken& acAcceptance, const std::uint32_t aServerId,
    const GameplayBridge::ApplyProjectileLaunchPayload& acPayload) noexcept
{
    PendingGameplayWork work{};
    work.ServerId = aServerId;
    work.Domain = GameplayBridge::GameplayDomain::Projectile;
    work.Actions = {GameplayBridge::GameplayAction::LaunchProjectile};
    work.Acceptance = acAcceptance;
    work.Projectile = acPayload;
    work.IsProjectile = true;
    return QueueReliableGameplayWork(std::move(work));
}

bool VRActorReplicationService::QueueReliableTextForServer(
    const AcceptanceToken& acAcceptance, const std::uint32_t aServerId,
    const GameplayBridge::GameplayDomain aDomain, const GameplayBridge::GameplayAction aAction,
    const std::uint64_t aTextId, const std::string_view acText) noexcept
{
    const auto maximumBytes = static_cast<std::size_t>(GameplayBridge::kGameplayTextBytesPerChunk) *
                              GameplayBridge::kMaximumGameplayTextChunks;
    if (aTextId == 0 || acText.empty() || acText.size() > maximumBytes)
        return false;
    const auto chunkCount = static_cast<std::size_t>(
        (acText.size() + GameplayBridge::kGameplayTextBytesPerChunk - 1) /
        GameplayBridge::kGameplayTextBytesPerChunk);
    PendingGameplayWork work{};
    work.ServerId = aServerId;
    work.Domain = aDomain;
    work.Actions.assign(chunkCount, aAction);
    work.Acceptance = acAcceptance;
    work.Text.assign(acText.data(), acText.size());
    work.TextId = aTextId;
    work.IsText = true;
    return QueueReliableGameplayWork(std::move(work));
}

bool VRActorReplicationService::QueueReliableGameplayWork(PendingGameplayWork&& arWork) noexcept try
{
    if (!arWork.Acceptance.Valid || arWork.Domain != arWork.Acceptance.Domain ||
        (arWork.TargetIsPlayer ? !IsRemotePlayer(m_transport, arWork.PlayerId) : arWork.ServerId == 0) ||
        arWork.Actions.empty() ||
        (!arWork.IsProjectile && !arWork.IsText && arWork.Actions.size() != arWork.Payloads.size()) ||
        (arWork.IsText && (arWork.TextId == 0 || arWork.Text.empty())) ||
        arWork.Actions.size() > GameplayBridge::kDefaultCommandRingCapacity)
        return false;

    const auto duplicate = std::find_if(m_pendingGameplayWork.begin(), m_pendingGameplayWork.end(),
        [this, &arWork](const PendingGameplayWork& acExisting) noexcept {
            return IsSameAcceptance(acExisting.Acceptance, arWork.Acceptance);
        });
    if (duplicate != m_pendingGameplayWork.end())
        return true;
    if (m_pendingGameplayWork.size() >= kMaximumPendingGameplayWork)
        return false;

    arWork.WorkId = m_nextGameplayWorkId++;
    if (m_nextGameplayWorkId == 0)
        m_nextGameplayWorkId = 1;
    if (arWork.WorkId == 0)
        arWork.WorkId = m_nextGameplayWorkId++;
    m_pendingGameplayWork.push_back(std::move(arWork));
    TP_UNUSED(TrySubmitReliableGameplayWork(m_pendingGameplayWork.size() - 1));
    return true;
}
catch (...)
{
    return false;
}

bool VRActorReplicationService::TrySubmitReliableGameplayWork(const std::size_t aIndex) noexcept try
{
    if (aIndex >= m_pendingGameplayWork.size())
        return false;
    auto& work = m_pendingGameplayWork[aIndex];
    if (work.AwaitingResult || work.Terminal || (!work.Admitted && !CanCommitAccept(work.Acceptance)))
        return false;
    if (m_gameplayResultOwners.size() > kMaximumGameplayResultOwners - work.Actions.size())
        return false;

    std::vector<GameplayBridge::CommandRecord> commands;
    commands.reserve(work.Actions.size());
    GameplayBridge::AdapterHandle targetHandle{};
    std::uint32_t targetLocalFormId{};
    for (std::size_t index = 0; index < work.Actions.size(); ++index) {
        GameplayBridge::CommandRecord command{};
        const auto built = work.TargetIsPlayer ?
            m_avatars.BuildRemoteGameplayCommand(work.PlayerId, work.Domain, work.Actions[index], command) :
            BuildGameplayCommandForServerActor(work.ServerId, work.Domain, work.Actions[index], command);
        if (!built)
            return false;

        const auto commandHandle = command.Payload.ApplyGameplayAction.TargetHandle;
        const auto commandLocalFormId = command.Payload.ApplyGameplayAction.TargetLocalFormId;
        if (index == 0) {
            targetHandle = commandHandle;
            targetLocalFormId = commandLocalFormId;
        } else if (targetHandle.Value != commandHandle.Value || targetLocalFormId != commandLocalFormId) {
            return false;
        }
        if (work.IsProjectile) {
            command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyProjectileLaunch);
            auto payload = work.Projectile;
            payload.TargetHandle = commandHandle;
            command.Payload.ApplyProjectileLaunch = payload;
        } else if (work.IsText) {
            const auto offset = index * GameplayBridge::kGameplayTextBytesPerChunk;
            command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyGameplayTextChunk);
            auto& payload = command.Payload.ApplyGameplayTextChunk;
            payload = {};
            payload.TargetHandle = commandHandle;
            payload.TargetLocalFormId = commandLocalFormId;
            payload.Domain = static_cast<std::uint16_t>(work.Domain);
            payload.Action = static_cast<std::uint16_t>(work.Actions[index]);
            payload.TextId = work.TextId;
            payload.ChunkIndex = static_cast<std::uint16_t>(index);
            payload.ChunkCount = static_cast<std::uint16_t>(work.Actions.size());
            payload.ByteCount = static_cast<std::uint16_t>(std::min<std::size_t>(
                GameplayBridge::kGameplayTextBytesPerChunk, work.Text.size() - offset));
            std::memcpy(payload.Utf8Bytes, work.Text.data() + offset, payload.ByteCount);
        } else {
            auto payload = work.Payloads[index];
            payload.TargetHandle = commandHandle;
            payload.TargetLocalFormId = commandLocalFormId;
            payload.Domain = static_cast<std::uint16_t>(work.Domain);
            payload.Action = static_cast<std::uint16_t>(work.Actions[index]);
            command.Payload.ApplyGameplayAction = payload;
        }
        commands.push_back(command);
    }
    // Once the batch enters the bridge, retaining this semantic work is the
    // only safe outcome until every owned result has been resolved.
    work.Terminal = true;
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommandBatch(commands.data(), commands.size())) {
        work.Terminal = false;
        return false;
    }
    work.Admitted = true;

    const auto firstActionId = commands.front().Header.Identity.ActionId;
    if (firstActionId == 0 || commands.back().Header.Identity.ActionId !=
                                  firstActionId + static_cast<std::uint64_t>(commands.size() - 1)) {
        work.AwaitingResult = false;
        RetireReliableGameplayWork(work.WorkId);
        return false;
    }
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const auto actionId = commands[index].Header.Identity.ActionId;
        const auto [owner, inserted] = m_gameplayResultOwners.emplace(actionId, GameplayResultOwner{
            work.WorkId, commands[index].Header.Identity, targetHandle, targetLocalFormId,
            work.Domain, work.Actions[index], static_cast<std::uint16_t>(index)});
        TP_UNUSED(owner);
        if (!inserted) {
            std::erase_if(m_gameplayResultOwners, [workId = work.WorkId](const auto& acEntry) noexcept {
                return acEntry.second.WorkId == workId;
            });
            work.AwaitingResult = false;
            RetireReliableGameplayWork(work.WorkId);
            return false;
        }
    }
    if (!work.AcceptanceCommitted && !CommitAccept(work.Acceptance)) {
        std::erase_if(m_gameplayResultOwners, [workId = work.WorkId](const auto& acEntry) noexcept {
            return acEntry.second.WorkId == workId;
        });
        work.AwaitingResult = false;
        RetireReliableGameplayWork(work.WorkId);
        return false;
    }
    work.AcceptanceCommitted = true;
    work.Terminal = false;
    work.AwaitingResult = true;
    work.ResultWaitElapsed = 0.0;
    work.NextResultIndex = 0;
    return true;
}
catch (...)
{
    for (std::size_t index = 0; index < m_pendingGameplayWork.size(); ++index) {
        const auto& work = m_pendingGameplayWork[index];
        if (work.Admitted && work.Terminal) {
            RetireReliableGameplayWork(work.WorkId);
            break;
        }
    }
    return false;
}

void VRActorReplicationService::ForgetReliableGameplayWork(const std::uint64_t aWorkId) noexcept
{
    std::erase_if(m_gameplayResultOwners, [aWorkId](const auto& acEntry) noexcept {
        return acEntry.second.WorkId == aWorkId;
    });
    std::erase_if(m_pendingGameplayWork, [aWorkId](const PendingGameplayWork& acWork) noexcept {
        return acWork.WorkId == aWorkId;
    });
}

void VRActorReplicationService::RetireReliableGameplayWork(const std::uint64_t aWorkId) noexcept
{
    const auto work = std::find_if(m_pendingGameplayWork.begin(), m_pendingGameplayWork.end(),
        [aWorkId](const PendingGameplayWork& acWork) noexcept { return acWork.WorkId == aWorkId; });
    if (work == m_pendingGameplayWork.end())
        return;
    if (work->Admitted && !RememberSemanticTombstone(work->Acceptance, work->AcceptanceCommitted)) {
        // Do not evict a retained semantic identity. The requested epoch
        // rebase clears this terminal entry as one lifecycle operation.
        work->AwaitingResult = false;
        work->Terminal = true;
        return;
    }
    ForgetReliableGameplayWork(aWorkId);
}

void VRActorReplicationService::ForgetReliableGameplayWorkForPlayer(const std::uint32_t aPlayerId) noexcept
{
    std::vector<std::uint64_t> workIds;
    for (const auto& work : m_pendingGameplayWork) {
        if (work.TargetIsPlayer && work.PlayerId == aPlayerId)
            workIds.push_back(work.WorkId);
    }
    for (const auto workId : workIds)
        ForgetReliableGameplayWork(workId);
}

void VRActorReplicationService::ForgetReliableGameplayWorkForServer(const std::uint32_t aServerId) noexcept
{
    std::vector<std::uint64_t> workIds;
    for (const auto& work : m_pendingGameplayWork) {
        if (!work.TargetIsPlayer && work.ServerId == aServerId)
            workIds.push_back(work.WorkId);
    }
    for (const auto workId : workIds)
        ForgetReliableGameplayWork(workId);
}

bool VRActorReplicationService::ApplyForPlayer(const std::uint32_t aPlayerId, const GameplayBridge::GameplayDomain aDomain,
                                                const GameplayBridge::GameplayAction aAction,
                                                const GameplayBridge::GameplayActionPayload& acPayload,
                                                std::uint64_t* const apActionId) noexcept
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
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command))
        return false;
    if (apActionId)
        *apActionId = command.Header.Identity.ActionId;
    return true;
}

bool VRActorReplicationService::ApplyForServer(const std::uint32_t aServerId, const GameplayBridge::GameplayDomain aDomain,
                                                const GameplayBridge::GameplayAction aAction,
                                                const GameplayBridge::GameplayActionPayload& acPayload) noexcept try
{
    if (m_recordingSpawnServerId == aServerId && m_spawnActionOwners.size() >= kMaximumSpawnActionOwners)
        return false;
    GameplayBridge::CommandRecord command{};
    if (!BuildGameplayCommandForServerActor(aServerId, aDomain, aAction, command))
        return false;
    const auto target = command.Payload.ApplyGameplayAction.TargetHandle;
    const auto targetLocalFormId = command.Payload.ApplyGameplayAction.TargetLocalFormId;
    command.Payload.ApplyGameplayAction = acPayload;
    command.Payload.ApplyGameplayAction.TargetHandle = target;
    command.Payload.ApplyGameplayAction.TargetLocalFormId = targetLocalFormId;
    command.Payload.ApplyGameplayAction.Domain = static_cast<std::uint16_t>(aDomain);
    command.Payload.ApplyGameplayAction.Action = static_cast<std::uint16_t>(aAction);
    const auto submitted = SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command);
    if (submitted && m_recordingSpawnServerId == aServerId && command.Header.Identity.ActionId != 0)
        m_spawnActionOwners[command.Header.Identity.ActionId] = {
            aServerId, 1, 0.0, command.Header.Identity, command.Payload.ApplyGameplayAction.TargetHandle,
            aDomain, aAction};
    return submitted;
} catch (...) {
    return false;
}

bool VRActorReplicationService::ApplyForTarget(const std::uint32_t aServerId, const GameplayBridge::GameplayDomain aDomain,
                                                const GameplayBridge::GameplayAction aAction,
                                                const GameplayBridge::GameplayActionPayload& acPayload) noexcept
{
    GameplayBridge::CommandRecord command{};
    if (!BuildGameplayCommandForServerActor(aServerId, aDomain, aAction, command))
        return false;

    const auto target = command.Payload.ApplyGameplayAction.TargetHandle;
    const auto targetLocalFormId = command.Payload.ApplyGameplayAction.TargetLocalFormId;
    command.Payload.ApplyGameplayAction = acPayload;
    command.Payload.ApplyGameplayAction.TargetHandle = target;
    command.Payload.ApplyGameplayAction.TargetLocalFormId = targetLocalFormId;
    command.Payload.ApplyGameplayAction.Domain = static_cast<std::uint16_t>(aDomain);
    command.Payload.ApplyGameplayAction.Action = static_cast<std::uint16_t>(aAction);
    return SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command);
}

bool VRActorReplicationService::BuildGameplayCommandForServerActor(
    const std::uint32_t aServerId, const GameplayBridge::GameplayDomain aDomain,
    const GameplayBridge::GameplayAction aAction, GameplayBridge::CommandRecord& arCommand) const noexcept
{
    if (aServerId == 0)
        return false;

    const auto localServerId = m_avatars.GetLocalServerId();
    if (aServerId == localServerId)
        return m_avatars.BuildLocalGameplayCommand(aDomain, aAction, arCommand);

    if (m_avatars.GetRemoteAvatarHandleForServerId(aServerId).Value != 0)
        return m_avatars.BuildRemoteGameplayCommandForServerId(aServerId, aDomain, aAction, arCommand);

    const auto localReferenceFormId = m_npcOwnership.GetLocalReferenceForOwnedServerId(aServerId);
    return localReferenceFormId != 0 &&
           m_avatars.BuildLocalNativeGameplayCommandForServerId(
               aServerId, localReferenceFormId, aDomain, aAction, arCommand);
}

bool VRActorReplicationService::TryResolveMagicActor(
    const std::uint32_t aServerId, MagicActorReference& arReference) const noexcept
{
    arReference = {};
    if (aServerId == 0)
        return false;

    if (aServerId == m_avatars.GetLocalServerId()) {
        arReference.LocalReferenceFormId = kLocalPlayerReferenceFormId;
        return true;
    }

    arReference.Handle = m_avatars.GetRemoteAvatarHandleForServerId(aServerId);
    if (arReference.Handle.Value != 0)
        return true;

    arReference.LocalReferenceFormId = m_npcOwnership.GetLocalReferenceForOwnedServerId(aServerId);
    return arReference.LocalReferenceFormId != 0;
}

bool VRActorReplicationService::ApplyTextForPlayer(
    const std::uint32_t aPlayerId, const GameplayBridge::GameplayDomain aDomain,
    const GameplayBridge::GameplayAction aAction, const std::uint64_t aTextId,
    const std::string_view acText, GameplayBridge::BridgeIdentity* const apIdentity,
    GameplayBridge::AdapterHandle* const apTargetHandle,
    std::uint16_t* const apResultCount) noexcept try
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
    const auto recordingSpawnServerId = m_recordingSpawnServerId;
    const auto recordsSpawn = recordingSpawnServerId != 0 &&
                              PlayerForServer(recordingSpawnServerId) == aPlayerId;
    if (recordsSpawn &&
        m_spawnActionOwners.size() > kMaximumSpawnActionOwners - chunkCount)
        return false;
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
    if (recordsSpawn) {
        const auto firstActionId = commands.front().Header.Identity.ActionId;
        if (firstActionId == 0 || commands.back().Header.Identity.ActionId !=
                                      firstActionId + static_cast<std::uint64_t>(commands.size() - 1)) {
            ForgetSpawnActionIds(recordingSpawnServerId);
            m_pendingSpawns.erase(recordingSpawnServerId);
            RememberBoundedServerId(m_quarantinedSpawns, recordingSpawnServerId);
            return true;
        }
        for (const auto& command : commands) {
            m_spawnActionOwners[command.Header.Identity.ActionId] = {
                recordingSpawnServerId, 1, 0.0, command.Header.Identity,
                command.Payload.ApplyGameplayTextChunk.TargetHandle, aDomain, aAction};
        }
    }
    if (apIdentity)
        *apIdentity = commands.front().Header.Identity;
    if (apTargetHandle)
        *apTargetHandle = commands.front().Payload.ApplyGameplayTextChunk.TargetHandle;
    if (apResultCount)
        *apResultCount = static_cast<std::uint16_t>(commands.size());
    return true;
} catch (...) {
    return false;
}

bool VRActorReplicationService::ApplyTextForServer(
    const std::uint32_t aServerId, const GameplayBridge::GameplayDomain aDomain,
    const GameplayBridge::GameplayAction aAction, const std::uint64_t aTextId,
    const std::string_view acText) noexcept try
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
    if (m_recordingSpawnServerId == aServerId &&
        m_spawnActionOwners.size() > kMaximumSpawnActionOwners - chunkCount)
        return false;
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
        for (const auto& command : commands) {
            if (command.Header.Identity.ActionId == 0)
                return false;
            m_spawnActionOwners[command.Header.Identity.ActionId] = {
                aServerId, 1, 0.0, command.Header.Identity,
                command.Payload.ApplyGameplayTextChunk.TargetHandle, aDomain, aAction};
        }
    }
    return true;
} catch (...) {
    return false;
}

std::uint32_t VRActorReplicationService::PlayerForServer(const std::uint32_t aServerId) const noexcept
{
    const auto it = m_serverPlayers.find(aServerId);
    return it != m_serverPlayers.end() ? it->second : 0;
}

bool VRActorReplicationService::HasInventoryTransactionCapability() const noexcept
{
    return m_transport.IsOnline() && !m_transport.IsGameplayCleanupRequired() &&
           SkyrimTogetherVR::GameplayBridgeClient::IsReady() &&
           SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() != 0 &&
           GameplayBridge::HasCapability(SkyrimTogetherVR::GameplayBridgeClient::GetActiveCapabilities(),
                                         GameplayBridge::Capability::InventoryStackTransactions);
}

bool VRActorReplicationService::QueueInventoryTransaction(
    const std::uint32_t aServerId, const std::vector<Inventory::Entry>& acEntries,
    const std::vector<std::uint8_t>& acDrops, const bool aReset,
    const bool aSpawnInventory, const AcceptanceToken* const apAcceptance) noexcept try
{
    if (aServerId == 0 || acEntries.size() > GameplayBridge::kMaximumInventoryTransactionItems ||
        acDrops.size() != acEntries.size() || (!aReset && acEntries.empty()))
        return false;
    std::size_t totalEffects{};
    for (std::size_t index = 0; index < acEntries.size(); ++index) {
        const auto& entry = acEntries[index];
        const bool hasEnchantment = entry.ExtraEnchantId.BaseId != 0;
        const bool dynamicEnchantment = hasEnchantment &&
            entry.ExtraEnchantId.ModId == (std::numeric_limits<std::uint32_t>::max)();
        if (!entry.IsValidMutation() || (aReset && entry.Count < 0) || acDrops[index] > 1 ||
            (acDrops[index] != 0 && (aReset || entry.Count >= 0)) ||
            (!dynamicEnchantment && !entry.EnchantData.Effects.empty()) ||
            (dynamicEnchantment && entry.EnchantData.Effects.empty()) ||
            entry.EnchantData.Effects.size() > GameplayBridge::kMaximumInventoryTransactionEffects ||
            totalEffects > GameplayBridge::kMaximumInventoryTransactionEffects - entry.EnchantData.Effects.size() ||
            ToLocal(m_world, entry.BaseId) == 0 ||
            (hasEnchantment && !dynamicEnchantment && ToLocal(m_world, entry.ExtraEnchantId) == 0) ||
            (entry.ExtraPoisonId && ToLocal(m_world, entry.ExtraPoisonId) == 0))
            return false;
        for (const auto& effect : entry.EnchantData.Effects) {
            if (ToLocal(m_world, effect.EffectId) == 0)
                return false;
        }
        totalEffects += entry.EnchantData.Effects.size();
    }
    if (2 + acEntries.size() * 2 + totalEffects > GameplayBridge::kMaximumInventoryTransactionRecords)
        return false;
    if (aSpawnInventory) {
        if (m_completedSpawnInventoryTransactions.contains(aServerId) ||
            m_failedSpawnInventoryTransactions.contains(aServerId) ||
            HasPendingSpawnInventoryTransaction(aServerId))
            return true;
    }
    if (apAcceptance && !apAcceptance->Valid)
        return false;
    if (apAcceptance) {
        const auto duplicate = std::find_if(m_pendingInventoryTransactions.begin(), m_pendingInventoryTransactions.end(),
            [this, apAcceptance](const PendingInventoryTransaction& acPending) noexcept {
                return acPending.HasAcceptance && IsSameAcceptance(acPending.Acceptance, *apAcceptance);
            });
        if (duplicate != m_pendingInventoryTransactions.end())
            return true;
    }
    if (m_pendingInventoryTransactions.size() >= kMaximumPendingInventoryTransactions)
        return false;

    PendingInventoryTransaction pending{};
    pending.ServerId = aServerId;
    pending.Entries = acEntries;
    pending.Drops = acDrops;
    pending.Reset = aReset;
    pending.SpawnInventory = aSpawnInventory;
    if (apAcceptance) {
        pending.Acceptance = *apAcceptance;
        pending.HasAcceptance = true;
    }
    m_pendingInventoryTransactions.push_back(std::move(pending));
    TP_UNUSED(TrySubmitInventoryTransaction(m_pendingInventoryTransactions.size() - 1));
    return true;
}
catch (...)
{
    return false;
}

bool VRActorReplicationService::TrySubmitInventoryTransaction(const std::size_t aIndex) noexcept try
{
    if (aIndex >= m_pendingInventoryTransactions.size())
        return false;
    auto& arPending = m_pendingInventoryTransactions[aIndex];
    if (arPending.Terminal || arPending.AwaitingResults || !HasInventoryTransactionCapability() || arPending.ServerId == 0 ||
        arPending.Entries.size() > GameplayBridge::kMaximumInventoryTransactionItems ||
        arPending.Drops.size() != arPending.Entries.size() || (!arPending.Reset && arPending.Entries.empty()))
        return false;
    if (!arPending.Admitted && arPending.HasAcceptance && !CanCommitAccept(arPending.Acceptance)) {
        if (arPending.Acceptance.Sequence != 0)
            return false;
        const auto refreshed = PrepareAccept(arPending.Acceptance.PlayerId, arPending.Acceptance.Domain, 0,
                                             arPending.Acceptance.Signature, arPending.Acceptance.Channel);
        if (!refreshed.Valid)
            return false;
        arPending.Acceptance = refreshed;
    }

    std::size_t totalEffects{};
    for (std::size_t index = 0; index < arPending.Entries.size(); ++index) {
        const auto& entry = arPending.Entries[index];
        const bool hasEnchantment = entry.ExtraEnchantId.BaseId != 0;
        const bool dynamicEnchantment = hasEnchantment &&
            entry.ExtraEnchantId.ModId == (std::numeric_limits<std::uint32_t>::max)();
        if (!entry.IsValidMutation() || (arPending.Reset && entry.Count < 0) ||
            arPending.Drops[index] > 1 ||
            (arPending.Drops[index] != 0 && (arPending.Reset || entry.Count >= 0)) ||
            (!dynamicEnchantment && !entry.EnchantData.Effects.empty()) ||
            (dynamicEnchantment && entry.EnchantData.Effects.empty()) ||
            entry.EnchantData.Effects.size() > GameplayBridge::kMaximumInventoryTransactionEffects ||
            totalEffects > GameplayBridge::kMaximumInventoryTransactionEffects - entry.EnchantData.Effects.size())
            return false;
        totalEffects += entry.EnchantData.Effects.size();
    }
    const auto recordCount = 2 + arPending.Entries.size() * 2 + totalEffects;
    if (recordCount > GameplayBridge::kMaximumInventoryTransactionRecords ||
        recordCount > GameplayBridge::kDefaultCommandRingCapacity)
        return false;

    GameplayBridge::CommandRecord base{};
    if (!BuildGameplayCommandForServerActor(arPending.ServerId, GameplayBridge::GameplayDomain::Inventory,
                                            GameplayBridge::GameplayAction::InventoryTransactionBegin, base))
        return false;
    const auto targetHandle = base.Payload.ApplyGameplayAction.TargetHandle;
    const auto targetLocalFormId = base.Payload.ApplyGameplayAction.TargetLocalFormId;
    if (targetHandle.Value == 0 && targetLocalFormId == 0)
        return false;

    std::vector<GameplayBridge::CommandRecord> commands;
    std::vector<GameplayBridge::GameplayAction> expectedActions;
    commands.reserve(recordCount);
    expectedActions.reserve(recordCount);
    const auto append = [&base, targetHandle, targetLocalFormId, &commands, &expectedActions](
                            const GameplayBridge::GameplayAction aAction,
                            const GameplayBridge::GameplayActionPayload& acPayload) {
        GameplayBridge::CommandRecord command{};
        command.Header = base.Header;
        auto payload = acPayload;
        payload.TargetHandle = targetHandle;
        payload.TargetLocalFormId = targetLocalFormId;
        payload.Domain = static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Inventory);
        payload.Action = static_cast<std::uint16_t>(aAction);
        command.Payload.ApplyGameplayAction = payload;
        commands.push_back(command);
        expectedActions.push_back(aAction);
    };

    auto payload = Payload();
    payload.ValueA = static_cast<std::int32_t>(arPending.Entries.size());
    payload.ActionFlags = arPending.Reset ? GameplayBridge::kInventoryTransactionReset : 0;
    append(GameplayBridge::GameplayAction::InventoryTransactionBegin, payload);
    for (std::size_t itemIndex = 0; itemIndex < arPending.Entries.size(); ++itemIndex) {
        const auto& entry = arPending.Entries[itemIndex];
        const auto baseFormId = ToLocal(m_world, entry.BaseId);
        if (baseFormId == 0)
            return false;

        payload = Payload();
        payload.LocalFormIdA = baseFormId;
        payload.LocalFormIdB = static_cast<std::uint32_t>(arPending.Entries.size());
        payload.ValueA = entry.Count;
        payload.ValueB = static_cast<std::int32_t>(itemIndex);
        payload.ActionFlags =
            (entry.IsQuestItem ? GameplayBridge::kInventoryTransactionQuestItem : 0u) |
            (entry.ExtraWorn ? GameplayBridge::kInventoryTransactionWorn : 0u) |
            (entry.ExtraWornLeft ? GameplayBridge::kInventoryTransactionWornLeft : 0u) |
            ((entry.EquipmentFlags & Inventory::Entry::kEquipmentWeapon) != 0 ?
                 GameplayBridge::kInventoryTransactionWeapon : 0u) |
            ((entry.EquipmentFlags & Inventory::Entry::kEquipmentAmmo) != 0 ?
                 GameplayBridge::kInventoryTransactionAmmo : 0u) |
            (arPending.Drops[itemIndex] != 0 ? GameplayBridge::kInventoryDrop : 0u);
        append(GameplayBridge::GameplayAction::InventoryTransactionItem, payload);

        const bool hasEnchantment = entry.ExtraEnchantId.BaseId != 0;
        const bool dynamicEnchantment = hasEnchantment &&
            entry.ExtraEnchantId.ModId == (std::numeric_limits<std::uint32_t>::max)();
        const auto enchantmentFormId = !hasEnchantment ? 0u :
            dynamicEnchantment ? GameplayBridge::kInventoryTransactionDynamicEnchantmentFormId :
                                 ToLocal(m_world, entry.ExtraEnchantId);
        const auto poisonFormId = ToLocal(m_world, entry.ExtraPoisonId);
        if ((hasEnchantment && enchantmentFormId == 0) || (entry.ExtraPoisonId && poisonFormId == 0))
            return false;

        payload = Payload();
        payload.LocalFormIdA = enchantmentFormId;
        payload.LocalFormIdB = poisonFormId;
        payload.LocalFormIdC = static_cast<std::uint32_t>(entry.ExtraSoulLevel);
        payload.LocalFormIdD = static_cast<std::uint32_t>(entry.EnchantData.Effects.size());
        payload.ValueA = entry.ExtraEnchantCharge;
        payload.ValueB = static_cast<std::int32_t>(entry.ExtraPoisonCount);
        payload.ScalarA = entry.ExtraCharge;
        payload.ScalarB = entry.ExtraHealth;
        payload.ActionFlags = hasEnchantment ?
            (entry.ExtraEnchantRemoveUnequip ? GameplayBridge::kInventoryTransactionEnchantRemoveUnequip : 0u) |
            (entry.EnchantData.IsWeapon ? GameplayBridge::kInventoryTransactionEnchantIsWeapon : 0u) : 0u;
        append(GameplayBridge::GameplayAction::InventoryTransactionItemExtra, payload);

        for (std::size_t effectIndex = 0; effectIndex < entry.EnchantData.Effects.size(); ++effectIndex) {
            const auto& effect = entry.EnchantData.Effects[effectIndex];
            const auto effectFormId = ToLocal(m_world, effect.EffectId);
            if (effectFormId == 0)
                return false;
            payload = Payload();
            payload.LocalFormIdA = effectFormId;
            payload.LocalFormIdB = static_cast<std::uint32_t>(itemIndex);
            payload.LocalFormIdC = static_cast<std::uint32_t>(effectIndex);
            payload.LocalFormIdD = static_cast<std::uint32_t>(entry.EnchantData.Effects.size());
            payload.ValueA = effect.Area;
            payload.ValueB = effect.Duration;
            payload.ScalarA = effect.Magnitude;
            payload.ScalarB = effect.RawCost;
            append(GameplayBridge::GameplayAction::InventoryTransactionItemEffect, payload);
        }
    }
    append(GameplayBridge::GameplayAction::InventoryTransactionEnd, Payload());
    if (commands.size() != recordCount || expectedActions.size() != recordCount)
        return false;

    std::vector<std::uint8_t> resultStates(recordCount, 0);
    arPending.Terminal = true;
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommandBatch(commands.data(), commands.size())) {
        arPending.Terminal = false;
        return false;
    }
    arPending.Admitted = true;

    const auto firstActionId = commands.front().Header.Identity.ActionId;
    const auto endActionId = commands.back().Header.Identity.ActionId;
    if (firstActionId == 0 || endActionId == 0 ||
        endActionId != firstActionId + static_cast<std::uint64_t>(commands.size() - 1)) {
        // Admission occurred but result ownership cannot be established.
        // Quarantine this semantic transaction rather than replaying it.
        TerminalizeInventoryTransaction(aIndex);
        return false;
    }

    arPending.FirstActionId = firstActionId;
    arPending.EndActionId = endActionId;
    arPending.Identity = commands.front().Header.Identity;
    arPending.TargetHandle = targetHandle;
    arPending.TargetLocalFormId = targetLocalFormId;
    arPending.ExpectedActions = std::move(expectedActions);
    arPending.ResultStates = std::move(resultStates);
    arPending.RetryWaitElapsed = 0.0;
    arPending.ResultWaitElapsed = 0.0;
    arPending.HadFailure = false;
    arPending.AwaitingResults = true;
    if (arPending.HasAcceptance && !CommitAccept(arPending.Acceptance)) {
        // Submission succeeded but the ledger changed unexpectedly. Retire
        // the semantic payload immediately; replay would be unsafe.
        TerminalizeInventoryTransaction(aIndex);
        return false;
    }
    arPending.AcceptanceCommitted = arPending.HasAcceptance;
    arPending.Terminal = false;
    return true;
}
catch (...)
{
    if (aIndex < m_pendingInventoryTransactions.size() &&
        m_pendingInventoryTransactions[aIndex].Admitted)
        TerminalizeInventoryTransaction(aIndex);
    return false;
}

void VRActorReplicationService::CompleteInventoryTransaction(const std::size_t aIndex,
                                                              const bool aSucceeded) noexcept try
{
    if (aIndex >= m_pendingInventoryTransactions.size())
        return;
    auto& pending = m_pendingInventoryTransactions[aIndex];
    if (pending.Admitted && pending.HasAcceptance &&
        !RememberSemanticTombstone(pending.Acceptance, pending.AcceptanceCommitted)) {
        m_pendingInventoryTransactions[aIndex].AwaitingResults = false;
        m_pendingInventoryTransactions[aIndex].Terminal = true;
        return;
    }
    if (aSucceeded && pending.TargetHandle.Value == 0 && pending.TargetLocalFormId != 0)
        m_npcOwnership.CommitRemoteInventoryTransaction(pending.ServerId, pending.Entries, pending.Reset);
    if (pending.SpawnInventory) {
        if (aSucceeded)
            RememberBoundedServerId(m_completedSpawnInventoryTransactions, pending.ServerId);
        else
            RememberBoundedServerId(m_failedSpawnInventoryTransactions, pending.ServerId);
    }
    m_pendingInventoryTransactions.erase(m_pendingInventoryTransactions.begin() +
                                         static_cast<std::ptrdiff_t>(aIndex));
}
catch (...)
{
    if (aIndex < m_pendingInventoryTransactions.size()) {
        RequestSemanticTombstoneRebase();
        TerminalizeInventoryTransaction(aIndex);
    }
}

void VRActorReplicationService::TerminalizeInventoryTransaction(const std::size_t aIndex) noexcept
{
    if (aIndex >= m_pendingInventoryTransactions.size())
        return;
    auto& pending = m_pendingInventoryTransactions[aIndex];
    if (pending.Admitted && pending.HasAcceptance &&
        !RememberSemanticTombstone(pending.Acceptance, pending.AcceptanceCommitted)) {
        m_pendingInventoryTransactions[aIndex].HadFailure = true;
        m_pendingInventoryTransactions[aIndex].AwaitingResults = false;
        m_pendingInventoryTransactions[aIndex].Terminal = true;
        m_pendingInventoryTransactions[aIndex].ResultWaitElapsed = 0.0;
        return;
    }
    pending.HadFailure = true;
    if (pending.SpawnInventory)
        RememberBoundedServerId(m_failedSpawnInventoryTransactions, pending.ServerId);
    m_pendingInventoryTransactions.erase(m_pendingInventoryTransactions.begin() +
                                         static_cast<std::ptrdiff_t>(aIndex));
}

void VRActorReplicationService::ForgetInventoryTransactions(const std::uint32_t aServerId) noexcept
{
    std::erase_if(m_pendingInventoryTransactions, [aServerId](const PendingInventoryTransaction& acPending) noexcept {
        return acPending.ServerId == aServerId;
    });
    m_completedSpawnInventoryTransactions.erase(aServerId);
    m_failedSpawnInventoryTransactions.erase(aServerId);
}

bool VRActorReplicationService::HasPendingSpawnInventoryTransaction(const std::uint32_t aServerId) const noexcept
{
    return std::any_of(m_pendingInventoryTransactions.begin(), m_pendingInventoryTransactions.end(),
                       [aServerId](const PendingInventoryTransaction& acPending) noexcept {
                           return acPending.ServerId == aServerId && acPending.SpawnInventory;
                       });
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
        header.Identity.SequenceId != 0 || header.Identity.ActionId == 0 ||
        GameplayBridge::IsNpcSnapshotActionId(header.Identity.ActionId))
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

VRActorReplicationService::LocalActorActionTransaction*
VRActorReplicationService::GetOrCreateLocalActorAction(const std::uint64_t aActionId) noexcept try
{
    if (const auto existing = m_localActorActions.find(aActionId); existing != m_localActorActions.end())
        return &existing->second;

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
    return &created->second;
}
catch (...)
{
    return nullptr;
}

bool VRActorReplicationService::BuildLocalActorAction(
    const LocalActorActionTransaction& acTransaction, ActionEvent& arAction) const noexcept try
{
    if (!acTransaction.HasMetadata || !acTransaction.Snapshot.IsComplete() ||
        acTransaction.TextChunkCount == 0 ||
        acTransaction.TextReceived.count() != acTransaction.TextChunkCount ||
        acTransaction.Metadata.SnapshotId == 0 ||
        acTransaction.Metadata.TextId != acTransaction.Metadata.SnapshotId)
        return false;

    std::string text;
    for (std::uint16_t index = 0; index < acTransaction.TextChunkCount; ++index)
        text.append(acTransaction.TextChunks[index].data(), acTransaction.TextLengths[index]);
    const auto separator = text.find('\0');
    if (separator == std::string::npos || text.find('\0', separator + 1) != std::string::npos ||
        separator > kMaximumActorActionStringBytes ||
        text.size() - separator - 1 > kMaximumActorActionStringBytes)
        return false;

    ActionEvent action{};
    action.Tick = m_world.GetTick();
    action.State1 = acTransaction.Metadata.State1;
    action.State2 = acTransaction.Metadata.State2;
    action.Type = acTransaction.Metadata.Type;
    if (!m_world.GetModSystem().GetServerModId(acTransaction.Metadata.ActionLocalFormId, action.ActionId) ||
        !action.ActionId ||
        (acTransaction.Metadata.ActionTargetLocalFormId != 0 &&
         (!m_world.GetModSystem().GetServerModId(
              acTransaction.Metadata.ActionTargetLocalFormId, action.TargetId) ||
          !action.TargetId)) ||
        (acTransaction.Metadata.IdleLocalFormId != 0 &&
         (!m_world.GetModSystem().GetServerModId(acTransaction.Metadata.IdleLocalFormId, action.IdleId) ||
          !action.IdleId)))
        return false;

    action.EventName = TiltedPhoques::String{text.data(), separator};
    action.TargetEventName = TiltedPhoques::String{
        text.data() + separator + 1, text.size() - separator - 1};
    action.Variables.Booleans.resize(acTransaction.Snapshot.Booleans.size());
    action.Variables.Floats.resize(acTransaction.Snapshot.Floats.size());
    action.Variables.Integers.resize(acTransaction.Snapshot.Integers.size());
    for (std::size_t index = 0; index < acTransaction.Snapshot.Booleans.size(); ++index)
        action.Variables.Booleans[index] = acTransaction.Snapshot.Booleans[index];
    for (std::size_t index = 0; index < acTransaction.Snapshot.Floats.size(); ++index)
        action.Variables.Floats[index] = acTransaction.Snapshot.Floats[index];
    for (std::size_t index = 0; index < acTransaction.Snapshot.Integers.size(); ++index)
        action.Variables.Integers[index] =
            std::bit_cast<std::uint32_t>(acTransaction.Snapshot.Integers[index]);
    arAction = std::move(action);
    return true;
}
catch (...)
{
    return false;
}

bool VRActorReplicationService::TryGetLatestLocalActorAction(ActionEvent& arAction) const noexcept
{
    const LocalActorActionTransaction* latest{};
    for (const auto& [actionId, transaction] : m_localActorActions) {
        if (transaction.TargetHandle.Value != GameplayBridge::kLocalPlayerHandle.Value ||
            transaction.ActorLocalFormId != 0x14 ||
            !transaction.HasMetadata ||
            transaction.Metadata.SnapshotId != actionId ||
            transaction.Metadata.TextId != actionId ||
            !transaction.Snapshot.IsComplete() ||
            transaction.TextChunkCount == 0 ||
            transaction.TextReceived.count() != transaction.TextChunkCount ||
            (latest && transaction.Order <= latest->Order))
            continue;
        latest = &transaction;
    }
    return latest && BuildLocalActorAction(*latest, arAction);
}

bool VRActorReplicationService::TryGetLatestLocalActorAction(
    const std::uint32_t aActorLocalFormId, ActionEvent& arAction) const noexcept
{
    if (aActorLocalFormId == 0)
        return false;

    const LocalActorActionTransaction* latest{};
    for (const auto& [actionId, transaction] : m_localActorActions) {
        if (transaction.TargetHandle.Value != 0 || transaction.ActorLocalFormId != aActorLocalFormId ||
            !transaction.HasMetadata || transaction.Metadata.SnapshotId != actionId ||
            transaction.Metadata.TextId != actionId || !transaction.Snapshot.IsComplete() ||
            transaction.TextChunkCount == 0 || transaction.TextReceived.count() != transaction.TextChunkCount ||
            (latest && transaction.Order <= latest->Order))
            continue;
        latest = &transaction;
    }
    return latest && BuildLocalActorAction(*latest, arAction);
}

bool VRActorReplicationService::TryCommitLocalActorAction(const GameplayBridge::EventRecord& acRecord) noexcept try
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

    std::uint32_t serverId{};
    if (transaction.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value)
        serverId = m_avatars.GetLocalServerId();
    else if (transaction.TargetHandle.Value == 0)
        serverId = m_npcOwnership.GetServerIdForLocalReference(transaction.ActorLocalFormId);
    if (serverId == 0)
        return false;

    ActionEvent action{};
    if (!BuildLocalActorAction(transaction, action))
    {
        m_localActorActions.erase(transactionIt);
        return false;
    }
    action.ActorId = serverId;

    ClientActorActionRequest request{};
    request.ServerId = serverId;
    request.Action = std::move(action);
    if (!m_transport.Send(request))
        return false;
    m_localActorActions.erase(transactionIt);
    return true;
}
catch (...)
{
    return false;
}

bool VRActorReplicationService::HasHumanoidActorActionVariables(const ActionEvent& acAction) const noexcept
{
    const std::string_view eventName{acAction.EventName.c_str(), acAction.EventName.size()};
    const std::string_view targetEventName{acAction.TargetEventName.c_str(), acAction.TargetEventName.size()};
    return acAction.ActionId && (acAction.Type & ~0x7u) == 0 &&
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
                                                         const ActionEvent& acAction,
                                                         const std::uint8_t aAttempts) noexcept try
{
    if (!HasExactActorActionCapability() || aServerId == 0 || !HasHumanoidActorActionVariables(acAction))
        return false;
    if (IsKnownRemoteActorAction(aServerId, acAction))
        return true;

    const auto actionForm = ToLocal(m_world, acAction.ActionId);
    const auto targetForm = ToLocal(m_world, acAction.TargetId);
    const auto idleForm = ToLocal(m_world, acAction.IdleId);
    if (actionForm == 0 || (acAction.TargetId && targetForm == 0) || (acAction.IdleId && idleForm == 0))
        return false;

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
                                 const auto& acValues) {
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
    if (m_remoteActorActionOwners.size() >= kMaximumTrackedRemoteActorActions)
        return false;
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommandBatch(commands.data(), commands.size()))
        return false;
    const auto actionId = commands.back().Header.Identity.ActionId;
    if (actionId == 0)
        return false;
    const auto [owner, inserted] = m_remoteActorActionOwners.emplace(
        actionId, RemoteActorActionTracking{aServerId, acAction, commands.back().Header.Identity,
                                             target, 0.0, aAttempts});
    TP_UNUSED(owner);
    if (!inserted)
        return false;
    return true;
} catch (...) {
    return false;
}

bool VRActorReplicationService::SubmitLegacyRemoteActorAction(
    const std::uint32_t aServerId, const ActionEvent& acAction) noexcept
{
    if (aServerId == 0 || m_avatars.GetRemoteAvatarHandleForServerId(aServerId).Value == 0)
        return false;
    const std::string_view eventName{acAction.EventName.c_str()};
    if (eventName.empty() || eventName.size() > 127 ||
        !GameplayBridge::IsSupportedLegacyAnimationEvent(eventName))
        return false;
    auto textId = Signature(aServerId, acAction.Tick, acAction.ActionId.LogFormat(),
                            acAction.TargetId.LogFormat(), acAction.IdleId.LogFormat(),
                            acAction.State1, acAction.State2, acAction.Type);
    if (textId == 0)
        textId = 1;
    const auto acceptance = PrepareAccept(aServerId, GameplayBridge::GameplayDomain::Animation, 0, textId);
    return !acceptance.Valid || QueueReliableTextForServer(acceptance, aServerId,
        GameplayBridge::GameplayDomain::Animation, GameplayBridge::GameplayAction::AnimationEvent, textId, eventName);
}

void VRActorReplicationService::QueueRemoteActorAction(const std::uint32_t aServerId,
                                                        const ActionEvent& acAction,
                                                        const std::uint8_t aAttempts) noexcept try
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
    m_pendingRemoteActorActions.push_back(PendingRemoteActorAction{aServerId, acAction, aAttempts});
} catch (...) {
    spdlog::error("VR remote actor action retry staging failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

bool VRActorReplicationService::IsKnownRemoteActorAction(
    const std::uint32_t aServerId, const ActionEvent& acAction) const noexcept
{
    return std::any_of(m_remoteActorActionOwners.begin(), m_remoteActorActionOwners.end(),
                       [aServerId, &acAction](const auto& acEntry) noexcept {
                           return acEntry.second.ServerId == aServerId && acEntry.second.Action == acAction;
                       }) ||
           std::any_of(m_completedRemoteActorActions.begin(), m_completedRemoteActorActions.end(),
                       [aServerId, &acAction](const CompletedRemoteActorAction& acEntry) noexcept {
                           return acEntry.ServerId == aServerId && acEntry.Action == acAction;
                       });
}

void VRActorReplicationService::RememberCompletedRemoteActorAction(
    const std::uint32_t aServerId, const ActionEvent& acAction) noexcept try
{
    if (std::any_of(m_completedRemoteActorActions.begin(), m_completedRemoteActorActions.end(),
                    [aServerId, &acAction](const CompletedRemoteActorAction& acEntry) noexcept {
                        return acEntry.ServerId == aServerId && acEntry.Action == acAction;
                    }))
        return;
    if (m_completedRemoteActorActions.size() >= kMaximumTrackedRemoteActorActions)
        m_completedRemoteActorActions.erase(m_completedRemoteActorActions.begin());
    m_completedRemoteActorActions.push_back({aServerId, acAction});
}
catch (...)
{
    spdlog::error("VR actor action replay ledger update failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

void VRActorReplicationService::ForgetRemoteActorActions(const std::uint32_t aServerId) noexcept
{
    std::erase_if(m_pendingRemoteActorActions, [aServerId](const PendingRemoteActorAction& acPending) noexcept {
        return acPending.ServerId == aServerId;
    });
    std::erase_if(m_completedRemoteActorActions, [aServerId](const CompletedRemoteActorAction& acCompleted) noexcept {
        return acCompleted.ServerId == aServerId;
    });
    for (auto it = m_remoteActorActionOwners.begin(); it != m_remoteActorActionOwners.end();) {
        if (it->second.ServerId == aServerId)
            it = m_remoteActorActionOwners.erase(it);
        else
            ++it;
    }
}

void VRActorReplicationService::ForgetPlayer(const std::uint32_t aPlayerId) noexcept
{
    ForgetAppearanceApplication(aPlayerId);
    ForgetReliableGameplayWorkForPlayer(aPlayerId);
    ForgetSemanticTombstones(aPlayerId);
    m_appliedAppearanceSequences.erase(aPlayerId);
    m_failedAppearanceSequences.erase(aPlayerId);
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
            ForgetInventoryTransactions(serverId);
            ForgetSpawnActionIds(serverId);
            ForgetReliableGameplayWorkForServer(serverId);
            ForgetSemanticTombstones(serverId);
            ForgetRemoteActorActions(serverId);
            m_quarantinedSpawns.erase(serverId);
            m_pendingMagicEffects.erase(std::remove_if(m_pendingMagicEffects.begin(), m_pendingMagicEffects.end(),
                [serverId](const PendingMagicEffect& acPending) noexcept {
                    return acPending.Message.TargetId == serverId || acPending.Message.CasterId == serverId;
                }), m_pendingMagicEffects.end());
            m_pendingSpellCasts.erase(std::remove_if(m_pendingSpellCasts.begin(), m_pendingSpellCasts.end(),
                [serverId](const PendingSpellCast& acPending) noexcept {
                    return acPending.Message.CasterId == serverId || acPending.Message.DesiredTarget == serverId;
                }), m_pendingSpellCasts.end());
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
    const auto npcAppearanceKey = NpcAppearanceTargetKey(aServerId);
    ForgetAppearanceApplication(npcAppearanceKey);
    m_latestAppearances.erase(npcAppearanceKey);
    m_appliedAppearanceSequences.erase(npcAppearanceKey);
    m_failedAppearanceSequences.erase(npcAppearanceKey);
    const auto playerId = PlayerForServer(aServerId);
    if (playerId != 0) {
        ForgetAppearanceApplication(playerId);
        ForgetReliableGameplayWorkForPlayer(playerId);
        ForgetSemanticTombstones(playerId);
        m_ledgers.erase(playerId);
    }
    m_serverPlayers.erase(aServerId);
    m_pendingSpawns.erase(aServerId);
    m_spawnSnapshots.erase(aServerId);
    m_resyncAttempts.erase(aServerId);
    ForgetInventoryTransactions(aServerId);
    ForgetSpawnActionIds(aServerId);
    ForgetReliableGameplayWorkForServer(aServerId);
    ForgetSemanticTombstones(aServerId);
    ForgetRemoteActorActions(aServerId);
    m_lastEquipmentTransactionByServer.erase(aServerId);
    ForgetEquipmentApplication(aServerId);
    m_pendingMagicEffects.erase(std::remove_if(m_pendingMagicEffects.begin(), m_pendingMagicEffects.end(),
        [aServerId](const PendingMagicEffect& acPending) noexcept {
            return acPending.Message.TargetId == aServerId || acPending.Message.CasterId == aServerId;
        }), m_pendingMagicEffects.end());
    m_pendingSpellCasts.erase(std::remove_if(m_pendingSpellCasts.begin(), m_pendingSpellCasts.end(),
        [aServerId](const PendingSpellCast& acPending) noexcept {
            return acPending.Message.CasterId == aServerId || acPending.Message.DesiredTarget == aServerId;
        }), m_pendingSpellCasts.end());
    m_pendingMounts.erase(aServerId);
    std::erase_if(m_pendingMounts, [aServerId](const auto& acEntry) {
        return acEntry.second.MountServerId == aServerId;
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
    m_quarantinedSpawns.erase(aServerId);
}

void VRActorReplicationService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept
{
    ForgetServer(acMessage.ServerId);
}

void VRActorReplicationService::OnCharacterSpawn(const CharacterSpawnRequest& acMessage) noexcept try
{
    if (!acMessage.IsDecodedValid || acMessage.ServerId == 0 ||
        (acMessage.HasVRAppearance &&
         (!acMessage.InitialVRAppearance.IsValid() ||
          (!acMessage.IsPlayer && acMessage.InitialVRAppearance.TintCount != 0))))
        return;
    if (acMessage.IsPlayer && acMessage.PlayerId == m_transport.GetLocalPlayerId()) {
        m_localServerId = acMessage.ServerId;
        return;
    }
    if (acMessage.IsPlayer && !IsRemotePlayer(m_transport, acMessage.PlayerId))
        return;
    if (!m_spawnSnapshots.contains(acMessage.ServerId) &&
        m_spawnSnapshots.size() >= kMaximumTrackedSpawnStates) {
        const auto oldest = std::min_element(m_spawnSnapshots.begin(), m_spawnSnapshots.end(),
            [](const auto& acLeft, const auto& acRight) noexcept { return acLeft.first < acRight.first; });
        if (oldest != m_spawnSnapshots.end())
            ForgetServer(oldest->first);
    }
    if (!m_pendingSpawns.contains(acMessage.ServerId) &&
        m_pendingSpawns.size() >= kMaximumTrackedSpawnStates) {
        const auto oldest = std::min_element(m_pendingSpawns.begin(), m_pendingSpawns.end(),
            [](const auto& acLeft, const auto& acRight) noexcept { return acLeft.first < acRight.first; });
        if (oldest != m_pendingSpawns.end())
            ForgetServer(oldest->first);
    }
    m_serverPlayers[acMessage.ServerId] = acMessage.PlayerId;
    if (acMessage.HasVRAppearance) {
        const auto targetKey = acMessage.IsPlayer ? static_cast<std::uint64_t>(acMessage.PlayerId) :
                                                    NpcAppearanceTargetKey(acMessage.ServerId);
        if (targetKey == 0)
            return;
        if (const auto existing = m_latestAppearances.find(targetKey);
            existing == m_latestAppearances.end() ||
            IsNewer(acMessage.InitialVRAppearance.Sequence, existing->second.Sequence))
            m_latestAppearances[targetKey] = acMessage.InitialVRAppearance;
    }
    m_quarantinedSpawns.erase(acMessage.ServerId);
    ForgetSpawnActionIds(acMessage.ServerId);
    m_resyncAttempts.erase(acMessage.ServerId);
    m_spawnSnapshots[acMessage.ServerId] = acMessage;
    m_pendingSpawns[acMessage.ServerId] = acMessage;
}
catch (...)
{
}

void VRActorReplicationService::OnReferencesMove(const ServerReferencesMoveRequest& acMessage) noexcept
{
    if (!acMessage.IsDecodedValid)
        return;

    for (const auto& [serverId, update] : acMessage.Updates)
    {
        const auto playerId = PlayerForServer(serverId);
        const bool hasRemoteAvatar = m_avatars.GetRemoteAvatarHandleForServerId(serverId).Value != 0;
        std::uint32_t index{};
        for (const auto& action : update.ActionEvents)
        {
            if (HasHumanoidActorActionVariables(action) &&
                SkyrimTogether::Protocol::HasCapability(
                    m_transport.GetNegotiatedGameplayCapabilities(),
                    SkyrimTogether::Protocol::GameplayCapability::ExactAnimationActions)) {
                if (HasExactActorActionCapability()) {
                    if (!SubmitRemoteActorAction(serverId, action))
                        QueueRemoteActorAction(serverId, action);
                } else if (!SubmitLegacyRemoteActorAction(serverId, action)) {
                    QueueRemoteActorAction(serverId, action);
                }
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
            const auto acceptance = PrepareAccept(serverId, GameplayBridge::GameplayDomain::Animation, 0,
                                                  Signature(textId, action.Tick));
            if (acceptance.Valid)
                TP_UNUSED(QueueReliableTextForServer(acceptance, serverId, GameplayBridge::GameplayDomain::Animation,
                                                      GameplayBridge::GameplayAction::AnimationEvent, textId, eventName));
        }
    }
}

void VRActorReplicationService::OnUpdate(const UpdateEvent& acEvent) noexcept try
{
    const auto lifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    const auto reliableDelta = std::isfinite(acEvent.Delta) && acEvent.Delta > 0.0 ? acEvent.Delta : 0.0;
    if (lifecycleEpoch == 0 && m_observedLifecycleEpoch != 0)
    {
        m_pendingSpawns.clear();
        m_pendingMounts.clear();
        m_pendingMagicEffects.clear();
        m_pendingSpellCasts.clear();
        m_pendingGameplayWork.clear();
        m_gameplayResultOwners.clear();
        m_semanticTombstones.clear();
        m_resyncAttempts.clear();
        m_ledgers.clear();
        m_lastEquipmentTransactionByServer.clear();
        m_pendingEquipmentApplications.clear();
        m_equipmentActionOwners.clear();
        m_pendingInventoryTransactions.clear();
        m_completedSpawnInventoryTransactions.clear();
        m_failedSpawnInventoryTransactions.clear();
        m_quarantinedSpawns.clear();
        m_pendingAppearanceApplications.clear();
        m_appearanceActionOwners.clear();
        m_appliedAppearanceSequences.clear();
        m_failedAppearanceSequences.clear();
        m_localActorActions.clear();
        m_pendingRemoteActorActions.clear();
        m_remoteActorActionOwners.clear();
        m_completedRemoteActorActions.clear();
        m_spawnActionOwners.clear();
        m_recordingSpawnServerId = 0;
        m_semanticTombstoneRebaseRequested = false;
        m_semanticTombstoneRebaseEpoch = 0;
        m_semanticTombstoneRebaseElapsed = 0.0;
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
        m_pendingInventoryTransactions.clear();
        m_completedSpawnInventoryTransactions.clear();
        m_failedSpawnInventoryTransactions.clear();
        m_quarantinedSpawns.clear();
        m_pendingAppearanceApplications.clear();
        m_appearanceActionOwners.clear();
        m_appliedAppearanceSequences.clear();
        m_failedAppearanceSequences.clear();
        m_pendingMounts.clear();
        m_pendingMagicEffects.clear();
        m_pendingSpellCasts.clear();
        m_pendingGameplayWork.clear();
        m_gameplayResultOwners.clear();
        m_semanticTombstones.clear();
        m_resyncAttempts.clear();
        m_localActorActions.clear();
        m_pendingRemoteActorActions.clear();
        m_remoteActorActionOwners.clear();
        m_completedRemoteActorActions.clear();
        m_spawnActionOwners.clear();
        m_recordingSpawnServerId = 0;
        m_semanticTombstoneRebaseRequested = false;
        m_semanticTombstoneRebaseEpoch = 0;
        m_semanticTombstoneRebaseElapsed = 0.0;
        m_replayAfterLifecycleBoundary = true;
        m_observedLifecycleEpoch = lifecycleEpoch;
        return;
    }
    if (lifecycleEpoch != 0)
        m_observedLifecycleEpoch = lifecycleEpoch;
    if (m_semanticTombstoneRebaseRequested) {
        if (m_semanticTombstoneRebaseEpoch == 0 && lifecycleEpoch != 0)
            m_semanticTombstoneRebaseEpoch = lifecycleEpoch;
        if (lifecycleEpoch != 0 && m_semanticTombstoneRebaseEpoch != 0 &&
            lifecycleEpoch != m_semanticTombstoneRebaseEpoch) {
            m_semanticTombstones.clear();
            m_semanticTombstoneRebaseRequested = false;
            m_semanticTombstoneRebaseEpoch = 0;
            m_semanticTombstoneRebaseElapsed = 0.0;
        } else if (lifecycleEpoch != 0) {
            m_semanticTombstoneRebaseElapsed += reliableDelta;
            if (m_semanticTombstoneRebaseElapsed >= kSemanticTombstoneRebaseRetrySeconds) {
                m_semanticTombstoneRebaseElapsed = 0.0;
                TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
            }
        }
    }
    if (lifecycleEpoch != 0 && m_replayAfterLifecycleBoundary)
    {
        m_pendingSpawns = m_spawnSnapshots;
        m_replayAfterLifecycleBoundary = false;
    }

    m_localServerId = m_avatars.GetLocalServerId();
    for (std::size_t index = 0; index < m_pendingGameplayWork.size();) {
        const auto workId = m_pendingGameplayWork[index].WorkId;
        auto& work = m_pendingGameplayWork[index];
        if (work.Terminal) {
            ++index;
            continue;
        }
        if (work.AwaitingResult) {
            work.ResultWaitElapsed += reliableDelta;
            if (work.ResultWaitElapsed >= kGameplayResultTimeoutSeconds) {
                spdlog::warn("VR gameplay result timed out for semantic work {}; suppressing replay", workId);
                std::erase_if(m_gameplayResultOwners, [workId](const auto& acEntry) noexcept {
                    return acEntry.second.WorkId == workId;
                });
                work.AwaitingResult = false;
                RetireReliableGameplayWork(workId);
            }
            if (index < m_pendingGameplayWork.size() && m_pendingGameplayWork[index].WorkId == workId)
                ++index;
            continue;
        }
        if (!work.Admitted && !CanCommitAccept(work.Acceptance)) {
            if (work.Acceptance.Sequence == 0) {
                const auto refreshed = PrepareAccept(work.Acceptance.PlayerId, work.Acceptance.Domain, 0,
                                                     work.Acceptance.Signature, work.Acceptance.Channel);
                if (refreshed.Valid) {
                    work.Acceptance = refreshed;
                } else {
                    ForgetReliableGameplayWork(workId);
                    continue;
                }
            } else {
                ForgetReliableGameplayWork(workId);
                continue;
            }
        }
        work.AdmissionWaitElapsed += reliableDelta;
        if (work.AdmissionWaitElapsed >= kGameplayAdmissionTimeoutSeconds) {
            if (work.Admitted) {
                // A pre-mutation result permitted a retry, but a later retry
                // could not re-enter the bridge. The original semantic work
                // was still admitted, so retain its no-replay identity.
                RetireReliableGameplayWork(workId);
            } else {
                // The command never entered the bridge, so no semantic state
                // needs to survive this pre-admission timeout.
                ForgetReliableGameplayWork(workId);
            }
            continue;
        }
        if (work.AdmissionWaitElapsed >= kMagicActorRetryDelaySeconds) {
            work.AdmissionWaitElapsed = 0.0;
            TP_UNUSED(TrySubmitReliableGameplayWork(index));
        }
        if (index < m_pendingGameplayWork.size() && m_pendingGameplayWork[index].WorkId == workId)
            ++index;
    }
    if (std::isfinite(acEvent.Delta) && acEvent.Delta > 0.0) {
        for (auto it = m_remoteActorActionOwners.begin(); it != m_remoteActorActionOwners.end();) {
            auto& tracking = it->second;
            tracking.ResultWaitElapsed += acEvent.Delta;
            if (tracking.ResultWaitElapsed < kActorActionResultTimeoutSeconds) {
                ++it;
                continue;
            }

            const auto serverId = tracking.ServerId;
            const auto action = tracking.Action;
            it = m_remoteActorActionOwners.erase(it);
            RememberCompletedRemoteActorAction(serverId, action);
            spdlog::warn("VR actor action result timed out for server actor {}; suppressing ambiguous replay",
                         serverId);
        }

        std::unordered_set<std::uint32_t> timedOutSpawns;
        for (auto& [actionId, tracking] : m_spawnActionOwners) {
            TP_UNUSED(actionId);
            tracking.ResultWaitElapsed += acEvent.Delta;
            if (tracking.ResultWaitElapsed >= kSpawnResultTimeoutSeconds)
                timedOutSpawns.insert(tracking.ServerId);
        }
        for (const auto serverId : timedOutSpawns) {
            ForgetSpawnActionIds(serverId);
            m_pendingSpawns.erase(serverId);
            RememberBoundedServerId(m_quarantinedSpawns, serverId);
            spdlog::warn("VR spawn result timed out for server actor {}; quarantining ambiguous spawn", serverId);
        }
    }
    std::vector<std::uint32_t> expiredEquipment;
    for (auto& [serverId, pending] : m_pendingEquipmentApplications) {
        if (pending.AwaitingResult) {
            if (std::isfinite(acEvent.Delta) && acEvent.Delta > 0.0)
                pending.ResultWaitElapsed += acEvent.Delta;
            if (pending.ResultWaitElapsed < kEquipmentResultTimeoutSeconds)
                continue;
            expiredEquipment.push_back(serverId);
            continue;
        }
        if (!pending.AwaitingResult && pending.ResultFailures < kMaximumEquipmentResultFailures)
            TP_UNUSED(TrySubmitEquipmentApplication(serverId, pending));
    }
    for (const auto serverId : expiredEquipment)
        TerminalizeEquipmentApplication(serverId);
    for (std::size_t index = 0; index < m_pendingInventoryTransactions.size();) {
        auto& pending = m_pendingInventoryTransactions[index];
        if (pending.Terminal) {
            ++index;
            continue;
        }
        if (pending.AwaitingResults) {
            if (std::isfinite(acEvent.Delta) && acEvent.Delta > 0.0)
                pending.ResultWaitElapsed += acEvent.Delta;
            if (pending.ResultWaitElapsed < kInventoryTransactionResultTimeoutSeconds) {
                ++index;
                continue;
            }
            spdlog::warn("VR inventory transaction result timed out for server actor {}; suppressing ambiguous replay",
                         pending.ServerId);
            TerminalizeInventoryTransaction(index);
            continue;
        }
        if (std::isfinite(acEvent.Delta) && acEvent.Delta > 0.0)
            pending.RetryWaitElapsed += acEvent.Delta;
        if (pending.RetryWaitElapsed >= kInventoryTransactionRetryDelaySeconds) {
            pending.RetryWaitElapsed = 0.0;
            const auto pendingCount = m_pendingInventoryTransactions.size();
            TP_UNUSED(TrySubmitInventoryTransaction(index));
            if (m_pendingInventoryTransactions.size() != pendingCount)
                continue;
        }
        if (index < m_pendingInventoryTransactions.size())
            ++index;
    }
    for (auto it = m_pendingAppearanceApplications.begin(); it != m_pendingAppearanceApplications.end();) {
        const auto playerId = it->first;
        auto& pending = it->second;
        if (pending.AwaitingResult) {
            if (std::isfinite(acEvent.Delta) && acEvent.Delta > 0.0)
                pending.ResultWaitElapsed += acEvent.Delta;
            if (pending.ResultWaitElapsed >= kAppearanceResultTimeoutSeconds) {
                for (const auto actionId : pending.ActionIds)
                    m_appearanceActionOwners.erase(actionId);
                m_failedAppearanceSequences[playerId] = pending.Appearance.Sequence;
                it = m_pendingAppearanceApplications.erase(it);
                continue;
            }
        } else if (std::isfinite(acEvent.Delta) && acEvent.Delta > 0.0) {
            pending.RetryWaitElapsed += acEvent.Delta;
        }
        if (!pending.AwaitingResult &&
            (pending.ResultFailures >= kMaximumAppearanceResultFailures ||
             pending.SubmissionFailures >= kMaximumAppearanceSubmissionFailures)) {
            m_failedAppearanceSequences[playerId] = pending.Appearance.Sequence;
            it = m_pendingAppearanceApplications.erase(it);
            continue;
        }
        if (!pending.AwaitingResult &&
            pending.RetryWaitElapsed >= AppearanceRetryDelay(
                std::max(pending.SubmissionFailures, pending.ResultFailures))) {
            pending.RetryWaitElapsed = 0.0;
            if (!ApplyVRAppearance(playerId, pending.Appearance) &&
                pending.SubmissionFailures < kMaximumAppearanceSubmissionFailures)
                ++pending.SubmissionFailures;
        }
        ++it;
    }
    for (const auto& [targetKey, appearance] : m_latestAppearances) {
        if (IsNpcAppearanceTarget(targetKey) && m_pendingSpawns.contains(AppearanceTargetId(targetKey)))
            continue;
        if (m_pendingAppearanceApplications.contains(targetKey))
            continue;
        const auto applied = m_appliedAppearanceSequences.find(targetKey);
        if (applied != m_appliedAppearanceSequences.end() && applied->second == appearance.Sequence)
            continue;
        const auto failed = m_failedAppearanceSequences.find(targetKey);
        if (failed != m_failedAppearanceSequences.end() && failed->second == appearance.Sequence)
            continue;
        QueueVRAppearance(targetKey, appearance);
    }
    for (auto it = m_pendingSpawns.begin(); it != m_pendingSpawns.end();)
    {
        if (!SubmitSpawn(it->second))
        {
            ++it;
            continue;
        }
        const auto targetKey = it->second.IsPlayer ? static_cast<std::uint64_t>(it->second.PlayerId) :
                                                      NpcAppearanceTargetKey(it->second.ServerId);
        if (const auto appearance = m_latestAppearances.find(targetKey);
            appearance != m_latestAppearances.end())
            QueueVRAppearance(targetKey, appearance->second);
        it = m_pendingSpawns.erase(it);
    }
    for (auto it = m_pendingRemoteActorActions.begin(); it != m_pendingRemoteActorActions.end();) {
        const auto submitted = HasExactActorActionCapability() ?
            SubmitRemoteActorAction(it->ServerId, it->Action, it->Attempts) :
            SubmitLegacyRemoteActorAction(it->ServerId, it->Action);
        if (submitted)
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
    const auto retryDelta = std::isfinite(acEvent.Delta) && acEvent.Delta > 0.0 ? acEvent.Delta : 0.0;
    for (auto it = m_pendingSpellCasts.begin(); it != m_pendingSpellCasts.end();) {
        it->RetryElapsed += retryDelta;
        if (it->RetryElapsed < kMagicActorRetryDelaySeconds) {
            ++it;
            continue;
        }

        it->RetryElapsed = 0.0;
        const auto result = SubmitSpellCast(it->Message, it->Acceptance);
        if (result != SpellCastSubmitResult::AwaitingActor || ++it->Attempts >= kMaximumPendingMagicEffectAttempts)
            it = m_pendingSpellCasts.erase(it);
        else
            ++it;
    }
    for (auto it = m_pendingMagicEffects.begin(); it != m_pendingMagicEffects.end();) {
        it->RetryElapsed += retryDelta;
        if (it->RetryElapsed < kMagicActorRetryDelaySeconds) {
            ++it;
            continue;
        }

        it->RetryElapsed = 0.0;
        const auto result = SubmitMagicEffect(it->Message, it->Acceptance);
        if (result != MagicEffectSubmitResult::AwaitingActor || ++it->Attempts >= kMaximumPendingMagicEffectAttempts)
            it = m_pendingMagicEffects.erase(it);
        else
            ++it;
    }
    for (auto it = m_pendingMounts.begin(); it != m_pendingMounts.end();) {
        it->second.RetryElapsed += retryDelta;
        if (it->second.RetryElapsed < kMagicActorRetryDelaySeconds) {
            ++it;
            continue;
        }
        it->second.RetryElapsed = 0.0;
        if (TryApplyMount(it->first, it->second.MountServerId, it->second.Acceptance) ||
            ++it->second.Attempts >= kMaximumPendingMagicEffectAttempts)
            it = m_pendingMounts.erase(it);
        else
            ++it;
    }
}
catch (...)
{
}

bool VRActorReplicationService::SubmitSpawn(const CharacterSpawnRequest& acMessage) noexcept try
{
    GameplayBridge::CommandRecord probe{};
    if (!BuildGameplayCommandForServerActor(acMessage.ServerId, GameplayBridge::GameplayDomain::Inventory,
                                            GameplayBridge::GameplayAction::InventoryTransactionBegin, probe))
        return false;

    struct ScopedSpawnActionRecording
    {
        std::uint32_t& RecordingServerId;
        ~ScopedSpawnActionRecording() { RecordingServerId = 0; }
    } recording{m_recordingSpawnServerId};
    m_recordingSpawnServerId = acMessage.ServerId;
    bool submitted = true;

    if (m_quarantinedSpawns.contains(acMessage.ServerId))
        return true;

    // Initial inventory is an ordered prerequisite. A terminal transaction
    // failure leaves the spawn explicitly quarantined rather than allowing
    // dependent equip/draw work to make it look synchronized.
    if (m_failedSpawnInventoryTransactions.contains(acMessage.ServerId)) {
        RememberBoundedServerId(m_quarantinedSpawns, acMessage.ServerId);
        return true;
    }
    if (!m_completedSpawnInventoryTransactions.contains(acMessage.ServerId)) {
        if (HasPendingSpawnInventoryTransaction(acMessage.ServerId))
            return false;
        const std::vector<Inventory::Entry> entries(acMessage.InventoryContent.Entries.begin(),
                                                     acMessage.InventoryContent.Entries.end());
        const std::vector<std::uint8_t> drops(entries.size(), 0);
        TP_UNUSED(QueueInventoryTransaction(acMessage.ServerId, entries, drops, true, true));
        return false;
    }
    bool admitted{};
    const auto retainAdmission = [&submitted, &admitted](const bool aAccepted) noexcept {
        submitted = aAccepted && submitted;
        admitted = admitted || aAccepted;
    };
    for (const auto& entry : acMessage.InventoryContent.Entries)
    {
        if (!entry.IsWorn())
            continue;
        const auto formId = ToLocal(m_world, entry.BaseId);
        if (formId == 0)
            continue;
        auto payload = Payload();
        payload.LocalFormIdA = formId;
        payload.ValueA = std::max(1, entry.Count);
        retainAdmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Equipment,
                                       GameplayBridge::GameplayAction::EquipForm, payload));
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
        retainAdmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Equipment,
                                       GameplayBridge::GameplayAction::EquipForm, payload));
    }
    auto factionSignature = Signature(acMessage.ServerId, acMessage.FactionsContent.NpcFactions.size(),
                                      acMessage.FactionsContent.ExtraFactions.size());
    const auto mixFactions = [&factionSignature](const auto& acFactions, const std::uint32_t aKind) noexcept {
        for (const auto& faction : acFactions)
            factionSignature = Mix(factionSignature, Signature(faction.Id.LogFormat(), faction.Rank, aKind));
    };
    mixFactions(acMessage.FactionsContent.NpcFactions, 0);
    mixFactions(acMessage.FactionsContent.ExtraFactions, 1);
    const auto factionAcceptance = PrepareAccept(acMessage.ServerId, GameplayBridge::GameplayDomain::ActorState, 0,
                                                 factionSignature);
    std::vector<GameplayBridge::GameplayAction> factionActions{GameplayBridge::GameplayAction::ResetFactions};
    std::vector<GameplayBridge::GameplayActionPayload> factionPayloads{Payload()};
    const auto appendFactions = [this, &factionActions, &factionPayloads](const auto& acFactions) noexcept {
        for (const auto& faction : acFactions) {
            const auto formId = ToLocal(m_world, faction.Id);
            if (formId == 0)
                return false;
            auto payload = Payload();
            payload.LocalFormIdA = formId;
            payload.ValueA = faction.Rank;
            factionActions.push_back(GameplayBridge::GameplayAction::SetFactionRank);
            factionPayloads.push_back(payload);
        }
        return true;
    };
    if (!factionAcceptance.Valid || !appendFactions(acMessage.FactionsContent.NpcFactions) ||
        !appendFactions(acMessage.FactionsContent.ExtraFactions)) {
        submitted = false;
    } else {
        retainAdmission(QueueReliableBatchForServer(factionAcceptance, acMessage.ServerId,
                                                     GameplayBridge::GameplayDomain::ActorState,
                                                     std::move(factionActions), std::move(factionPayloads)));
    }
    std::uint32_t replayIndex{};
    for (const auto& action : acMessage.ActionsToReplay.Actions)
    {
        if (HasHumanoidActorActionVariables(action) &&
            SkyrimTogether::Protocol::HasCapability(
                m_transport.GetNegotiatedGameplayCapabilities(),
                SkyrimTogether::Protocol::GameplayCapability::ExactAnimationActions)) {
            if (HasExactActorActionCapability()) {
                if (!SubmitRemoteActorAction(acMessage.ServerId, action))
                    QueueRemoteActorAction(acMessage.ServerId, action);
            } else if (!SubmitLegacyRemoteActorAction(acMessage.ServerId, action)) {
                QueueRemoteActorAction(acMessage.ServerId, action);
            }
            continue;
        }
        const std::string_view eventName{action.EventName.c_str()};
        if (eventName.empty() || eventName.size() > 127 ||
            !GameplayBridge::IsSupportedLegacyAnimationEvent(eventName))
            continue;
        auto textId = Signature(acMessage.ServerId, acMessage.PlayerId, action.Tick, replayIndex++);
        if (textId == 0)
            textId = 1;
        retainAdmission(ApplyTextForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Animation,
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
        retainAdmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::ActorState,
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
        retainAdmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::ActorState,
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
        retainAdmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Appearance,
                                       GameplayBridge::GameplayAction::SetTint, payload));
    }
    auto payload = Payload();
    payload.ValueA = acMessage.IsDead ? 1 : 0;
    retainAdmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::ActorState,
                                   GameplayBridge::GameplayAction::SetDeathState, payload));
    payload = Payload();
    payload.ValueA = acMessage.IsWeaponDrawn ? 1 : 0;
    retainAdmission(ApplyForServer(acMessage.ServerId, GameplayBridge::GameplayDomain::Animation,
                                   GameplayBridge::GameplayAction::DrawWeapon, payload));
    if (!submitted && admitted) {
        RememberBoundedServerId(m_quarantinedSpawns, acMessage.ServerId);
        return true;
    }
    return submitted;
}
catch (...)
{
    ForgetSpawnActionIds(acMessage.ServerId);
    m_pendingSpawns.erase(acMessage.ServerId);
    RememberBoundedServerId(m_quarantinedSpawns, acMessage.ServerId);
    return true;
}

void VRActorReplicationService::OnDrawWeapon(const NotifyDrawWeapon& acMessage) noexcept
{
    const auto acceptance = PrepareAccept(acMessage.Id, GameplayBridge::GameplayDomain::Animation, 0,
                                          Signature(acMessage.Id, acMessage.IsWeaponDrawn));
    if (!acceptance.Valid)
        return;
    auto payload = Payload();
    payload.ValueA = acMessage.IsWeaponDrawn ? 1 : 0;
    TP_UNUSED(QueueReliableForServer(acceptance, acMessage.Id, GameplayBridge::GameplayDomain::Animation,
                                     GameplayBridge::GameplayAction::DrawWeapon, payload));
}

bool VRActorReplicationService::TrySubmitEquipmentApplication(
    const std::uint32_t aServerId, PendingEquipmentApplication& arPending) noexcept try
{
    if (aServerId == 0 || arPending.TransactionId == 0 || arPending.AwaitingResult ||
        arPending.ResultFailures >= kMaximumEquipmentResultFailures || arPending.Entries.size() > 64)
        return false;
    if (m_equipmentActionOwners.size() > kMaximumEquipmentResultOwners - (arPending.Entries.size() + 2))
        return false;
    if (!CanCommitAccept(arPending.Acceptance)) {
        if (arPending.Acceptance.Sequence != 0)
            return false;
        const auto refreshed = PrepareAccept(arPending.Acceptance.PlayerId, arPending.Acceptance.Domain, 0,
                                             arPending.Acceptance.Signature, arPending.Acceptance.Channel);
        if (!refreshed.Valid)
            return false;
        arPending.Acceptance = refreshed;
    }

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
    if (!append(GameplayBridge::GameplayAction::EquipmentSnapshotEnd, payload))
        return false;
    arPending.AwaitingResult = true;
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommandBatch(commands.data(), commands.size())) {
        arPending.AwaitingResult = false;
        return false;
    }

    const auto firstActionId = commands.front().Header.Identity.ActionId;
    const auto actionId = commands.back().Header.Identity.ActionId;
    if (firstActionId == 0 || actionId == 0 ||
        actionId != firstActionId + static_cast<std::uint64_t>(commands.size() - 1)) {
        TerminalizeEquipmentApplication(aServerId);
        return false;
    }
    arPending.ActionId = actionId;
    arPending.ExpectedResults = static_cast<std::uint16_t>(commands.size());
    arPending.NextResultIndex = 0;
    arPending.AwaitingResult = true;
    arPending.ResultWaitElapsed = 0.0;
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const auto [owner, inserted] = m_equipmentActionOwners.emplace(
            commands[index].Header.Identity.ActionId,
            EquipmentActionTracking{aServerId, arPending.TransactionId,
                                    static_cast<GameplayBridge::GameplayAction>(
                                        commands[index].Payload.ApplyGameplayAction.Action),
                                    static_cast<std::uint16_t>(index), commands[index].Header.Identity,
                                    commands[index].Payload.ApplyGameplayAction.TargetHandle});
        TP_UNUSED(owner);
        if (!inserted) {
            TerminalizeEquipmentApplication(aServerId);
            return false;
        }
    }
    if (!CommitAccept(arPending.Acceptance)) {
        // This can only happen after an unexpected ledger change. The bridge
        // batch is already admitted, so leave no replay path.
        TerminalizeEquipmentApplication(aServerId);
        return false;
    }
    return true;
} catch (...) {
    return false;
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

void VRActorReplicationService::TerminalizeEquipmentApplication(const std::uint32_t aServerId) noexcept
{
    std::erase_if(m_equipmentActionOwners, [aServerId](const auto& acEntry) noexcept {
        return acEntry.second.ServerId == aServerId;
    });
    const auto pending = m_pendingEquipmentApplications.find(aServerId);
    if (pending == m_pendingEquipmentApplications.end())
        return;
    pending->second.AwaitingResult = false;
    pending->second.ResultFailures = kMaximumEquipmentResultFailures;
    pending->second.ResultWaitElapsed = 0.0;
}

void VRActorReplicationService::ForgetAppearanceApplication(const std::uint64_t aTargetKey) noexcept
{
    const auto pending = m_pendingAppearanceApplications.find(aTargetKey);
    if (pending != m_pendingAppearanceApplications.end()) {
        for (const auto actionId : pending->second.ActionIds)
            m_appearanceActionOwners.erase(actionId);
        m_pendingAppearanceApplications.erase(pending);
    }
    std::erase_if(m_appearanceActionOwners, [aTargetKey](const auto& acEntry) noexcept {
        return acEntry.second.TargetKey == aTargetKey;
    });
}

void VRActorReplicationService::OnEquipment(const NotifyEquipmentChanges& acMessage) noexcept try
{
    if (acMessage.TransactionId != 0) {
        if (acMessage.ServerId == 0 || acMessage.ItemId || acMessage.EquipSlotId || acMessage.Count != 0 ||
            acMessage.Unequip || acMessage.IsSpell || acMessage.IsShout ||
            acMessage.FinalEquipment.Entries.size() > 64)
            return;
        const auto known = m_lastEquipmentTransactionByServer.find(acMessage.ServerId);
        if (known != m_lastEquipmentTransactionByServer.end() && acMessage.TransactionId <= known->second)
            return;
        const auto acceptance = PrepareAccept(acMessage.ServerId, GameplayBridge::GameplayDomain::Equipment, 0,
                                              Signature(acMessage.ServerId, acMessage.TransactionId));
        if (!acceptance.Valid)
            return;

        std::vector<PendingEquipmentEntry> entries;
        entries.reserve(acMessage.FinalEquipment.Entries.size());
        std::unordered_set<GameId> seen;
        for (const auto& entry : acMessage.FinalEquipment.Entries) {
            const auto formId = ToLocal(m_world, entry.BaseId);
            const auto knownEquipmentFlags = Inventory::Entry::kEquipmentWeapon | Inventory::Entry::kEquipmentAmmo;
            if (!entry.BaseId || entry.Count <= 0 || entry.Count > 10'000 || !entry.IsWorn() || formId == 0 ||
                (entry.EquipmentFlags & ~knownEquipmentFlags) != 0 ||
                (entry.EquipmentFlags & knownEquipmentFlags) == knownEquipmentFlags ||
                ((entry.EquipmentFlags & Inventory::Entry::kEquipmentAmmo) != 0 && entry.ExtraWornLeft) ||
                !seen.emplace(entry.BaseId).second)
                return;
            entries.push_back({formId, entry.Count,
                               (entry.ExtraWorn ? GameplayBridge::kEquipmentSnapshotWorn : 0u) |
                               (entry.ExtraWornLeft ? GameplayBridge::kEquipmentSnapshotWornLeft : 0u) |
                               ((entry.EquipmentFlags & Inventory::Entry::kEquipmentWeapon) != 0 ?
                                    GameplayBridge::kEquipmentSnapshotWeapon : 0u) |
                               ((entry.EquipmentFlags & Inventory::Entry::kEquipmentAmmo) != 0 ?
                                    GameplayBridge::kEquipmentSnapshotAmmo : 0u)});
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
                                        std::move(entries), 0, acceptance, 0, 0, 0, 0.0, false});
        TP_UNUSED(created);
        TP_UNUSED(TrySubmitEquipmentApplication(acMessage.ServerId, inserted->second));
        return;
    }

    const auto item = ToLocal(m_world, acMessage.ItemId);
    const auto slot = ToLocal(m_world, acMessage.EquipSlotId);
    const auto acceptance = PrepareAccept(acMessage.ServerId, GameplayBridge::GameplayDomain::Equipment, 0,
                                          Signature(acMessage.ServerId, item, slot, acMessage.Count, acMessage.Unequip));
    if (item == 0 || !acceptance.Valid)
        return;
    auto payload = Payload();
    payload.LocalFormIdA = item;
    payload.LocalFormIdB = slot;
    payload.ValueA = static_cast<std::int32_t>(acMessage.Count);
    payload.ActionFlags = (acMessage.IsSpell ? kFlagBool0 : 0) | (acMessage.IsShout ? kFlagBool1 : 0);
    TP_UNUSED(QueueReliableForServer(acceptance, acMessage.ServerId, GameplayBridge::GameplayDomain::Equipment,
        acMessage.Unequip ? GameplayBridge::GameplayAction::UnequipForm : GameplayBridge::GameplayAction::EquipForm, payload));
}
catch (...)
{
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
        const auto acceptance = PrepareAccept(serverId, GameplayBridge::GameplayDomain::ActorState, 0, signature);
        if (!acceptance.Valid)
            continue;

        std::vector<GameplayBridge::GameplayAction> actions{GameplayBridge::GameplayAction::ResetFactions};
        std::vector<GameplayBridge::GameplayActionPayload> payloads{Payload()};
        const auto appendFactions = [this, &actions, &payloads](const auto& acFactions) noexcept {
            for (const auto& faction : acFactions)
            {
                const auto formId = ToLocal(m_world, faction.Id);
                const auto rank = static_cast<std::int32_t>(faction.Rank);
                if (formId == 0 || rank < std::numeric_limits<std::int8_t>::min() ||
                    rank > std::numeric_limits<std::int8_t>::max())
                    return false;

                auto payload = Payload();
                payload.LocalFormIdA = formId;
                payload.ValueA = rank;
                actions.push_back(GameplayBridge::GameplayAction::SetFactionRank);
                payloads.push_back(payload);
            }
            return true;
        };
        if (!appendFactions(factions.NpcFactions) || !appendFactions(factions.ExtraFactions))
            continue;
        TP_UNUSED(QueueReliableBatchForServer(acceptance, serverId, GameplayBridge::GameplayDomain::ActorState,
                                               std::move(actions), std::move(payloads)));
    }
}

void VRActorReplicationService::OnInventory(const NotifyInventoryChanges& acMessage) noexcept try
{
    const auto drop = acMessage.Drop && acMessage.Item.Count < 0;
    const auto acceptance = PrepareAccept(acMessage.ServerId, GameplayBridge::GameplayDomain::Inventory, 0,
                                          Signature(acMessage.ServerId, InventoryEntrySignature(acMessage.Item, drop)));
    if (!acMessage.Item.IsValidMutation() || !acceptance.Valid)
        return;
    const std::vector<Inventory::Entry> entries{acMessage.Item};
    const std::vector<std::uint8_t> drops{static_cast<std::uint8_t>(drop)};
    TP_UNUSED(QueueInventoryTransaction(acMessage.ServerId, entries, drops, false, false, &acceptance));
}
catch (...)
{
}

void VRActorReplicationService::OnActorValues(const NotifyActorValueChanges& acMessage) noexcept try
{
    std::vector<std::pair<std::uint32_t, float>> values(acMessage.Values.begin(), acMessage.Values.end());
    std::sort(values.begin(), values.end());
    for (const auto& [id, value] : values)
    {
        const auto acceptance = PrepareAccept(acMessage.Id, GameplayBridge::GameplayDomain::ActorState, 0,
                                              Signature(acMessage.Id, id, FloatBits(value)));
        if (!IsFinite(value) || id >= kActorValueCount || id == kDragonSoulsActorValue || !acceptance.Valid)
            continue;
        auto payload = Payload();
        payload.LocalFormIdA = id;
        payload.ScalarA = value;
        TP_UNUSED(QueueReliableForServer(acceptance, acMessage.Id, GameplayBridge::GameplayDomain::ActorState,
                                         GameplayBridge::GameplayAction::SetActorValue, payload));
    }
}
catch (...)
{
}

void VRActorReplicationService::OnActorMaximums(const NotifyActorMaxValueChanges& acMessage) noexcept try
{
    std::vector<std::pair<std::uint32_t, float>> values(acMessage.Values.begin(), acMessage.Values.end());
    std::sort(values.begin(), values.end());
    for (const auto& [id, value] : values)
    {
        const auto acceptance = PrepareAccept(acMessage.Id, GameplayBridge::GameplayDomain::ActorState, 0,
                                              Signature(acMessage.Id, id, FloatBits(value), 1));
        if (!IsFinite(value) || id >= kActorValueCount || id == kDragonSoulsActorValue || !acceptance.Valid)
            continue;
        auto payload = Payload();
        payload.LocalFormIdA = id;
        payload.ScalarA = value;
        TP_UNUSED(QueueReliableForServer(acceptance, acMessage.Id, GameplayBridge::GameplayDomain::ActorState,
                                         GameplayBridge::GameplayAction::SetActorMaximum, payload));
    }
}
catch (...)
{
}

void VRActorReplicationService::OnHealthChangeBroadcast(const NotifyHealthChangeBroadcast& acMessage) noexcept
{
    if (acMessage.Id == 0 || !IsFinite(acMessage.DeltaHealth))
        return;

    const auto acceptance = PrepareAccept(acMessage.Id, GameplayBridge::GameplayDomain::ActorState, 0,
                                          Signature(acMessage.Id, FloatBits(acMessage.DeltaHealth), 4));
    if (!acceptance.Valid)
        return;

    auto payload = Payload();
    payload.LocalFormIdA = kHealthActorValue;
    payload.ScalarA = acMessage.DeltaHealth;
    TP_UNUSED(QueueReliableForServer(acceptance, acMessage.Id, GameplayBridge::GameplayDomain::ActorState,
                                     GameplayBridge::GameplayAction::ModifyActorValue, payload));
}

void VRActorReplicationService::OnDeath(const NotifyDeathStateChange& acMessage) noexcept
{
    const auto acceptance = PrepareAccept(acMessage.Id, GameplayBridge::GameplayDomain::ActorState, 0,
                                          Signature(acMessage.Id, acMessage.IsDead, 2));
    if (!acceptance.Valid)
        return;
    auto payload = Payload();
    payload.ValueA = acMessage.IsDead ? 1 : 0;
    TP_UNUSED(QueueReliableForServer(acceptance, acMessage.Id, GameplayBridge::GameplayDomain::ActorState,
                                     GameplayBridge::GameplayAction::SetDeathState, payload));
}

void VRActorReplicationService::OnRespawn(const NotifyRespawn& acMessage) noexcept
{
    const auto acceptance = PrepareAccept(acMessage.ActorId, GameplayBridge::GameplayDomain::ActorState, 0,
                                          Signature(acMessage.ActorId, 3));
    if (!acceptance.Valid)
        return;
    TP_UNUSED(QueueReliableForServer(acceptance, acMessage.ActorId, GameplayBridge::GameplayDomain::ActorState,
                                     GameplayBridge::GameplayAction::Respawn, Payload()));
}

void VRActorReplicationService::OnMount(const NotifyMount& acMessage) noexcept
{
    const auto acceptance = PrepareAccept(acMessage.RiderId, GameplayBridge::GameplayDomain::ActorState, 0,
                                          Signature(acMessage.RiderId, acMessage.MountId));
    if (!acceptance.Valid)
        return;
    if (acMessage.MountId == 0)
        return;
    if (TryApplyMount(acMessage.RiderId, acMessage.MountId, acceptance)) {
        m_pendingMounts.erase(acMessage.RiderId);
        return;
    }
    constexpr std::size_t kMaximumPendingMounts = 64;
    if (m_pendingMounts.contains(acMessage.RiderId) || m_pendingMounts.size() < kMaximumPendingMounts)
        m_pendingMounts[acMessage.RiderId] = {acMessage.MountId, acceptance, 0, 0.0};
}

bool VRActorReplicationService::TryApplyMount(
    const std::uint32_t aRiderServerId, const std::uint32_t aMountServerId,
    const AcceptanceToken& acAcceptance) noexcept
{
    if (aRiderServerId == 0 || aMountServerId == 0)
        return false;
    const auto mountHandle = m_avatars.GetRemoteAvatarHandleForServerId(aMountServerId);
    if (mountHandle.Value == 0)
        return false;
    auto payload = Payload();
    payload.SecondaryHandle = mountHandle;
    return QueueReliableForServer(acAcceptance, aRiderServerId, GameplayBridge::GameplayDomain::ActorState,
                                  GameplayBridge::GameplayAction::Mount, payload);
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
        acMessage.Area > GameplayBridge::kMaximumProjectileArea)
        return;

    const auto acceptance = PrepareAccept(acMessage.ShooterID, GameplayBridge::GameplayDomain::Projectile, 0,
                                          Signature(acMessage.ShooterID, projectile, FloatBits(acMessage.OriginX),
                                                    FloatBits(acMessage.OriginY), FloatBits(acMessage.OriginZ)));
    if (!acceptance.Valid)
        return;

    const auto shooter = m_avatars.GetRemoteAvatarHandleForServerId(acMessage.ShooterID);
    if (shooter.Value == 0)
        return;
    GameplayBridge::ApplyProjectileLaunchPayload payload{};
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
    TP_UNUSED(QueueReliableProjectile(acceptance, acMessage.ShooterID, payload));
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
    m_quarantinedSpawns.erase(acMessage.Id);
    ForgetSpawnActionIds(acMessage.Id);
    m_resyncAttempts.erase(acMessage.Id);
    m_pendingSpawns[acMessage.Id] = snapshot->second;
}

void VRActorReplicationService::OnSpellCast(const NotifySpellCast& acMessage) noexcept
{
    const auto spell = ToLocal(m_world, acMessage.SpellFormId);
    const auto acceptance = PrepareAccept(acMessage.CasterId, GameplayBridge::GameplayDomain::Magic, 0,
                                          Signature(acMessage.CasterId, spell, acMessage.CastingSource,
                                                    acMessage.DesiredTarget, acMessage.IsDualCasting));
    if (spell == 0 || !acceptance.Valid)
        return;

    if (SubmitSpellCast(acMessage, acceptance) != SpellCastSubmitResult::AwaitingActor ||
        m_pendingSpellCasts.size() >= kMaximumPendingMagicEffects)
        return;

    const auto existing = std::find_if(m_pendingSpellCasts.begin(), m_pendingSpellCasts.end(),
        [&acMessage](const PendingSpellCast& acPending) noexcept {
            return acPending.Message == acMessage;
    });
    if (existing == m_pendingSpellCasts.end())
        m_pendingSpellCasts.push_back({acMessage, acceptance});
}

VRActorReplicationService::SpellCastSubmitResult VRActorReplicationService::SubmitSpellCast(
    const NotifySpellCast& acMessage, const AcceptanceToken& acAcceptance) noexcept
{
    if (acMessage.CasterId == 0)
        return SpellCastSubmitResult::Rejected;

    const auto spell = ToLocal(m_world, acMessage.SpellFormId);
    if (spell == 0)
        return SpellCastSubmitResult::Rejected;

    MagicActorReference caster{};
    if (!TryResolveMagicActor(acMessage.CasterId, caster))
        return SpellCastSubmitResult::AwaitingActor;

    MagicActorReference desiredTarget{};
    if (acMessage.DesiredTarget != 0 && !TryResolveMagicActor(acMessage.DesiredTarget, desiredTarget))
        return SpellCastSubmitResult::AwaitingActor;

    auto payload = Payload();
    payload.LocalFormIdA = spell;
    payload.ValueA = acMessage.CastingSource;
    payload.ScalarA = 1.0f;
    payload.ActionFlags = acMessage.IsDualCasting ? kFlagBool2 : 0;
    if (desiredTarget.Handle.Value != 0)
        payload.SecondaryHandle = desiredTarget.Handle;
    else
        payload.LocalFormIdB = desiredTarget.LocalReferenceFormId;

    return QueueReliableForServer(acAcceptance, acMessage.CasterId, GameplayBridge::GameplayDomain::Magic,
                                  GameplayBridge::GameplayAction::CastSpell, payload) ?
               SpellCastSubmitResult::Submitted : SpellCastSubmitResult::Rejected;
}

void VRActorReplicationService::OnInterruptCast(const NotifyInterruptCast& acMessage) noexcept
{
    const auto acceptance = PrepareAccept(acMessage.CasterId, GameplayBridge::GameplayDomain::Magic, 0,
                                          Signature(acMessage.CasterId, acMessage.CastingSource, 1));
    if (!acceptance.Valid)
        return;
    auto payload = Payload();
    payload.ValueA = acMessage.CastingSource;
    TP_UNUSED(QueueReliableForServer(acceptance, acMessage.CasterId, GameplayBridge::GameplayDomain::Magic,
                                     GameplayBridge::GameplayAction::InterruptCast, payload));
}

void VRActorReplicationService::OnNotifyRemoveSpell(const NotifyRemoveSpell& acMessage) noexcept
{
    const auto spell = ToLocal(m_world, acMessage.SpellId);
    if (acMessage.TargetId == 0 || spell == 0)
        return;

    const auto acceptance = PrepareAccept(acMessage.TargetId, GameplayBridge::GameplayDomain::Magic, 0,
                                          Signature(acMessage.TargetId, spell, 2));
    if (!acceptance.Valid)
        return;

    auto payload = Payload();
    payload.LocalFormIdA = spell;
    TP_UNUSED(QueueReliableForServer(acceptance, acMessage.TargetId, GameplayBridge::GameplayDomain::Magic,
                                     GameplayBridge::GameplayAction::RemoveSpell, payload));
}

void VRActorReplicationService::OnNotifyAddTarget(const NotifyAddTarget& acMessage) noexcept try
{
    const auto acceptance = PrepareAccept(acMessage.TargetId, GameplayBridge::GameplayDomain::Magic, 0,
                                          Signature(acMessage.TargetId, acMessage.CasterId,
                                                    acMessage.SpellId.LogFormat(), acMessage.EffectId.LogFormat(),
                                                    FloatBits(acMessage.Magnitude), acMessage.IsDualCasting,
                                                    acMessage.ApplyHealPerkBonus, acMessage.ApplyStaminaPerkBonus));
    if (!acceptance.Valid || SubmitMagicEffect(acMessage, acceptance) != MagicEffectSubmitResult::AwaitingActor ||
        m_pendingMagicEffects.size() >= kMaximumPendingMagicEffects)
        return;

    const auto existing = std::find_if(m_pendingMagicEffects.begin(), m_pendingMagicEffects.end(),
        [&acMessage](const PendingMagicEffect& acPending) noexcept {
            return acPending.Message == acMessage;
    });
    if (existing == m_pendingMagicEffects.end())
        m_pendingMagicEffects.push_back({acMessage, acceptance});
}
catch (...)
{
}

VRActorReplicationService::MagicEffectSubmitResult VRActorReplicationService::SubmitMagicEffect(
    const NotifyAddTarget& acMessage, const AcceptanceToken& acAcceptance) noexcept
{
    if (acMessage.TargetId == 0 || !IsFinite(acMessage.Magnitude) || acMessage.Magnitude < 0.0F)
        return MagicEffectSubmitResult::Rejected;

    const auto spell = ToLocal(m_world, acMessage.SpellId);
    const auto effect = ToLocal(m_world, acMessage.EffectId);
    if (spell == 0 || effect == 0)
        return MagicEffectSubmitResult::Rejected;

    MagicActorReference target{};
    if (!TryResolveMagicActor(acMessage.TargetId, target))
        return MagicEffectSubmitResult::AwaitingActor;

    MagicActorReference caster{};
    if (acMessage.CasterId != 0 && !TryResolveMagicActor(acMessage.CasterId, caster))
        return MagicEffectSubmitResult::AwaitingActor;

    auto payload = Payload();
    payload.LocalFormIdA = spell;
    payload.LocalFormIdB = effect;
    payload.ValueA = kCastingSourceOther;
    payload.ScalarA = acMessage.Magnitude;
    payload.ScalarB = 1.0F;
    payload.ActionFlags = (acMessage.IsDualCasting ? kFlagBool1 : 0) |
                          (acMessage.ApplyHealPerkBonus ? kMagicEffectApplyHealPerkBonus : 0) |
                          (acMessage.ApplyStaminaPerkBonus ? kMagicEffectApplyStaminaPerkBonus : 0);

    if (acMessage.CasterId != 0) {
        payload.SecondaryHandle = caster.Handle;
        payload.LocalFormIdC = caster.LocalReferenceFormId;
    }

    return QueueReliableForServer(acAcceptance, acMessage.TargetId, GameplayBridge::GameplayDomain::Magic,
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
    if (!IsFinite(magic.CasterPosition) || !IsFinite(magic.TargetPosition))
        return;
    // This diagnostic packet identifies only an MGEF. The canonical adapter
    // action requires its owning MagicItem and effect index, so damage/effects
    // continue through original spell and actor-value messages.
    TP_UNUSED(magic);
}

void VRActorReplicationService::OnVrProjectile(const NotifyVRProjectileEvent& acMessage) noexcept
{
    const auto& projectile = acMessage.Projectile;
    if (!IsFinite(projectile.Origin) || !IsFinite(projectile.Destination) || !IsFinite(projectile.Power))
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
    const auto acceptance = PrepareAccept(
        acMessage.PlayerId, GameplayBridge::GameplayDomain::VrBodyPose, pose.Sequence,
        Signature(pose.Body.CaptureSequence, pose.Body.RootGeneration, pose.Hmd.Valid, pose.LeftHand.Valid,
                  pose.RightHand.Valid));
    if (!acceptance.Valid)
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
    std::uint32_t expectedNodeMask{};
    std::vector<GameplayBridge::GameplayAction> actions;
    std::vector<GameplayBridge::GameplayActionPayload> payloads;
    actions.reserve(nodes.size() * 4 + 1);
    payloads.reserve(nodes.size() * 4 + 1);
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
        actions.push_back(GameplayBridge::GameplayAction::VrPoseChunk);
        payloads.push_back(payload);

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
            actions.push_back(GameplayBridge::GameplayAction::VrPoseChunk);
            payloads.push_back(payload);
        }
        expectedNodeMask |= 1u << static_cast<std::uint32_t>(index);
    }

    if (expectedNodeMask == 0)
        return;
    auto commit = Payload();
    commit.ValueA = static_cast<std::int32_t>(pose.Sequence);
    commit.ValueB = static_cast<std::int32_t>(pose.Body.RootGeneration);
    commit.ActionFlags = expectedNodeMask;
    actions.push_back(GameplayBridge::GameplayAction::VrPoseCommit);
    payloads.push_back(commit);
    TP_UNUSED(QueueReliableBatchForPlayer(acceptance, acMessage.PlayerId, GameplayBridge::GameplayDomain::VrBodyPose,
                                           std::move(actions), std::move(payloads)));
}

void VRActorReplicationService::OnVrHiggs(const NotifyVRHiggsState& acMessage) noexcept
{
    const auto& state = acMessage.State;
    const auto acceptance = PrepareAccept(acMessage.PlayerId, GameplayBridge::GameplayDomain::Higgs, state.Sequence,
                                          Signature(state.LastEvent.Sequence, state.LastEvent.EventKind,
                                                    state.LastEvent.ObjectId.LogFormat()));
    if (!acceptance.Valid)
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
    TP_UNUSED(QueueReliableForPlayer(acceptance, acMessage.PlayerId, GameplayBridge::GameplayDomain::Higgs,
                                     action, payload));
}

void VRActorReplicationService::OnVrAppearance(const NotifyVRAppearance& acMessage) noexcept
try
{
    const auto& appearance = acMessage.Appearance;
    const auto acceptance = PrepareAccept(acMessage.PlayerId, GameplayBridge::GameplayDomain::Appearance,
                                          appearance.Sequence, AppearanceSignature(appearance));
    if (!IsRemotePlayer(m_transport, acMessage.PlayerId) || !appearance.IsValid() || !acceptance.Valid)
        return;
    if (!m_pendingAppearanceApplications.contains(acMessage.PlayerId) &&
        m_pendingAppearanceApplications.size() >= kMaximumPendingAppearanceApplications)
        return;

    m_latestAppearances[acMessage.PlayerId] = appearance;
    QueueVRAppearance(acMessage.PlayerId, appearance, &acceptance);
} catch (...) {
}

void VRActorReplicationService::QueueVRAppearance(
    const std::uint64_t aTargetKey, const VRAppearance& acAppearance,
    const AcceptanceToken* const apAcceptance) noexcept try
{
    if (aTargetKey == 0 || (IsNpcAppearanceTarget(aTargetKey) && AppearanceTargetId(aTargetKey) == 0) ||
        !acAppearance.IsValid() || (apAcceptance && !apAcceptance->Valid))
        return;
    const auto existing = m_pendingAppearanceApplications.find(aTargetKey);
    if (existing != m_pendingAppearanceApplications.end()) {
        if (!IsNewer(acAppearance.Sequence, existing->second.Appearance.Sequence))
            return;
        ForgetAppearanceApplication(aTargetKey);
    } else if (m_pendingAppearanceApplications.size() >= kMaximumPendingAppearanceApplications) {
        return;
    }

    auto& pending = m_pendingAppearanceApplications[aTargetKey];
    pending.Appearance = acAppearance;
    if (apAcceptance)
        pending.Acceptance = *apAcceptance;
    m_appliedAppearanceSequences.erase(aTargetKey);
    m_failedAppearanceSequences.erase(aTargetKey);
    if (!ApplyVRAppearance(aTargetKey, pending.Appearance)) {
        pending.SubmissionFailures = 1;
        pending.RetryWaitElapsed = 0.0;
    }
} catch (...) {
}

void VRActorReplicationService::QueueNpcVRAppearance(
    const std::uint32_t aServerId, const VRAppearance& acAppearance) noexcept
{
    if (aServerId != 0)
        QueueVRAppearance(NpcAppearanceTargetKey(aServerId), acAppearance);
}

bool VRActorReplicationService::ApplyVRAppearance(
    const std::uint64_t aTargetKey, const VRAppearance& acAppearance) noexcept try
{
    const auto isNpc = IsNpcAppearanceTarget(aTargetKey);
    const auto targetId = AppearanceTargetId(aTargetKey);
    if (targetId == 0 || (!isNpc && !IsRemotePlayer(m_transport, targetId)))
        return false;
    const auto& appearance = acAppearance;
    const auto pendingIt = m_pendingAppearanceApplications.find(aTargetKey);
    if (pendingIt == m_pendingAppearanceApplications.end() ||
        pendingIt->second.Appearance.Sequence != appearance.Sequence)
        return false;
    auto& pending = pendingIt->second;
    if (pending.AwaitingResult)
        return true;
    if (pending.Acceptance.Valid && !CanCommitAccept(pending.Acceptance)) {
        if (pending.Acceptance.Sequence != 0)
            return false;
        const auto refreshed = PrepareAccept(pending.Acceptance.PlayerId, pending.Acceptance.Domain, 0,
                                             pending.Acceptance.Signature, pending.Acceptance.Channel);
        if (!refreshed.Valid)
            return false;
        pending.Acceptance = refreshed;
    }

    const auto race = ToLocal(m_world, appearance.RaceId);
    if (race == 0)
        return false;

    const auto hairColor = ToLocal(m_world, appearance.HairColorId);
    const auto faceTexture = ToLocal(m_world, appearance.FaceTextureId);
    if ((appearance.HairColorId && hairColor == 0) || (appearance.FaceTextureId && faceTexture == 0))
        return false;

    std::vector<GameplayBridge::CommandRecord> commands;
    commands.reserve(96 + GameplayBridge::kMaximumAppearanceTextRecords);
    const auto append = [this, isNpc, targetId, &commands](
                            const GameplayBridge::GameplayAction aAction,
                            GameplayBridge::GameplayActionPayload aPayload) {
        GameplayBridge::CommandRecord command{};
        const auto built = isNpc ?
            m_avatars.BuildRemoteGameplayCommandForServerId(
                targetId, GameplayBridge::GameplayDomain::Appearance, aAction, command) :
            m_avatars.BuildRemoteGameplayCommand(
                targetId, GameplayBridge::GameplayDomain::Appearance, aAction, command);
        if (!built)
            return false;
        const auto target = command.Payload.ApplyGameplayAction.TargetHandle;
        aPayload.TargetHandle = target;
        aPayload.Domain = static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Appearance);
        aPayload.Action = static_cast<std::uint16_t>(aAction);
        if (aAction != GameplayBridge::GameplayAction::CommitAppearance)
            aPayload.ActionFlags |= GameplayBridge::kAppearanceDeferredRefresh;
        command.Payload.ApplyGameplayAction = aPayload;
        commands.push_back(command);
        return true;
    };
    const auto appendText = [this, isNpc, targetId, &commands](
                                const GameplayBridge::GameplayAction aAction,
                                const std::uint64_t aTextId,
                                const std::uint32_t aOrdinal,
                                const std::string_view aText) {
        if (aText.empty() || aText.size() >
                static_cast<std::size_t>(GameplayBridge::kGameplayTextBytesPerChunk) *
                    GameplayBridge::kMaximumGameplayTextChunks)
            return false;
        GameplayBridge::CommandRecord base{};
        const auto built = isNpc ?
            m_avatars.BuildRemoteGameplayCommandForServerId(
                targetId, GameplayBridge::GameplayDomain::Appearance, aAction, base) :
            m_avatars.BuildRemoteGameplayCommand(
                targetId, GameplayBridge::GameplayDomain::Appearance, aAction, base);
        if (!built)
            return false;
        const auto target = base.Payload.ApplyGameplayAction.TargetHandle;
        const auto chunkCount = static_cast<std::uint16_t>(
            (aText.size() + GameplayBridge::kGameplayTextBytesPerChunk - 1) /
            GameplayBridge::kGameplayTextBytesPerChunk);
        for (std::uint16_t index = 0; index < chunkCount; ++index) {
            auto& command = commands.emplace_back();
            command.Header = base.Header;
            command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyGameplayTextChunk);
            auto& text = command.Payload.ApplyGameplayTextChunk;
            text.TargetHandle = target;
            text.Domain = static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Appearance);
            text.Action = static_cast<std::uint16_t>(aAction);
            text.TextId = aTextId;
            text.ChunkIndex = index;
            text.ChunkCount = chunkCount;
            text.Reserved0 = GameplayBridge::kGameplayTextAppearanceDeferred;
            text.AuxiliaryLocalFormId = aOrdinal;
            const auto offset = static_cast<std::size_t>(index) * GameplayBridge::kGameplayTextBytesPerChunk;
            text.ByteCount = static_cast<std::uint16_t>(std::min<std::size_t>(
                GameplayBridge::kGameplayTextBytesPerChunk, aText.size() - offset));
            std::memcpy(text.Utf8Bytes, aText.data() + offset, text.ByteCount);
        }
        return true;
    };

    auto payload = Payload();
    auto digest = AppearanceSignature(appearance);
    if (digest == 0)
        digest = 1;
    payload.LocalFormIdA = appearance.Sequence;
    payload.LocalFormIdB = static_cast<std::uint32_t>(digest);
    payload.LocalFormIdC = static_cast<std::uint32_t>(digest >> 32);
    payload.LocalFormIdD = VRAppearance::kSchemaVersion;
    payload.ValueA = appearance.HeadPartCount;
    payload.ValueB = appearance.TintCount;
    if (!append(GameplayBridge::GameplayAction::BeginAppearance, payload))
        return false;
    payload = Payload();
    payload.LocalFormIdA = race;
    if (!append(GameplayBridge::GameplayAction::SetRace, payload))
        return false;
    payload = Payload();
    payload.ValueA = appearance.Sex;
    if (!append(GameplayBridge::GameplayAction::SetSex, payload))
        return false;
    payload = Payload();
    payload.ScalarA = appearance.Weight;
    if (!append(GameplayBridge::GameplayAction::SetWeight, payload))
        return false;
    payload = Payload();
    payload.LocalFormIdA = hairColor;
    if (!append(GameplayBridge::GameplayAction::SetHairColor, payload))
        return false;
    payload = Payload();
    payload.LocalFormIdA = faceTexture;
    if (!append(GameplayBridge::GameplayAction::SetFaceTexture, payload))
        return false;
    if (!append(GameplayBridge::GameplayAction::ResetHeadParts, Payload()))
        return false;

    for (std::uint8_t index = 0; index < appearance.HeadPartCount; ++index)
    {
        const auto formId = ToLocal(m_world, appearance.HeadParts[index].FormId);
        if (formId == 0)
            return false;
        payload = Payload();
        payload.LocalFormIdA = formId;
        payload.ValueA = appearance.HeadParts[index].Slot;
        if (!append(GameplayBridge::GameplayAction::SetHeadPart, payload))
            return false;
    }
    if (appearance.HasFaceData) {
        for (std::uint8_t index = 0; index < VRAppearance::kFaceMorphCount; ++index) {
            payload = Payload();
            payload.ValueA = index;
            payload.ScalarA = appearance.FaceMorphs[index];
            if (!append(GameplayBridge::GameplayAction::SetFaceMorph, payload))
                return false;
        }
        for (std::uint8_t index = 0; index < VRAppearance::kFacePartCount; ++index) {
            payload = Payload();
            payload.ValueA = index;
            payload.ValueB = appearance.FaceParts[index];
            if (!append(GameplayBridge::GameplayAction::SetFacePart, payload))
                return false;
        }
    } else if (!append(GameplayBridge::GameplayAction::ResetFaceData, Payload())) {
        return false;
    }
    if (!append(GameplayBridge::GameplayAction::ResetTints, Payload()))
        return false;
    for (std::uint8_t index = 0; index < appearance.TintCount; ++index)
    {
        const auto& tint = appearance.Tints[index];
        payload = Payload();
        payload.LocalFormIdB = tint.Color;
        payload.ValueA = index;
        payload.ValueB = tint.Type;
        payload.ScalarA = tint.Alpha;
        if (tint.TexturePathLength != 0)
            payload.ActionFlags |= GameplayBridge::kAppearanceTintHasTexturePath;
        if (!append(GameplayBridge::GameplayAction::SetTint, payload))
            return false;
        if (tint.TexturePathLength != 0 &&
            !appendText(
                GameplayBridge::GameplayAction::SetTint,
                (static_cast<std::uint64_t>(appearance.Sequence) << 32) | (static_cast<std::uint64_t>(index) + 2),
                static_cast<std::uint32_t>(index) + 1,
                std::string_view{tint.TexturePath.data(), tint.TexturePathLength}))
            return false;
    }
    if (!appendText(
            GameplayBridge::GameplayAction::SetName,
            (static_cast<std::uint64_t>(appearance.Sequence) << 32) | 1u,
            0,
            std::string_view{appearance.Name.data(), appearance.NameLength}))
        return false;

    payload = Payload();
    payload.ValueA = appearance.Level;
    payload.ValueB = appearance.Essential ? 1 : 0;
    if (!append(GameplayBridge::GameplayAction::CommitAppearance, payload) ||
        !SkyrimTogetherVR::GameplayBridgeClient::TrySubmitAppearanceBatch(commands.data(), commands.size()))
        return false;

    pending.ActionIds.clear();
    pending.ActionIds.reserve(commands.size() + 1);
    pending.RemainingResults = 0;
    pending.ResultWaitElapsed = 0.0;
    pending.HadFailure = false;
    const auto track = [this, aTargetKey, &appearance, &pending](
                           const GameplayBridge::BridgeIdentity& acIdentity,
                           const GameplayBridge::AdapterHandle aTargetHandle,
                           const std::uint16_t aResultCount,
                           const GameplayBridge::GameplayDomain aDomain,
                           const GameplayBridge::GameplayAction aAction) noexcept {
        const auto aActionId = acIdentity.ActionId;
        if (aActionId == 0 || aResultCount == 0 ||
            pending.RemainingResults > std::numeric_limits<std::uint16_t>::max() - aResultCount)
            return false;
        pending.ActionIds.push_back(aActionId);
        pending.RemainingResults = static_cast<std::uint16_t>(pending.RemainingResults + aResultCount);
        m_appearanceActionOwners[aActionId] = {
            aTargetKey, appearance.Sequence, aResultCount, aDomain, aAction, acIdentity, aTargetHandle};
        return true;
    };
    for (std::size_t index = 0; index < commands.size();) {
        const auto& command = commands[index];
        const auto kind = static_cast<GameplayBridge::CommandKind>(command.Header.Kind);
        if (kind == GameplayBridge::CommandKind::ApplyGameplayAction) {
            const auto action = static_cast<GameplayBridge::GameplayAction>(
                command.Payload.ApplyGameplayAction.Action);
            if (!track(command.Header.Identity, command.Payload.ApplyGameplayAction.TargetHandle, 1,
                       GameplayBridge::GameplayDomain::Appearance, action))
                pending.HadFailure = true;
            ++index;
            continue;
        }
        const auto& text = command.Payload.ApplyGameplayTextChunk;
        if (!track(command.Header.Identity, text.TargetHandle, text.ChunkCount,
                   GameplayBridge::GameplayDomain::Appearance,
                   static_cast<GameplayBridge::GameplayAction>(text.Action)))
            pending.HadFailure = true;
        index += text.ChunkCount;
    }
    pending.AwaitingResult = pending.RemainingResults != 0;
    if (!pending.AwaitingResult || pending.HadFailure)
        return false;
    if (pending.Acceptance.Valid && !CommitAccept(pending.Acceptance)) {
        pending.HadFailure = true;
        return false;
    }
    pending.SubmissionFailures = 0;
    pending.RetryWaitElapsed = 0.0;
    return true;
} catch (...) {
    return false;
}

void VRActorReplicationService::OnVrGrab(const NotifyVRGrabEvent& acMessage) noexcept
{
    const auto& grab = acMessage.Grab;
    const auto acceptance = PrepareAccept(
        acMessage.PlayerId, GameplayBridge::GameplayDomain::Higgs, grab.Sequence,
        Signature(grab.ObjectId.LogFormat(), grab.Grabbed, FloatBits(grab.Position.x), FloatBits(grab.Position.y),
                  FloatBits(grab.Position.z)), 1);
    if (!acceptance.Valid)
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
    TP_UNUSED(QueueReliableForPlayer(acceptance, acMessage.PlayerId, GameplayBridge::GameplayDomain::Higgs,
        grab.Grabbed ? GameplayBridge::GameplayAction::HiggsGrab : GameplayBridge::GameplayAction::HiggsDrop, payload));
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

    const auto acceptance = PrepareAccept(acMessage.PlayerId, GameplayBridge::GameplayDomain::ActorState, 0,
                                          Signature(acMessage.PlayerId, acMessage.NewLevel, 5));
    if (!acceptance.Valid)
        return;

    auto payload = Payload();
    payload.ValueA = acMessage.NewLevel;
    TP_UNUSED(QueueReliableForPlayer(acceptance, acMessage.PlayerId, GameplayBridge::GameplayDomain::ActorState,
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
    m_pendingInventoryTransactions.clear();
    m_completedSpawnInventoryTransactions.clear();
    m_failedSpawnInventoryTransactions.clear();
    m_quarantinedSpawns.clear();
    m_pendingAppearanceApplications.clear();
    m_appearanceActionOwners.clear();
    m_appliedAppearanceSequences.clear();
    m_failedAppearanceSequences.clear();
    m_pendingMagicEffects.clear();
    m_pendingSpellCasts.clear();
    m_pendingGameplayWork.clear();
    m_gameplayResultOwners.clear();
    m_semanticTombstones.clear();
    m_localActorActions.clear();
    m_pendingRemoteActorActions.clear();
    m_remoteActorActionOwners.clear();
    m_completedRemoteActorActions.clear();
    m_spawnActionOwners.clear();
    m_recordingSpawnServerId = 0;
    m_localServerId = 0;
    m_observedLifecycleEpoch = 0;
    m_replayAfterLifecycleBoundary = false;
    m_semanticTombstoneRebaseRequested = false;
    m_semanticTombstoneRebaseEpoch = 0;
    m_semanticTombstoneRebaseElapsed = 0.0;
}

void VRActorReplicationService::OnGameplayResult(
    const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept try
{
    const auto& record = acEvent.Record;
    if (record.Header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::RemoteGameplayActionState))
        return;
    const auto& result = record.Payload.RemoteGameplayActionState;
    if (const auto gameplay = m_gameplayResultOwners.find(record.Header.Identity.ActionId);
        gameplay != m_gameplayResultOwners.end()) {
        const auto owner = gameplay->second;
        const auto work = std::find_if(m_pendingGameplayWork.begin(), m_pendingGameplayWork.end(),
            [workId = owner.WorkId](const PendingGameplayWork& acWork) noexcept {
                return acWork.WorkId == workId;
            });
        const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
        const auto domain = static_cast<GameplayBridge::GameplayDomain>(result.Domain);
        const auto action = static_cast<GameplayBridge::GameplayAction>(result.Action);
        if (work == m_pendingGameplayWork.end()) {
            m_gameplayResultOwners.erase(gameplay);
            return;
        }
        if (work->Terminal) {
            m_gameplayResultOwners.erase(gameplay);
            return;
        }
        if (!work->AwaitingResult ||
            !MatchesTrackedResult(record, owner.Identity, owner.TargetHandle) ||
            result.TargetLocalFormId != owner.TargetLocalFormId || domain != owner.Domain || action != owner.Action ||
            owner.ResultIndex != work->NextResultIndex) {
            // A result with a known ActionId but a different identity/order is
            // ambiguous. It must terminate the semantic work, never replay it.
            std::erase_if(m_gameplayResultOwners, [workId = owner.WorkId](const auto& acEntry) noexcept {
                return acEntry.second.WorkId == workId;
            });
            work->AwaitingResult = false;
            RetireReliableGameplayWork(owner.WorkId);
            return;
        }
        if (status == GameplayBridge::CommandStatus::Success) {
            m_gameplayResultOwners.erase(gameplay);
            ++work->NextResultIndex;
            work->ResultWaitElapsed = 0.0;
            if (work->NextResultIndex == work->Actions.size())
                RetireReliableGameplayWork(owner.WorkId);
            return;
        }

        const bool retryableBeforeMutation = owner.ResultIndex == 0 && work->NextResultIndex == 0 &&
            (status == GameplayBridge::CommandStatus::Inactive ||
             status == GameplayBridge::CommandStatus::MissingForm ||
             status == GameplayBridge::CommandStatus::MissingCell ||
             status == GameplayBridge::CommandStatus::QueueOverflow);
        if (retryableBeforeMutation && work->Attempts + 1 < kMaximumGameplayWorkAttempts) {
            std::erase_if(m_gameplayResultOwners, [workId = owner.WorkId](const auto& acEntry) noexcept {
                return acEntry.second.WorkId == workId;
            });
            work->AwaitingResult = false;
            work->ResultWaitElapsed = 0.0;
            work->AdmissionWaitElapsed = 0.0;
            ++work->Attempts;
            return;
        }
        std::erase_if(m_gameplayResultOwners, [workId = owner.WorkId](const auto& acEntry) noexcept {
            return acEntry.second.WorkId == workId;
        });
        work->AwaitingResult = false;
        RetireReliableGameplayWork(owner.WorkId);
        return;
    }
    if (const auto actorAction = m_remoteActorActionOwners.find(record.Header.Identity.ActionId);
        actorAction != m_remoteActorActionOwners.end()) {
        const auto owner = actorAction->second;
        if (!MatchesTrackedResult(record, owner.Identity, owner.TargetHandle)) {
            m_remoteActorActionOwners.erase(actorAction);
            RememberCompletedRemoteActorAction(owner.ServerId, owner.Action);
            return;
        }

        const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
        const auto domain = static_cast<GameplayBridge::GameplayDomain>(result.Domain);
        const auto action = static_cast<GameplayBridge::GameplayAction>(result.Action);
        m_remoteActorActionOwners.erase(actorAction);

        if (status == GameplayBridge::CommandStatus::Success &&
            domain == GameplayBridge::GameplayDomain::Animation &&
            action == GameplayBridge::GameplayAction::ActorAction) {
            RememberCompletedRemoteActorAction(owner.ServerId, owner.Action);
            return;
        }

        const bool retryableBeforeMutation =
            domain == GameplayBridge::GameplayDomain::Animation &&
            action == GameplayBridge::GameplayAction::ActorAction &&
            (status == GameplayBridge::CommandStatus::Inactive ||
             status == GameplayBridge::CommandStatus::MissingForm ||
             status == GameplayBridge::CommandStatus::MissingCell ||
             status == GameplayBridge::CommandStatus::QueueOverflow);
        if (retryableBeforeMutation && owner.Attempts + 1 < kMaximumActorActionAttempts) {
            QueueRemoteActorAction(owner.ServerId, owner.Action,
                                   static_cast<std::uint8_t>(owner.Attempts + 1));
            return;
        }

        // EngineRejected and malformed/stale results can be observed after
        // partial engine work. Suppress replay rather than duplicate an action.
        RememberCompletedRemoteActorAction(owner.ServerId, owner.Action);
        return;
    }
    for (std::size_t index = 0; index < m_pendingInventoryTransactions.size(); ++index) {
        auto& pending = m_pendingInventoryTransactions[index];
        if (!pending.AwaitingResults || record.Header.Identity.ActionId < pending.FirstActionId ||
            record.Header.Identity.ActionId > pending.EndActionId)
            continue;

        const auto resultIndex = static_cast<std::size_t>(record.Header.Identity.ActionId - pending.FirstActionId);
        if (resultIndex >= pending.ExpectedActions.size() || resultIndex >= pending.ResultStates.size() ||
            resultIndex != std::count(pending.ResultStates.begin(), pending.ResultStates.end(), std::uint8_t{1})) {
            TerminalizeInventoryTransaction(index);
            return;
        }
        auto expectedIdentity = pending.Identity;
        expectedIdentity.ActionId = record.Header.Identity.ActionId;
        if (!MatchesTrackedResult(record, expectedIdentity, pending.TargetHandle) ||
            result.TargetLocalFormId != pending.TargetLocalFormId || pending.ResultStates[resultIndex] != 0) {
            TerminalizeInventoryTransaction(index);
            return;
        }

        const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
        const auto domain = static_cast<GameplayBridge::GameplayDomain>(result.Domain);
        const auto action = static_cast<GameplayBridge::GameplayAction>(result.Action);
        const auto expectedAction = pending.ExpectedActions[resultIndex];
        const bool succeeded = status == GameplayBridge::CommandStatus::Success &&
                               domain == GameplayBridge::GameplayDomain::Inventory && action == expectedAction;
        pending.ResultStates[resultIndex] = succeeded ? 1 : 2;
        pending.ResultWaitElapsed = 0.0;
        if (!succeeded) {
            TerminalizeInventoryTransaction(index);
            return;
        }
        if (record.Header.Identity.ActionId != pending.EndActionId)
            return;

        CompleteInventoryTransaction(index, true);
        return;
    }
    if (const auto appearanceOwner = m_appearanceActionOwners.find(record.Header.Identity.ActionId);
        appearanceOwner != m_appearanceActionOwners.end()) {
        auto& owner = appearanceOwner->second;
        const auto ownerTargetKey = owner.TargetKey;
        const auto ownerSequence = owner.Sequence;
        if (!MatchesTrackedResult(record, owner.Identity, owner.TargetHandle)) {
            m_failedAppearanceSequences[ownerTargetKey] = ownerSequence;
            ForgetAppearanceApplication(ownerTargetKey);
            return;
        }
        const auto pendingIt = m_pendingAppearanceApplications.find(owner.TargetKey);
        if (pendingIt == m_pendingAppearanceApplications.end() ||
            pendingIt->second.Appearance.Sequence != owner.Sequence) {
            m_appearanceActionOwners.erase(appearanceOwner);
            return;
        }

        auto& pending = pendingIt->second;
        const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
        const auto domain = static_cast<GameplayBridge::GameplayDomain>(result.Domain);
        const auto action = static_cast<GameplayBridge::GameplayAction>(result.Action);
        const auto expectedResult = domain == owner.Domain && action == owner.Action;
        const auto degradedAppearanceCommit =
            expectedResult && status == GameplayBridge::CommandStatus::Degraded &&
            domain == GameplayBridge::GameplayDomain::Appearance &&
            action == GameplayBridge::GameplayAction::CommitAppearance;
        if (!expectedResult || !GameplayBridge::IsSuccessfulCommandResult(status, domain, action))
            pending.HadFailure = true;
        else if (degradedAppearanceCommit)
            spdlog::warn("VR appearance sequence {} applied with uncomposed face tint masks", owner.Sequence);
        if (owner.RemainingResults > 0)
            --owner.RemainingResults;
        if (owner.RemainingResults == 0)
            m_appearanceActionOwners.erase(appearanceOwner);
        if (pending.RemainingResults > 0)
            --pending.RemainingResults;
        pending.ResultWaitElapsed = 0.0;
        if (pending.RemainingResults != 0)
            return;

        for (const auto actionId : pending.ActionIds)
            m_appearanceActionOwners.erase(actionId);
        pending.ActionIds.clear();
        pending.AwaitingResult = false;
        if (!pending.HadFailure) {
            m_appliedAppearanceSequences[ownerTargetKey] = ownerSequence;
            m_failedAppearanceSequences.erase(ownerTargetKey);
            m_pendingAppearanceApplications.erase(pendingIt);
            return;
        }
        // A post-admission appearance result is not replayed. Even a bridge
        // failure can arrive after native mutation in this staged batch.
        m_failedAppearanceSequences[ownerTargetKey] = ownerSequence;
        m_pendingAppearanceApplications.erase(pendingIt);
        return;
    }
    if (const auto equipment = m_equipmentActionOwners.find(record.Header.Identity.ActionId);
        equipment != m_equipmentActionOwners.end()) {
        const auto owner = equipment->second;
        if (!MatchesTrackedResult(record, owner.Identity, owner.TargetHandle)) {
            TerminalizeEquipmentApplication(owner.ServerId);
            return;
        }
        const auto pending = m_pendingEquipmentApplications.find(owner.ServerId);
        if (pending == m_pendingEquipmentApplications.end() ||
            pending->second.TransactionId != owner.TransactionId ||
            !pending->second.AwaitingResult || owner.ResultIndex != pending->second.NextResultIndex)
        {
            TerminalizeEquipmentApplication(owner.ServerId);
            return;
        }

        auto& application = pending->second;
        const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
        const auto domain = static_cast<GameplayBridge::GameplayDomain>(result.Domain);
        const auto action = static_cast<GameplayBridge::GameplayAction>(result.Action);
        if (status == GameplayBridge::CommandStatus::Success &&
            domain == GameplayBridge::GameplayDomain::Equipment &&
            action == owner.Action) {
            application.ResultWaitElapsed = 0.0;
            m_equipmentActionOwners.erase(equipment);
            ++application.NextResultIndex;
            if (application.NextResultIndex != application.ExpectedResults)
                return;
            m_lastEquipmentTransactionByServer[owner.ServerId] = owner.TransactionId;
            m_pendingEquipmentApplications.erase(pending);
            return;
        }
        TerminalizeEquipmentApplication(owner.ServerId);
        return;
    }

    const auto tracked = m_spawnActionOwners.find(record.Header.Identity.ActionId);
    if (tracked == m_spawnActionOwners.end())
        return;
    const auto serverId = tracked->second.ServerId;
    if (!MatchesTrackedResult(record, tracked->second.Identity, tracked->second.TargetHandle)) {
        m_spawnActionOwners.erase(tracked);
        ForgetSpawnActionIds(serverId);
        m_pendingSpawns.erase(serverId);
        RememberBoundedServerId(m_quarantinedSpawns, serverId);
        return;
    }

    const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
    const auto domain = static_cast<GameplayBridge::GameplayDomain>(result.Domain);
    const auto action = static_cast<GameplayBridge::GameplayAction>(result.Action);
    if (domain != tracked->second.Domain || action != tracked->second.Action) {
        m_spawnActionOwners.erase(tracked);
        ForgetSpawnActionIds(serverId);
        m_pendingSpawns.erase(serverId);
        RememberBoundedServerId(m_quarantinedSpawns, serverId);
        return;
    }
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
    TP_UNUSED(status);
    ForgetSpawnActionIds(serverId);
    m_pendingSpawns.erase(serverId);
    RememberBoundedServerId(m_quarantinedSpawns, serverId);
    spdlog::warn("VR spawn action failed for server actor {}; quarantining ambiguous spawn", serverId);
}
catch (...)
{
    spdlog::error("VR gameplay command result processing failed; rebasing the native capture epoch");
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
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
    return HasPendingSpawnInventoryTransaction(aServerId) || std::any_of(
        m_spawnActionOwners.begin(),
        m_spawnActionOwners.end(),
        [aServerId](const auto& a_entry) noexcept { return a_entry.second.ServerId == aServerId; });
}

void VRActorReplicationService::OnLocalGameplay(
    const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept try
{
    if (!IsCurrentActorActionRecord(acEvent))
        return;

    const auto& record = acEvent.Record;
    const auto actionId = record.Header.Identity.ActionId;
    auto* transaction = GetOrCreateLocalActorAction(actionId);
    if (!transaction)
        return;
    const auto coherentActor = [transaction](const GameplayBridge::AdapterHandle aTargetHandle,
                                              const std::uint32_t aActorLocalFormId) noexcept {
        return transaction->ActorLocalFormId == 0 ||
               (transaction->TargetHandle.Value == aTargetHandle.Value &&
                transaction->ActorLocalFormId == aActorLocalFormId);
    };

    switch (static_cast<GameplayBridge::EventKind>(record.Header.Kind))
    {
    case GameplayBridge::EventKind::LocalActorActionMetadata:
    {
        const auto& payload = record.Payload.LocalActorActionMetadata;
        if (transaction->HasMetadata || !coherentActor(payload.TargetHandle, payload.ActorLocalFormId) ||
            (transaction->Snapshot.SnapshotId != 0 && transaction->Snapshot.SnapshotId != payload.SnapshotId) ||
            (transaction->TextId != 0 && transaction->TextId != payload.TextId))
            return;
        transaction->TargetHandle = payload.TargetHandle;
        transaction->ActorLocalFormId = payload.ActorLocalFormId;
        transaction->Metadata = payload;
        transaction->TextId = payload.TextId;
        transaction->HasMetadata = true;
        TP_UNUSED(TryCommitLocalActorAction(record));
        return;
    }
    case GameplayBridge::EventKind::LocalActorActionGraphChunk:
    {
        const auto& payload = record.Payload.LocalActorActionGraphChunk;
        if (!coherentActor(payload.TargetHandle, payload.ActorLocalFormId) ||
            (transaction->Snapshot.SnapshotId != 0 && transaction->Snapshot.SnapshotId != payload.SnapshotId))
            return;
        const auto type = static_cast<SkyrimTogetherVR::AnimationGraphProtocol::ValueType>(payload.ValueType);
        const auto chunkBit = type == SkyrimTogetherVR::AnimationGraphProtocol::ValueType::BooleanBits ? 1u :
                              1u << (payload.StartIndex / SkyrimTogetherVR::AnimationGraphProtocol::kValuesPerChunk);
        const auto receivedMask = type == SkyrimTogetherVR::AnimationGraphProtocol::ValueType::BooleanBits ?
                                      transaction->Snapshot.BooleanChunkMask :
                                  type == SkyrimTogetherVR::AnimationGraphProtocol::ValueType::Float ?
                                      transaction->Snapshot.FloatChunkMask : transaction->Snapshot.IntegerChunkMask;
        if ((receivedMask & chunkBit) != 0)
            return;
        transaction->TargetHandle = payload.TargetHandle;
        transaction->ActorLocalFormId = payload.ActorLocalFormId;
        const auto accepted = SkyrimTogetherVR::AnimationGraphProtocol::AcceptChunk(
            transaction->Snapshot, payload.SnapshotId, type, payload.StartIndex, payload.ValueCount,
            payload.TotalCount, payload.Direction, payload.Values);
        if (accepted == SkyrimTogetherVR::AnimationGraphProtocol::ChunkAcceptResult::Malformed ||
            accepted == SkyrimTogetherVR::AnimationGraphProtocol::ChunkAcceptResult::Stale)
            return;
        if (transaction->HasMetadata)
            TP_UNUSED(TryCommitLocalActorAction(record));
        return;
    }
    case GameplayBridge::EventKind::LocalActorActionTextChunk:
    {
        const auto& payload = record.Payload.LocalActorActionTextChunk;
        if (!coherentActor(payload.TargetHandle, payload.TargetLocalFormId) ||
            (transaction->TextId != 0 && transaction->TextId != payload.TextId) ||
            (transaction->TextChunkCount != 0 && transaction->TextChunkCount != payload.ChunkCount) ||
            transaction->TextReceived.test(payload.ChunkIndex))
            return;
        transaction->TargetHandle = payload.TargetHandle;
        transaction->ActorLocalFormId = payload.TargetLocalFormId;
        transaction->TextId = payload.TextId;
        transaction->TextChunkCount = payload.ChunkCount;
        std::copy_n(payload.Utf8Bytes, payload.ByteCount, transaction->TextChunks[payload.ChunkIndex].begin());
        transaction->TextLengths[payload.ChunkIndex] = payload.ByteCount;
        transaction->TextReceived.set(payload.ChunkIndex);
        if (transaction->HasMetadata)
            TP_UNUSED(TryCommitLocalActorAction(record));
        return;
    }
    default:
        return;
    }
}
catch (...)
{
}
