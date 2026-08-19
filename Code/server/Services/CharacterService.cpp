#include <Services/CharacterService.h>
#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Events/CharacterSpawnedEvent.h>
#include <Events/CharacterExteriorCellChangeEvent.h>
#include <Events/CharacterInteriorCellChangeEvent.h>
#include <Events/PlayerEnterWorldEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/CharacterRemoveEvent.h>
#include <Events/OwnershipTransferEvent.h>
#include <Events/PlayerLeaveEvent.h>

#include <Game/OwnerView.h>
#include <Game/Player.h>

#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/ClientActorActionRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifySpawnData.h>
#include <Messages/RequestOwnershipTransfer.h>
#include <Messages/NotifyOwnershipTransfer.h>
#include <Messages/RequestOwnershipClaim.h>
#include <Messages/MountRequest.h>
#include <Messages/NotifyMount.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/RequestRespawn.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/SyncExperienceRequest.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/DialogueRequest.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifyActorTeleport.h>
#include <Messages/NotifyRelinquishControl.h>
#include <Structs/MovementOrdering.h>
#include <Structs/GameplayCapabilities.h>
#include <Services/VRAppearanceRelayService.h>
#include <vr_common/VRAnimationGraphProtocol.h>
#include <vr_common/VRAssignmentLimits.h>
#include <vr_common/VRGameplayBridge.h>

#include <Setting.h>

#include <cmath>
#include <utility>
namespace
{
Console::Setting bEnableXpSync{"Gameplay:bEnableXpSync", "Syncs combat XP within the party", true};
constexpr auto kOwnershipGrantLifetime = std::chrono::seconds(5);
constexpr std::size_t kActorValueCount = 164;
constexpr std::size_t kMaximumVRAssignmentInventoryEntries = 512;
constexpr std::size_t kMaximumVRAssignmentInventoryEffects = 512;
constexpr float kMaximumActorValueMagnitude = 1'000'000.0F;
constexpr float kMaximumInventoryScalarMagnitude = 1'000'000.0F;

[[nodiscard]] bool HasAssignmentAction(const ActionEvent& acAction) noexcept;
[[nodiscard]] bool IsValidAssignmentAction(const ActionEvent& acAction) noexcept;

void LogCharacterServiceFailure(const char* apOperation) noexcept
{
    try
    {
        spdlog::error("Character service {} failed; authoritative state was retained", apOperation);
    }
    catch (...)
    {
    }
}

void SwapActorValues(ActorValues& aLeft, ActorValues& aRight) noexcept
{
    aLeft.ActorValuesList.swap(aRight.ActorValuesList);
    aLeft.ActorMaxValuesList.swap(aRight.ActorMaxValuesList);
    const bool decodedValid = aLeft.IsDecodedValid;
    aLeft.IsDecodedValid = aRight.IsDecodedValid;
    aRight.IsDecodedValid = decodedValid;
}

void SwapInventory(Inventory& aLeft, Inventory& aRight) noexcept
{
    aLeft.Entries.swap(aRight.Entries);
    using std::swap;
    swap(aLeft.CurrentMagicEquipment, aRight.CurrentMagicEquipment);
    const bool decodedValid = aLeft.IsDecodedValid;
    aLeft.IsDecodedValid = aRight.IsDecodedValid;
    aRight.IsDecodedValid = decodedValid;
}

void SwapFactions(Factions& aLeft, Factions& aRight) noexcept
{
    aLeft.NpcFactions.swap(aRight.NpcFactions);
    aLeft.ExtraFactions.swap(aRight.ExtraFactions);
    const bool decodedValid = aLeft.IsDecodedValid;
    aLeft.IsDecodedValid = aRight.IsDecodedValid;
    aRight.IsDecodedValid = decodedValid;
}

void SwapAnimationVariables(AnimationVariables& aLeft, AnimationVariables& aRight) noexcept
{
    aLeft.Booleans.swap(aRight.Booleans);
    aLeft.Integers.swap(aRight.Integers);
    aLeft.Floats.swap(aRight.Floats);
    const bool decodedValid = aLeft.IsDecodedValid;
    aLeft.IsDecodedValid = aRight.IsDecodedValid;
    aRight.IsDecodedValid = decodedValid;
}

struct PendingActorData
{
    ActorValues Values{};
    Inventory Content{};
};

void PrepareActorData(const ActorData& acActorData, const bool aHasActorValues,
    const bool aHasInventory, PendingActorData& aPending)
{
    if (aHasActorValues)
        aPending.Values = acActorData.InitialActorValues;
    if (aHasInventory)
        aPending.Content = acActorData.InitialInventory;
}

void CommitActorData(ActorValuesComponent* apActorValuesComponent,
                     InventoryComponent* apInventoryComponent,
                     CharacterComponent* apCharacterComponent,
    const ActorData& acActorData, PendingActorData& aPending) noexcept
{
    if (apActorValuesComponent)
        SwapActorValues(apActorValuesComponent->CurrentActorValues, aPending.Values);
    if (apInventoryComponent)
        SwapInventory(apInventoryComponent->Content, aPending.Content);
    if (apCharacterComponent)
    {
        apCharacterComponent->SetDead(acActorData.IsDead);
        apCharacterComponent->SetWeaponDrawn(acActorData.IsWeaponDrawn);
    }
}

[[nodiscard]] bool IsValidRequiredGameId(const GameId& acId) noexcept
{
    return acId.BaseId != 0;
}

[[nodiscard]] bool IsValidOptionalGameId(const GameId& acId) noexcept
{
    return !acId || IsValidRequiredGameId(acId);
}

[[nodiscard]] bool IsValidActorValues(const ActorValues& acValues, const bool aRequireComplete) noexcept
{
    if (!acValues.IsDecodedValid || acValues.ActorValuesList.size() > kActorValueCount ||
        acValues.ActorMaxValuesList.size() > kActorValueCount)
        return false;

    const auto validMap = [aRequireComplete](const auto& acMap) noexcept {
        for (const auto& [id, value] : acMap)
        {
            if (id >= kActorValueCount || !std::isfinite(value) ||
                std::abs(value) > kMaximumActorValueMagnitude)
                return false;
        }
        if (aRequireComplete)
            for (const auto id : SkyrimTogetherVR::GameplayBridge::kEssentialAssignmentActorValues)
                if (acMap.find(id) == acMap.end())
                    return false;
        return true;
    };
    return validMap(acValues.ActorValuesList) && validMap(acValues.ActorMaxValuesList);
}

[[nodiscard]] bool IsValidAssignmentInventory(const Inventory& acInventory, const bool aIsVR) noexcept
{
    const auto maximumEntries = aIsVR ? kMaximumVRAssignmentInventoryEntries : Inventory::kMaximumWireEntries;
    const auto maximumEffects = aIsVR ? kMaximumVRAssignmentInventoryEffects : Inventory::kMaximumWireEffects;
    if (!acInventory.IsDecodedValid || acInventory.Entries.size() > maximumEntries ||
        !IsValidOptionalGameId(acInventory.CurrentMagicEquipment.LeftHandSpell) ||
        !IsValidOptionalGameId(acInventory.CurrentMagicEquipment.RightHandSpell) ||
        !IsValidOptionalGameId(acInventory.CurrentMagicEquipment.Shout))
        return false;

    std::size_t effectCount{};
    for (const auto& entry : acInventory.Entries)
    {
        constexpr auto knownEquipmentFlags = Inventory::Entry::kEquipmentWeapon |
            Inventory::Entry::kEquipmentAmmo;
        if (!entry.IsDecodedValid || !IsValidRequiredGameId(entry.BaseId) || entry.Count <= 0 ||
            !std::isfinite(entry.ExtraCharge) || entry.ExtraCharge < 0.0F ||
            entry.ExtraCharge > kMaximumInventoryScalarMagnitude ||
            !std::isfinite(entry.ExtraHealth) || entry.ExtraHealth < 0.0F ||
            entry.ExtraHealth > kMaximumInventoryScalarMagnitude ||
            !IsValidOptionalGameId(entry.ExtraEnchantId) ||
            !IsValidOptionalGameId(entry.ExtraPoisonId) || entry.ExtraSoulLevel < 0 ||
            entry.ExtraSoulLevel > 5 || (entry.EquipmentFlags & ~knownEquipmentFlags) != 0 ||
            (entry.EquipmentFlags & knownEquipmentFlags) == knownEquipmentFlags ||
            entry.EnchantData.Effects.size() > maximumEffects - effectCount ||
            (!entry.ExtraEnchantId &&
             (entry.ExtraEnchantCharge != 0 || !entry.EnchantData.Effects.empty() ||
              entry.EnchantData.IsWeapon || entry.ExtraEnchantRemoveUnequip)) ||
            (!entry.ExtraPoisonId && entry.ExtraPoisonCount != 0) ||
            entry.ExtraPoisonCount > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
            return false;

        effectCount += entry.EnchantData.Effects.size();
        for (const auto& effect : entry.EnchantData.Effects)
        {
            if (!IsValidRequiredGameId(effect.EffectId) || effect.Area < 0 || effect.Duration < 0 ||
                !std::isfinite(effect.Magnitude) ||
                std::abs(effect.Magnitude) > kMaximumInventoryScalarMagnitude ||
                !std::isfinite(effect.RawCost) || effect.RawCost < 0.0F ||
                effect.RawCost > kMaximumInventoryScalarMagnitude)
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsValidAssignmentFactions(const Factions& acFactions) noexcept
{
    if (!acFactions.IsDecodedValid || acFactions.NpcFactions.size() > Factions::kMaximumWireEntries ||
        acFactions.ExtraFactions.size() > Factions::kMaximumWireEntries)
        return false;

    const auto validList = [](const auto& acList) noexcept {
        for (std::size_t index = 0; index < acList.size(); ++index)
        {
            if (!IsValidRequiredGameId(acList[index].Id))
                return false;
            for (std::size_t prior = 0; prior < index; ++prior)
                if (acList[prior].Id == acList[index].Id)
                    return false;
        }
        return true;
    };
    return validList(acFactions.NpcFactions) && validList(acFactions.ExtraFactions);
}

[[nodiscard]] bool IsValidAssignmentQuests(const QuestLog& acQuests) noexcept
{
    if (!acQuests.IsDecodedValid ||
        acQuests.Entries.size() > SkyrimTogetherVR::VRAssignmentLimits::kMaximumQuestEntries)
        return false;
    for (std::size_t index = 0; index < acQuests.Entries.size(); ++index)
    {
        if (!IsValidRequiredGameId(acQuests.Entries[index].Id))
            return false;
        for (std::size_t prior = 0; prior < index; ++prior)
            if (acQuests.Entries[prior].Id == acQuests.Entries[index].Id)
                return false;
    }
    return true;
}

[[nodiscard]] bool IsValidAssignmentTints(const Tints& acTints, const bool aIsVR) noexcept
{
    const auto maximumTints = aIsVR ? static_cast<std::size_t>(VRAppearance::kMaximumTints) :
                                      Tints::kMaximumWireEntries;
    if (!acTints.IsDecodedValid || acTints.Entries.size() > maximumTints)
        return false;
    for (const auto& tint : acTints.Entries)
        if (!tint.Name.IsDecodedValid || tint.Name.size() > CachedString::kMaximumWireBytes ||
            tint.Type >= 15 || !std::isfinite(tint.Alpha) || tint.Alpha < 0.0F || tint.Alpha > 1.0F)
            return false;
    return true;
}

[[nodiscard]] bool IsValidVRAppearanceIdentity(const VRAppearance& acAppearance) noexcept
{
    if (!acAppearance.IsValid() || !IsValidRequiredGameId(acAppearance.RaceId) ||
        !IsValidOptionalGameId(acAppearance.HairColorId) ||
        !IsValidOptionalGameId(acAppearance.FaceTextureId))
        return false;
    for (std::uint8_t index = 0; index < acAppearance.HeadPartCount; ++index)
        if (!IsValidRequiredGameId(acAppearance.HeadParts[index].FormId))
            return false;
    return true;
}

[[nodiscard]] bool IsValidActorData(const ActorData& acActorData, const bool aIsVR) noexcept
{
    return acActorData.IsDecodedValid() && IsValidActorValues(acActorData.InitialActorValues, aIsVR) &&
           IsValidAssignmentInventory(acActorData.InitialInventory, aIsVR);
}

[[nodiscard]] bool IsValidAssignmentRequest(const AssignCharacterRequest& acMessage,
                                            const Player& acPlayer) noexcept
{
    const auto capabilities = acPlayer.GetGameplayCapabilities();
    const bool isVR = SkyrimTogether::Protocol::IsVrClient(capabilities);
    const bool supportsVRAppearance = SkyrimTogether::Protocol::HasCapability(
        capabilities, SkyrimTogether::Protocol::GameplayCapability::VRAppearanceRelay);
    const bool isPlayer = acMessage.ReferenceId.ModId == 0 && acMessage.ReferenceId.BaseId == 0x14;
    const bool isTemporary = acMessage.ReferenceId.ModId == std::numeric_limits<std::uint32_t>::max();

    if (!acMessage.IsDecodedValid() ||
        !IsValidRequiredGameId(acMessage.ReferenceId) || !IsValidRequiredGameId(acMessage.CellId) ||
        !IsValidOptionalGameId(acMessage.WorldSpaceId) ||
        (!isPlayer && (isVR || isTemporary) && !IsValidRequiredGameId(acMessage.FormId)) ||
        (!isPlayer && !isVR && !isTemporary && !IsValidOptionalGameId(acMessage.FormId)) ||
        (isPlayer && acMessage.FormId) ||
        !std::isfinite(acMessage.Position.x) || !std::isfinite(acMessage.Position.y) ||
        !std::isfinite(acMessage.Position.z) || !std::isfinite(acMessage.Rotation.x) ||
        !std::isfinite(acMessage.Rotation.y) || !IsValidActorData(acMessage.CurrentActorData, isVR) ||
        !IsValidAssignmentFactions(acMessage.FactionsContent) ||
        !IsValidAssignmentQuests(acMessage.QuestContent) ||
        !IsValidAssignmentTints(acMessage.FaceTints, isVR) ||
        (!acMessage.HasQuestContent && !acMessage.QuestContent.Entries.empty()) ||
        (!acMessage.HasFaceTints && !acMessage.FaceTints.Entries.empty()) ||
        (HasAssignmentAction(acMessage.LatestAction) && !IsValidAssignmentAction(acMessage.LatestAction)))
        return false;

    if (!isVR)
        return !acMessage.HasVRAppearance;

    if (!supportsVRAppearance)
        return false;

    if (!isPlayer)
        return acMessage.HasVRAppearance && acMessage.AppearanceBuffer.empty() &&
               acMessage.ChangeFlags == 0 && !acMessage.HasQuestContent && !acMessage.HasFaceTints &&
               acMessage.InitialVRAppearance.Sequence == 1 &&
               acMessage.InitialVRAppearance.TintCount == 0 &&
               acMessage.FaceTints.Entries.empty() &&
               IsValidVRAppearanceIdentity(acMessage.InitialVRAppearance);

    if (!acMessage.HasVRAppearance ||
        !acMessage.AppearanceBuffer.empty() || acMessage.ChangeFlags != 0 ||
        acMessage.InitialVRAppearance.Sequence != 1 ||
        !IsValidVRAppearanceIdentity(acMessage.InitialVRAppearance) ||
        acMessage.InitialVRAppearance.TintCount != acMessage.FaceTints.Entries.size())
        return false;

    for (std::uint8_t index = 0; index < acMessage.InitialVRAppearance.TintCount; ++index)
    {
        const auto& semantic = acMessage.InitialVRAppearance.Tints[index];
        const auto& legacy = acMessage.FaceTints.Entries[index];
        if (semantic.Type != legacy.Type || semantic.Color != legacy.Color || semantic.Alpha != legacy.Alpha)
            return false;
    }
    return true;
}

[[nodiscard]] bool HasAssignmentAction(const ActionEvent& acAction) noexcept
{
    return static_cast<bool>(acAction.ActionId) || !acAction.EventName.empty();
}

[[nodiscard]] bool IsValidAssignmentAction(const ActionEvent& acAction) noexcept
{
    const auto validRequiredForm = [](const GameId& acId) noexcept { return acId.BaseId != 0; };
    const auto validOptionalForm = [&validRequiredForm](const GameId& acId) noexcept {
        return !acId || validRequiredForm(acId);
    };
    return acAction.IsDecodedValid && acAction.EventName.IsDecodedValid &&
           acAction.TargetEventName.IsDecodedValid && acAction.Variables.IsDecodedValid &&
           validOptionalForm(acAction.ActionId) && validOptionalForm(acAction.TargetId) &&
           validOptionalForm(acAction.IdleId) && (acAction.Type & ~0x7u) == 0 &&
           (acAction.ActionId || (!acAction.TargetId && !acAction.IdleId)) &&
           acAction.EventName.size() <= 127 && acAction.TargetEventName.size() <= 127 &&
           std::find(acAction.EventName.begin(), acAction.EventName.end(), '\0') == acAction.EventName.end() &&
           std::find(acAction.TargetEventName.begin(), acAction.TargetEventName.end(), '\0') ==
               acAction.TargetEventName.end() &&
           ((acAction.Variables.Booleans.empty() && acAction.Variables.Floats.empty() &&
             acAction.Variables.Integers.empty()) ||
            (SkyrimTogetherVR::AnimationGraphProtocol::IsKnownShape(
                 acAction.Variables.Booleans.size(), acAction.Variables.Floats.size(),
                 acAction.Variables.Integers.size()))) &&
           std::all_of(acAction.Variables.Floats.begin(), acAction.Variables.Floats.end(),
                       [](const float aValue) noexcept { return std::isfinite(aValue); });
}

[[nodiscard]] bool MatchesOwnershipGrantSession(const Player* apPlayer, const ConnectionId_t aConnectionId,
                                                const std::uint64_t aConnectionGeneration,
                                                const std::uint64_t aSessionNonce) noexcept
{
    return apPlayer && apPlayer->GetConnectionId() == aConnectionId &&
           apPlayer->GetConnectionGeneration() == aConnectionGeneration &&
           apPlayer->GetClientSessionNonce() == aSessionNonce;
}

void SendAssignmentRejection(const PacketEvent<AssignCharacterRequest>& acMessage)
{
    if (!acMessage.pPlayer || !acMessage.Packet.IsDecodedValid() || !SkyrimTogether::Protocol::CanReceiveAssignmentRejection(acMessage.pPlayer->GetGameplayCapabilities()))
        return;

    AssignCharacterResponse response{};
    response.Cookie = acMessage.Packet.Cookie;
    response.ServerId = 0;
    response.Owner = false;
    if (acMessage.Packet.ReferenceId.ModId == 0 && acMessage.Packet.ReferenceId.BaseId == 0x14)
        response.PlayerId = acMessage.pPlayer->GetId();

    acMessage.pPlayer->Send(response);
}
}

CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&CharacterService::OnUpdate>(this))
    , m_interiorCellChangeEventConnection(aDispatcher.sink<CharacterInteriorCellChangeEvent>().connect<&CharacterService::OnCharacterInteriorCellChange>(this))
    , m_exteriorCellChangeEventConnection(aDispatcher.sink<CharacterExteriorCellChangeEvent>().connect<&CharacterService::OnCharacterExteriorCellChange>(this))
    , m_characterAssignRequestConnection(aDispatcher.sink<PacketEvent<AssignCharacterRequest>>().connect<&CharacterService::OnAssignCharacterRequest>(this))
    , m_transferOwnershipConnection(aDispatcher.sink<PacketEvent<RequestOwnershipTransfer>>().connect<&CharacterService::OnOwnershipTransferRequest>(this))
    , m_ownershipTransferEventConnection(aDispatcher.sink<OwnershipTransferEvent>().connect<&CharacterService::OnOwnershipTransferEvent>(this))
    , m_claimOwnershipConnection(aDispatcher.sink<PacketEvent<RequestOwnershipClaim>>().connect<&CharacterService::OnOwnershipClaimRequest>(this))
    , m_removeCharacterConnection(aDispatcher.sink<CharacterRemoveEvent>().connect<&CharacterService::OnCharacterRemoveEvent>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&CharacterService::OnPlayerLeave>(this))
    , m_characterSpawnedConnection(aDispatcher.sink<CharacterSpawnedEvent>().connect<&CharacterService::OnCharacterSpawned>(this))
    , m_referenceMovementSnapshotConnection(aDispatcher.sink<PacketEvent<ClientReferencesMoveRequest>>().connect<&CharacterService::OnReferencesMoveRequest>(this))
    , m_actorActionRequestConnection(aDispatcher.sink<PacketEvent<ClientActorActionRequest>>().connect<&CharacterService::OnActorActionRequest>(this))
    , m_factionsChangesConnection(aDispatcher.sink<PacketEvent<RequestFactionsChanges>>().connect<&CharacterService::OnFactionsChanges>(this))
    , m_mountConnection(aDispatcher.sink<PacketEvent<MountRequest>>().connect<&CharacterService::OnMountRequest>(this))
    , m_newPackageConnection(aDispatcher.sink<PacketEvent<NewPackageRequest>>().connect<&CharacterService::OnNewPackageRequest>(this))
    , m_requestRespawnConnection(aDispatcher.sink<PacketEvent<RequestRespawn>>().connect<&CharacterService::OnRequestRespawn>(this))
    , m_syncExperienceConnection(aDispatcher.sink<PacketEvent<SyncExperienceRequest>>().connect<&CharacterService::OnSyncExperienceRequest>(this))
    , m_dialogueConnection(aDispatcher.sink<PacketEvent<DialogueRequest>>().connect<&CharacterService::OnDialogueRequest>(this))
    , m_subtitleConnection(aDispatcher.sink<PacketEvent<SubtitleRequest>>().connect<&CharacterService::OnSubtitleRequest>(this))
{
}

bool CharacterService::Serialize(World& aRegistry, entt::entity aEntity,
                                 CharacterSpawnRequest* apSpawnRequest) noexcept try
{
    if (!apSpawnRequest)
        return false;

    CharacterSpawnRequest serialized{};
    const auto& characterComponent = aRegistry.get<CharacterComponent>(aEntity);
    const auto semanticNpc = characterComponent.HasVRAppearance && !characterComponent.IsPlayer();
    if ((characterComponent.HasVRAppearance && !characterComponent.InitialVRAppearance.IsValid()) ||
        (semanticNpc && (characterComponent.InitialVRAppearance.TintCount != 0 ||
                         !characterComponent.SaveBuffer.empty() || characterComponent.ChangeFlags != 0 ||
                         !characterComponent.FaceTints.Entries.empty())))
        return false;

    serialized.ServerId = World::ToInteger(aEntity);
    serialized.AppearanceBuffer = characterComponent.SaveBuffer;
    serialized.ChangeFlags = characterComponent.ChangeFlags;
    serialized.FaceTints = characterComponent.FaceTints;
    serialized.FactionsContent = characterComponent.FactionsContent;
    serialized.IsDead = characterComponent.IsDead();
    serialized.IsPlayer = characterComponent.IsPlayer();
    serialized.IsWeaponDrawn = characterComponent.IsWeaponDrawn();
    serialized.IsPlayerSummon = characterComponent.IsPlayerSummon();
    serialized.PlayerId = characterComponent.PlayerId;
    serialized.HasVRAppearance = characterComponent.HasVRAppearance;
    if (serialized.HasVRAppearance)
        serialized.InitialVRAppearance = characterComponent.InitialVRAppearance;

    const auto* pFormIdComponent = aRegistry.try_get<FormIdComponent>(aEntity);
    if (pFormIdComponent)
    {
        serialized.FormId = pFormIdComponent->Id;
    }

    const auto* pInventoryComponent = aRegistry.try_get<InventoryComponent>(aEntity);
    if (pInventoryComponent)
    {
        serialized.InventoryContent = pInventoryComponent->Content;
    }

    const auto* pActorValuesComponent = aRegistry.try_get<ActorValuesComponent>(aEntity);
    if (pActorValuesComponent)
    {
        serialized.InitialActorValues = pActorValuesComponent->CurrentActorValues;
    }

    if (characterComponent.BaseId)
    {
        serialized.BaseId = characterComponent.BaseId.Id;
    }

    const auto* pMovementComponent = aRegistry.try_get<MovementComponent>(aEntity);
    if (pMovementComponent)
    {
        serialized.Position = pMovementComponent->Position;
        serialized.Rotation.x = pMovementComponent->Rotation.x;
        serialized.Rotation.y = pMovementComponent->Rotation.z;
    }

    const auto* pCellIdComponent = aRegistry.try_get<CellIdComponent>(aEntity);
    if (pCellIdComponent)
    {
        serialized.CellId = pCellIdComponent->Cell;
    }

    auto& animationComponent = aRegistry.get<AnimationComponent>(aEntity);
    serialized.ActionsToReplay = animationComponent.ActionsReplayCache.FormRefinedReplayChain();

    using std::swap;
    swap(*apSpawnRequest, serialized);
    return true;
}
catch (...)
{
    LogCharacterServiceFailure("serialization");
    return false;
}

void CharacterService::OnUpdate(const UpdateEvent&) const noexcept try
{
    ExpireOwnershipGrants();
    ProcessFactionsChanges();
    ProcessMovementChanges();
}
catch (...)
{
    LogCharacterServiceFailure("update processing");
}

void CharacterService::OnCharacterExteriorCellChange(const CharacterExteriorCellChangeEvent& acEvent) const noexcept try
{
    CharacterSpawnRequest spawnMessage;
    if (!Serialize(m_world, acEvent.Entity, &spawnMessage))
        return;

    NotifyRemoveCharacter removeMessage;
    removeMessage.ServerId = World::ToInteger(acEvent.Entity);

    for (auto pPlayer : m_world.GetPlayerManager())
    {
        if (acEvent.Owner == pPlayer)
            continue;

        if (pPlayer->GetCellComponent().WorldSpaceId != acEvent.WorldSpaceId || pPlayer->GetCellComponent().WorldSpaceId == acEvent.WorldSpaceId && !GridCellCoords::IsCellInGridCell(acEvent.CurrentCoords, pPlayer->GetCellComponent().CenterCoords, false))
        {
            pPlayer->Send(removeMessage);
        }
        else if (pPlayer->GetCellComponent().WorldSpaceId == acEvent.WorldSpaceId && GridCellCoords::IsCellInGridCell(acEvent.CurrentCoords, pPlayer->GetCellComponent().CenterCoords, false))
        {
            pPlayer->Send(spawnMessage);
        }
    }
}
catch (...)
{
    LogCharacterServiceFailure("exterior-cell character fanout");
}

void CharacterService::OnCharacterInteriorCellChange(const CharacterInteriorCellChangeEvent& acEvent) const noexcept try
{
    CharacterSpawnRequest spawnMessage;
    if (!Serialize(m_world, acEvent.Entity, &spawnMessage))
        return;

    NotifyRemoveCharacter removeMessage;
    removeMessage.ServerId = World::ToInteger(acEvent.Entity);

    for (auto pPlayer : m_world.GetPlayerManager())
    {
        if (acEvent.Owner == pPlayer)
            continue;

        if (acEvent.NewCell == pPlayer->GetCellComponent().Cell)
            pPlayer->Send(spawnMessage);
        else
            pPlayer->Send(removeMessage);
    }
}
catch (...)
{
    LogCharacterServiceFailure("interior-cell character fanout");
}

void CharacterService::OnAssignCharacterRequest(const PacketEvent<AssignCharacterRequest>& acMessage) const noexcept try
{
    if (!acMessage.pPlayer)
        return;

    if (!IsValidAssignmentRequest(acMessage.Packet, *acMessage.pPlayer))
    {
        SendAssignmentRejection(acMessage);
        spdlog::warn("Client {:X} sent an invalid character assignment payload",
                     acMessage.pPlayer->GetConnectionId());
        return;
    }

    auto& message = acMessage.Packet;
    const auto& refId = message.ReferenceId;

    const auto isPlayer = (refId.ModId == 0 && refId.BaseId == 0x14);
    const auto isCustom = isPlayer || refId.ModId == std::numeric_limits<uint32_t>::max();
    if (!isPlayer && !SkyrimTogether::Protocol::CanOwnNpc(acMessage.pPlayer->GetGameplayCapabilities()))
    {
        SendAssignmentRejection(acMessage);
        spdlog::warn("VR client {:X} attempted NPC assignment without negotiated NPC ownership capability",
                     acMessage.pPlayer->GetConnectionId());
        return;
    }

    // Player assignment is retried by the client until its response arrives.
    // Unlike persistent references, the player has no FormIdComponent lookup,
    // so use the session-owned entity to make those retries idempotent.
    if (isPlayer)
    {
        const auto existing = acMessage.pPlayer->GetCharacter();
        if (existing)
        {
            if (!m_world.valid(*existing) ||
                !m_world.all_of<OwnerComponent, CharacterComponent>(*existing) ||
                m_world.get<OwnerComponent>(*existing).GetOwner() != acMessage.pPlayer ||
                !m_world.get<CharacterComponent>(*existing).IsPlayer())
            {
                spdlog::error("Player {:X} has an invalid retained character assignment",
                              acMessage.pPlayer->GetConnectionId());
                return;
            }

            AssignCharacterResponse response{};
            response.Cookie = message.Cookie;
            response.ServerId = World::ToInteger(*existing);
            response.PlayerId = acMessage.pPlayer->GetId();
            response.Owner = true;
            acMessage.pPlayer->Send(response);
            return;
        }
    }

    // Check if id is the player
    if (!isCustom)
    {
        // Look for the character
        auto view = m_world.view<FormIdComponent, ActorValuesComponent, CharacterComponent, MovementComponent, CellIdComponent, OwnerComponent, InventoryComponent>();

        const auto itor = std::find_if(
            std::begin(view), std::end(view),
            [view, refId](auto entity)
            {
                const auto& formIdComponent = view.get<FormIdComponent>(entity);

                return formIdComponent.Id == refId;
            });

        if (itor != std::end(view))
        {
            // This entity already has an owner
            spdlog::debug("FormId: {:x}:{:x} is already managed", refId.ModId, refId.BaseId);

            auto& actorValuesComponent = view.get<ActorValuesComponent>(*itor);
            auto& inventoryComponent = view.get<InventoryComponent>(*itor);
            auto& characterComponent = view.get<CharacterComponent>(*itor);
            auto& movementComponent = view.get<MovementComponent>(*itor);
            auto& cellIdComponent = view.get<CellIdComponent>(*itor);
            auto& ownerComponent = view.get<OwnerComponent>(*itor);

            auto& partyService = m_world.GetPartyService();

            bool isOwner = false;

            if (partyService.IsPlayerInParty(acMessage.pPlayer) && partyService.IsPlayerLeader(acMessage.pPlayer) && !characterComponent.IsMount())
            {
                PartyService::Party* pParty = partyService.GetPlayerParty(acMessage.pPlayer);
                Player* pOwningPlayer = view.get<OwnerComponent>(*itor).GetOwner();

                // Transfer ownership if owning player is in the same party as the owner
                if (std::find(pParty->Members.begin(), pParty->Members.end(), pOwningPlayer) != pParty->Members.end())
                {
                    isOwner = TransferOwnership(acMessage.pPlayer, World::ToInteger(*itor),
                                                  acMessage.Packet.CurrentActorData);
                }
            }

            AssignCharacterResponse response{};
            response.Cookie = message.Cookie;
            response.ServerId = World::ToInteger(*itor);
            response.Owner = isOwner;
            response.AllActorValues = actorValuesComponent.CurrentActorValues;
            response.CurrentInventory = inventoryComponent.Content;
            response.IsDead = characterComponent.IsDead();
            response.IsWeaponDrawn = characterComponent.IsWeaponDrawn();
            response.PlayerId = characterComponent.PlayerId;
            response.Position = movementComponent.Position;
            response.CellId = cellIdComponent.Cell;
            response.WorldSpaceId = cellIdComponent.WorldSpaceId;

            if (auto* pAnimationComponent = m_world.try_get<AnimationComponent>(*itor))
            {
                response.ActionsToReplay = pAnimationComponent->ActionsReplayCache.FormRefinedReplayChain();
            }

            acMessage.pPlayer->Send(response);

            return;
        }
    }

    // This entity has no owner create it
    CreateCharacter(acMessage);
}
catch (...)
{
    LogCharacterServiceFailure("assignment request");
}

void CharacterService::OnOwnershipTransferRequest(const PacketEvent<RequestOwnershipTransfer>& acMessage) const noexcept try
{
    auto& message = acMessage.Packet;
    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);

    if (!acMessage.pPlayer || message.ServerId == 0 || !m_world.valid(cEntity) ||
        !m_world.all_of<OwnerComponent, CharacterComponent, CellIdComponent, ActorValuesComponent, InventoryComponent>(cEntity))
    {
        if (acMessage.pPlayer)
            spdlog::warn("Client {:X} requested ownership transfer of an invalid entity, server id: {:X}",
                         acMessage.pPlayer->GetConnectionId(), message.ServerId);
        return;
    }

    auto& characterOwnerComponent = m_world.get<OwnerComponent>(cEntity);
    auto& characterComponent = m_world.get<CharacterComponent>(cEntity);
    if (characterOwnerComponent.GetOwner() != acMessage.pPlayer)
    {
        spdlog::warn("Client {:X} requested ownership transfer of entity {:X} without ownership",
                     acMessage.pPlayer->GetConnectionId(), message.ServerId);
        return;
    }
    if (characterComponent.IsPlayerSummon())
    {
        spdlog::info("Client {:X} requested ownership transfer of an orphaned summon, serverid id: {:X}", acMessage.pPlayer->GetConnectionId(), message.ServerId);
        m_pendingOwnershipGrants.erase(message.ServerId);
        m_world.GetDispatcher().trigger(CharacterRemoveEvent(message.ServerId));
        return;
    }
    if (!characterComponent.IsPlayer() && !SkyrimTogether::Protocol::CanOwnNpc(acMessage.pPlayer->GetGameplayCapabilities()))
    {
        spdlog::warn("Client {:X} requested NPC ownership transfer without negotiated capability",
                     acMessage.pPlayer->GetConnectionId());
        return;
    }

    const auto invalidOwnerIt = std::find(characterOwnerComponent.InvalidOwners.begin(),
                                          characterOwnerComponent.InvalidOwners.end(), acMessage.pPlayer);
    const bool addInvalidOwner = invalidOwnerIt == characterOwnerComponent.InvalidOwners.end();
    if (addInvalidOwner)
        characterOwnerComponent.InvalidOwners.reserve(characterOwnerComponent.InvalidOwners.size() + 1);

    if (message.WorldSpaceId || message.CellId)
    {
        if (!m_world.all_of<FormIdComponent, MovementComponent>(cEntity))
            return;
        auto& formIdComponent = m_world.get<FormIdComponent>(cEntity);

        NotifyActorTeleport notify{};
        notify.FormId = formIdComponent.Id;
        notify.WorldSpaceId = message.WorldSpaceId;
        notify.CellId = message.CellId;
        notify.Position = message.Position;

        auto& cellIdComponent = m_world.get<CellIdComponent>(cEntity);
        cellIdComponent.WorldSpaceId = message.WorldSpaceId;
        cellIdComponent.Cell = message.CellId;
        cellIdComponent.CenterCoords = GridCellCoords::CalculateGridCellCoords(message.Position);

        auto& movementComponent = m_world.get<MovementComponent>(cEntity);
        movementComponent.Position = message.Position;
        movementComponent.Sent = true;

        GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
    }

    if (addInvalidOwner)
        characterOwnerComponent.InvalidOwners.push_back(acMessage.pPlayer);
    m_pendingOwnershipGrants.erase(message.ServerId);

    m_world.GetDispatcher().trigger(OwnershipTransferEvent(cEntity));
}
catch (...)
{
    LogCharacterServiceFailure("ownership-transfer request");
}

void CharacterService::OnOwnershipTransferEvent(const OwnershipTransferEvent& acEvent) const noexcept try
{
    const auto serverId = World::ToInteger(acEvent.Entity);
    m_pendingOwnershipGrants.erase(serverId);
    if (!m_world.valid(acEvent.Entity) ||
        !m_world.all_of<OwnerComponent, CharacterComponent, CellIdComponent, ActorValuesComponent, InventoryComponent>(acEvent.Entity))
        return;

    auto& characterComponent = m_world.get<CharacterComponent>(acEvent.Entity);
    auto& ownerComponent = m_world.get<OwnerComponent>(acEvent.Entity);
    auto& cellIdComponent = m_world.get<CellIdComponent>(acEvent.Entity);
    auto* const pCurrentOwner = ownerComponent.GetOwner();
    if (!pCurrentOwner)
        return;

    NotifyOwnershipTransfer response;
    response.ServerId = serverId;

    bool foundOwner = false;
    for (auto pPlayer : m_world.GetPlayerManager())
    {
        if (ownerComponent.GetOwner() == pPlayer)
            continue;
        if (!SkyrimTogether::Protocol::CanOwnNpc(pPlayer->GetGameplayCapabilities()))
            continue;

        bool isPlayerInvalid = false;
        for (const auto invalidOwner : ownerComponent.InvalidOwners)
        {
            isPlayerInvalid = invalidOwner == pPlayer;
            if (isPlayerInvalid)
                break;
        }

        if (isPlayerInvalid)
            continue;

        if (!pPlayer->GetCellComponent().IsInRange(cellIdComponent, characterComponent.IsDragon()))
            continue;

        if (characterComponent.IsPlayer())
        {
            pPlayer->Send(response);
            ownerComponent.SetOwner(pPlayer);
            if (auto* movementComponent = m_world.try_get<MovementComponent>(acEvent.Entity))
                movementComponent->HasTick = false;
            foundOwner = true;
            break;
        }

        PendingOwnershipGrant grant{};
        grant.pCurrentOwner = pCurrentOwner;
        grant.pSelectedPlayer = pPlayer;
        grant.CurrentOwnerConnectionId = pCurrentOwner->GetConnectionId();
        grant.CurrentOwnerConnectionGeneration = pCurrentOwner->GetConnectionGeneration();
        grant.CurrentOwnerSessionNonce = pCurrentOwner->GetClientSessionNonce();
        grant.SelectedConnectionId = pPlayer->GetConnectionId();
        grant.SelectedConnectionGeneration = pPlayer->GetConnectionGeneration();
        grant.SelectedSessionNonce = pPlayer->GetClientSessionNonce();
        grant.ExpiresAt = std::chrono::steady_clock::now() + kOwnershipGrantLifetime;
        do {
            grant.Token = m_nextOwnershipGrantToken++;
            if (m_nextOwnershipGrantToken == 0)
                m_nextOwnershipGrantToken = 1;
        } while (grant.Token == 0 || std::any_of(m_pendingOwnershipGrants.begin(), m_pendingOwnershipGrants.end(),
                                                  [&grant](const auto& acEntry) { return acEntry.second.Token == grant.Token; }));
        m_pendingOwnershipGrants.emplace(serverId, grant);
        response.GrantToken = grant.Token;

        pPlayer->Send(response);

        foundOwner = true;
        break;
    }

    if (!foundOwner)
        m_world.GetDispatcher().trigger(CharacterRemoveEvent(response.ServerId));
}
catch (...)
{
    LogCharacterServiceFailure("ownership-transfer selection");
}

void CharacterService::OnCharacterRemoveEvent(const CharacterRemoveEvent& acEvent) const noexcept
{
    try
    {
        m_pendingOwnershipGrants.erase(acEvent.ServerId);
        const auto entity = static_cast<entt::entity>(acEvent.ServerId);
        if (!m_world.valid(entity) || !m_world.all_of<OwnerComponent>(entity))
            return;
        const auto& characterOwnerComponent = m_world.get<OwnerComponent>(entity);

        try
        {
            GameServer::Get()->GetWorld().GetScriptService().HandleCharacterDestoy(entity);
        }
        catch (...)
        {
            LogCharacterServiceFailure("character-destroy script callback");
        }

        NotifyRemoveCharacter response;
        response.ServerId = acEvent.ServerId;

        for (auto pPlayer : m_world.GetPlayerManager())
        {
            if (characterOwnerComponent.GetOwner() == pPlayer)
                continue;

            try
            {
                pPlayer->Send(response);
            }
            catch (...)
            {
                LogCharacterServiceFailure("character-removal notification");
            }
        }

        m_world.destroy(entity);
        spdlog::debug("Character destroyed {:X}", acEvent.ServerId);
    }
    catch (...)
    {
        LogCharacterServiceFailure("character removal");
    }
}

void CharacterService::OnOwnershipClaimRequest(const PacketEvent<RequestOwnershipClaim>& acMessage) const noexcept try
{
    const auto& message = acMessage.Packet;
    const auto entity = static_cast<entt::entity>(message.ServerId);
    if (!acMessage.pPlayer || message.ServerId == 0 || !m_world.valid(entity) ||
        !m_world.all_of<OwnerComponent, CharacterComponent>(entity))
        return;

    const auto& characterComponent = m_world.get<CharacterComponent>(entity);
    if (characterComponent.IsPlayer())
    {
        // Player-character ownership keeps the legacy protocol behavior.
        static_cast<void>(TransferOwnership(acMessage.pPlayer, message.ServerId, message.NewActorData));
        return;
    }

    const auto grantIt = m_pendingOwnershipGrants.find(message.ServerId);
    if (message.GrantToken == 0 || grantIt == m_pendingOwnershipGrants.end())
        return;
    const auto grant = grantIt->second;
    if (grant.Token != message.GrantToken ||
        grant.pSelectedPlayer != acMessage.pPlayer ||
        !MatchesOwnershipGrantSession(acMessage.pPlayer, grant.SelectedConnectionId,
                                      grant.SelectedConnectionGeneration, grant.SelectedSessionNonce))
        return;
    if (grant.ExpiresAt <= std::chrono::steady_clock::now())
    {
        m_pendingOwnershipGrants.erase(grantIt);
        return;
    }

    // Once the selected session presents the exact grant, consume it even if
    // subsequent entity/range/old-owner validation fails.
    m_pendingOwnershipGrants.erase(grantIt);
    const auto* pGrantOwner = m_world.GetPlayerManager().GetByConnectionId(grant.CurrentOwnerConnectionId);
    if (!pGrantOwner || pGrantOwner != grant.pCurrentOwner ||
        !MatchesOwnershipGrantSession(pGrantOwner, grant.CurrentOwnerConnectionId,
                                      grant.CurrentOwnerConnectionGeneration,
                                      grant.CurrentOwnerSessionNonce))
        return;

    if (!m_world.all_of<OwnerComponent, CharacterComponent, CellIdComponent, ActorValuesComponent, InventoryComponent>(entity))
        return;
    const auto& ownerComponent = m_world.get<OwnerComponent>(entity);
    if (ownerComponent.GetOwner() != grant.pCurrentOwner ||
        !SkyrimTogether::Protocol::CanOwnNpc(acMessage.pPlayer->GetGameplayCapabilities()) ||
        !acMessage.pPlayer->GetCellComponent().IsInRange(m_world.get<CellIdComponent>(entity), characterComponent.IsDragon()))
        return;

    static_cast<void>(TransferOwnership(acMessage.pPlayer, message.ServerId, message.NewActorData));
}
catch (...)
{
    LogCharacterServiceFailure("ownership claim");
}

void CharacterService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) const noexcept try
{
    if (!acEvent.pPlayer)
        return;
    for (auto it = m_pendingOwnershipGrants.begin(); it != m_pendingOwnershipGrants.end();) {
        if (it->second.pCurrentOwner == acEvent.pPlayer || it->second.pSelectedPlayer == acEvent.pPlayer)
            it = m_pendingOwnershipGrants.erase(it);
        else
            ++it;
    }
}
catch (...)
{
    LogCharacterServiceFailure("ownership-grant cleanup");
}

void CharacterService::OnCharacterSpawned(const CharacterSpawnedEvent& acEvent) const noexcept try
{
    CharacterSpawnRequest message;
    if (!Serialize(m_world, acEvent.Entity, &message))
        return;

    const auto& ownerComp = m_world.get<OwnerComponent>(acEvent.Entity);
    if (!GameServer::Get()->SendToPlayersInRange(message, acEvent.Entity, ownerComp.GetOwner()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

    GameServer::Get()->GetWorld().GetScriptService().HandleCharacterSpawn(acEvent.Entity);
}
catch (...)
{
    LogCharacterServiceFailure("character-spawn fanout");
}

void CharacterService::OnReferencesMoveRequest(const PacketEvent<ClientReferencesMoveRequest>& acMessage) const noexcept try
{
    if (!acMessage.pPlayer || !acMessage.Packet.IsDecodedValid)
        return;

    OwnerView<AnimationComponent, MovementComponent, CellIdComponent> view(m_world, acMessage.GetSender());

    auto& message = acMessage.Packet;

    for (auto& entry : message.Updates)
    {
        const auto entity = static_cast<entt::entity>(entry.first);

        auto itor = view.find(entity);
        if (itor == std::end(view))
        {
            spdlog::debug("{:x} requested move of {:x} but does not exist or is not owned by the sender",
                          acMessage.pPlayer->GetConnectionId(), entry.first);
            continue;
        }

        auto& movementComponent = view.get<MovementComponent>(*itor);
        auto& cellIdComponent = view.get<CellIdComponent>(*itor);
        auto& animationComponent = view.get<AnimationComponent>(*itor);

        if (!SkyrimTogether::Protocol::IsNewerMovementTick(movementComponent.HasTick, movementComponent.Tick, message.Tick))
            continue;

        auto& update = entry.second;
        auto& movement = update.UpdatedMovement;

        AnimationVariables variables = movement.Variables;
        ActionEvent currentAction = animationComponent.CurrentAction;
        Vector<ActionEvent> pendingActions = animationComponent.Actions;

        for (auto& action : update.ActionEvents)
        {
            auto [canceled, reason] = GameServer::Get()->GetWorld().GetScriptService().HandleCharacterMove(entity);
            if (canceled)
                continue;

            currentAction = action;
            pendingActions.push_back(currentAction);
        }

        const auto centerCoords = GridCellCoords::CalculateGridCellCoords(movement.Position.x, movement.Position.y);

        movementComponent.Tick = message.Tick;
        movementComponent.HasTick = true;
        movementComponent.Position = movement.Position;
        movementComponent.Rotation = glm::vec3(movement.Rotation.x, 0.f, movement.Rotation.y);
        SwapAnimationVariables(movementComponent.Variables, variables);
        movementComponent.Direction = movement.Direction;

        cellIdComponent.Cell = movement.CellId;
        cellIdComponent.WorldSpaceId = movement.WorldSpaceId;
        cellIdComponent.CenterCoords = centerCoords;

        std::swap(animationComponent.CurrentAction, currentAction);
        animationComponent.Actions.swap(pendingActions);
        animationComponent.ActionsReplayCache.AppendAll(update.ActionEvents);

        movementComponent.Sent = false;
    }
}
catch (...)
{
    LogCharacterServiceFailure("movement request");
}

void CharacterService::OnActorActionRequest(const PacketEvent<ClientActorActionRequest>& acMessage) const noexcept try
{
    const auto& message = acMessage.Packet;
    const auto& action = message.Action;
    const auto validRequiredForm = [](const GameId& acId) noexcept { return acId.BaseId != 0; };
    const auto validOptionalForm = [&validRequiredForm](const GameId& acId) noexcept {
        return !acId || validRequiredForm(acId);
    };
    if (!acMessage.pPlayer ||
        !SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(),
            SkyrimTogether::Protocol::GameplayCapability::ExactAnimationActions) ||
        !action.IsDecodedValid || !action.EventName.IsDecodedValid ||
        !action.TargetEventName.IsDecodedValid || !action.Variables.IsDecodedValid ||
        message.ServerId == 0 || !validRequiredForm(action.ActionId) ||
        !validOptionalForm(action.TargetId) || !validOptionalForm(action.IdleId) ||
        (action.Type & ~0x7u) != 0 ||
        action.EventName.size() > 127 || action.TargetEventName.size() > 127 ||
        std::find(action.EventName.begin(), action.EventName.end(), '\0') != action.EventName.end() ||
        std::find(action.TargetEventName.begin(), action.TargetEventName.end(), '\0') != action.TargetEventName.end() ||
        !SkyrimTogetherVR::AnimationGraphProtocol::IsKnownShape(
            action.Variables.Booleans.size(), action.Variables.Floats.size(),
            action.Variables.Integers.size()) ||
        !std::all_of(action.Variables.Floats.begin(), action.Variables.Floats.end(),
                     [](const float a_value) { return std::isfinite(a_value); }))
        return;

    const auto entity = static_cast<entt::entity>(message.ServerId);
    if (!m_world.valid(entity))
    {
        spdlog::debug("{:x} requested action for an invalid entity {:x}", acMessage.pPlayer->GetConnectionId(), message.ServerId);
        return;
    }

    OwnerView<CharacterComponent, AnimationComponent> view(m_world, acMessage.GetSender());
    const auto itor = view.find(entity);
    if (itor == std::end(view))
    {
        spdlog::debug("{:x} requested action for {:x} but it is not an owned character", acMessage.pPlayer->GetConnectionId(), message.ServerId);
        return;
    }

    auto [canceled, reason] = GameServer::Get()->GetWorld().GetScriptService().HandleCharacterMove(entity);
    if (canceled)
        return;

    auto& animationComponent = view.get<AnimationComponent>(*itor);
    ActionEvent currentAction = message.Action;
    Vector<ActionEvent> pendingActions = animationComponent.Actions;
    pendingActions.push_back(currentAction);

    std::swap(animationComponent.CurrentAction, currentAction);
    animationComponent.Actions.swap(pendingActions);
    animationComponent.ActionsReplayCache.Append(animationComponent.CurrentAction);
}
catch (...)
{
    LogCharacterServiceFailure("actor action request");
}

void CharacterService::OnFactionsChanges(const PacketEvent<RequestFactionsChanges>& acMessage) const noexcept try
{
    OwnerView<CharacterComponent> view(m_world, acMessage.GetSender());

    auto& message = acMessage.Packet;

    for (auto& [id, factions] : message.Changes)
    {
        auto it = view.find(static_cast<entt::entity>(id));

        if (it == std::end(view) || view.get<OwnerComponent>(*it).GetOwner() != acMessage.pPlayer)
            continue;

        auto& characterComponent = view.get<CharacterComponent>(*it);
        Factions replacement = factions;
        SwapFactions(characterComponent.FactionsContent, replacement);
        characterComponent.SetDirtyFactions(true);
    }
}
catch (...)
{
    LogCharacterServiceFailure("faction request");
}

void CharacterService::OnMountRequest(const PacketEvent<MountRequest>& acMessage) const noexcept try
{
    auto& message = acMessage.Packet;

    const auto rider = static_cast<entt::entity>(message.RiderId);
    const auto mount = static_cast<entt::entity>(message.MountId);
    if (!acMessage.pPlayer || message.RiderId == 0 || message.MountId == 0 || rider == mount ||
        !m_world.valid(rider) || !m_world.valid(mount) ||
        !m_world.all_of<CharacterComponent, OwnerComponent>(rider) ||
        !m_world.all_of<CharacterComponent, OwnerComponent>(mount) ||
        !m_world.all_of<CellIdComponent>(rider) || !m_world.all_of<CellIdComponent>(mount) ||
        m_world.get<OwnerComponent>(rider).GetOwner() != acMessage.pPlayer ||
        m_world.get<OwnerComponent>(mount).GetOwner() != acMessage.pPlayer ||
        !m_world.get<CharacterComponent>(mount).IsMount() ||
        !m_world.get<CellIdComponent>(rider).IsInRange(m_world.get<CellIdComponent>(mount), false))
        return;

    NotifyMount notify;
    notify.RiderId = message.RiderId;
    notify.MountId = message.MountId;

    const entt::entity cEntity = static_cast<entt::entity>(message.MountId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
catch (...)
{
    LogCharacterServiceFailure("mount fanout");
}

void CharacterService::OnNewPackageRequest(const PacketEvent<NewPackageRequest>& acMessage) const noexcept try
{
    auto& message = acMessage.Packet;
    const auto actor = static_cast<entt::entity>(message.ActorId);
    if (!acMessage.pPlayer || message.ActorId == 0 || !message.PackageId || !m_world.valid(actor) ||
        !m_world.all_of<CharacterComponent, OwnerComponent>(actor) ||
        m_world.get<OwnerComponent>(actor).GetOwner() != acMessage.pPlayer)
        return;

    NotifyNewPackage notify;
    notify.ActorId = message.ActorId;
    notify.PackageId = message.PackageId;

    if (!GameServer::Get()->SendToPlayersInRange(notify, actor, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
catch (...)
{
    LogCharacterServiceFailure("package fanout");
}

void CharacterService::OnRequestRespawn(const PacketEvent<RequestRespawn>& acMessage) const noexcept try
{
    if (!acMessage.pPlayer)
        return;

    auto view = m_world.view<OwnerComponent, CharacterComponent>();
    auto it = view.find(static_cast<entt::entity>(acMessage.Packet.ActorId));
    if (it == view.end())
    {
        spdlog::warn("No OwnerComponent found for actor id {:X}", acMessage.Packet.ActorId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);

    auto* const pAnimationComponent = m_world.try_get<AnimationComponent>(*it);
    if (!pAnimationComponent)
        return;

    if (ownerComponent.GetOwner() == acMessage.pPlayer)
    {
        if (!acMessage.Packet.AppearanceBuffer.empty())
        {
            auto& characterComponent = view.get<CharacterComponent>(*it);
            String appearanceBuffer = acMessage.Packet.AppearanceBuffer;
            using std::swap;
            swap(characterComponent.SaveBuffer, appearanceBuffer);
            characterComponent.ChangeFlags = acMessage.Packet.ChangeFlags;
        }

        // Replay cache needs to be cleared when a character respawns.
        pAnimationComponent->ActionsReplayCache.Clear();

        NotifyRespawn notify;
        notify.ActorId = acMessage.Packet.ActorId;

        if (!GameServer::Get()->SendToPlayersInRange(notify, *it, acMessage.GetSender()))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    }
    else
    {
        CharacterSpawnRequest message;
        if (!Serialize(m_world, *it, &message))
            return;

        acMessage.GetSender()->Send(message);
    }
}
catch (...)
{
    LogCharacterServiceFailure("respawn request");
}

void CharacterService::OnSyncExperienceRequest(const PacketEvent<SyncExperienceRequest>& acMessage) const noexcept try
{
    if (!bEnableXpSync || !acMessage.pPlayer || !std::isfinite(acMessage.Packet.Experience) ||
        acMessage.Packet.Experience <= 0.0F || acMessage.Packet.Experience > 100000.0F)
        return;

    NotifySyncExperience notify;
    notify.Experience = acMessage.Packet.Experience;

    const auto& partyComponent = acMessage.pPlayer->GetParty();
    GameServer::Get()->SendToParty(notify, partyComponent, acMessage.GetSender());
}
catch (...)
{
    LogCharacterServiceFailure("experience fanout");
}

void CharacterService::OnDialogueRequest(const PacketEvent<DialogueRequest>& acMessage) const noexcept try
{
    auto& message = acMessage.Packet;

    NotifyDialogue notify{};
    notify.ServerId = message.ServerId;
    notify.SoundFilename = message.SoundFilename;

    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
catch (...)
{
    LogCharacterServiceFailure("dialogue fanout");
}

void CharacterService::OnSubtitleRequest(const PacketEvent<SubtitleRequest>& acMessage) const noexcept try
{
    auto& message = acMessage.Packet;

    NotifySubtitle notify{};
    notify.ServerId = message.ServerId;
    notify.Text = message.Text;
    notify.TopicFormId = message.TopicFormId;

    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
catch (...)
{
    LogCharacterServiceFailure("subtitle fanout");
}

void CharacterService::CreateCharacter(const PacketEvent<AssignCharacterRequest>& acMessage) const noexcept try
{
    if (!acMessage.pPlayer || !IsValidAssignmentRequest(acMessage.Packet, *acMessage.pPlayer))
        return;

    auto& message = acMessage.Packet;
    const auto gameId = message.ReferenceId;
    const auto baseId = message.FormId;
    const auto isPlayer = (gameId.ModId == 0 && gameId.BaseId == 0x14);

    const auto capabilities = acMessage.pPlayer->GetGameplayCapabilities();
    const bool supportsVRAppearance = SkyrimTogether::Protocol::IsVrClient(capabilities) &&
                                      SkyrimTogether::Protocol::HasCapability(
                                          capabilities,
                                          SkyrimTogether::Protocol::GameplayCapability::VRAppearanceRelay);
    if (message.HasVRAppearance != supportsVRAppearance ||
        (message.HasVRAppearance && !message.InitialVRAppearance.IsValid()))
    {
        spdlog::warn("Player {:x} sent a missing or invalid negotiated initial appearance",
                     acMessage.pPlayer->GetConnectionId());
        return;
    }

    QuestLog previousQuestContent{};
    QuestLog replacementQuestContent{};
    if (isPlayer && message.HasQuestContent)
    {
        previousQuestContent = acMessage.pPlayer->GetQuestLogComponent().QuestContent;
        replacementQuestContent = message.QuestContent;
    }

    const auto cEntity = m_world.create();
    struct AssignmentRollback
    {
        World& ServerWorld;
        Player* pPlayer;
        entt::entity Entity;
        VRAppearanceRelayService* pAppearanceService{};
        QuestLog PreviousQuestContent{};
        std::uint32_t AppearancePlayerId{};
        std::uint32_t AppearanceSequence{};
        bool CharacterAssigned{};
        bool QuestChanged{};
        bool AppearanceSeeded{};
        bool Committed{};

        ~AssignmentRollback() noexcept
        {
            if (Committed)
                return;

            if (pPlayer && CharacterAssigned)
                TP_UNUSED(pPlayer->ClearCharacter(Entity));
            if (pPlayer && QuestChanged)
            {
                try
                {
                    pPlayer->GetQuestLogComponent().QuestContent = std::move(PreviousQuestContent);
                }
                catch (...)
                {
                    spdlog::critical("Failed to restore quest state after rejected character assignment");
                }
            }
            if (pAppearanceService && AppearanceSeeded)
                pAppearanceService->DiscardSeededAppearance(AppearancePlayerId, AppearanceSequence);
            if (ServerWorld.valid(Entity))
                ServerWorld.destroy(Entity);
        }
    } assignmentRollback{m_world, acMessage.pPlayer, cEntity};
    const auto isTemporary = gameId.ModId == std::numeric_limits<uint32_t>::max();
    const auto isCustom = isPlayer || isTemporary;

    // For player characters and temporary forms
    if (!isCustom)
    {
        m_world.emplace<FormIdComponent>(cEntity, gameId.BaseId, gameId.ModId);
    }
    else if (baseId != GameId{} && !isTemporary)
    {
        m_world.destroy(cEntity);
        spdlog::warn("Unexpected NpcId, player {:x} might be forging packets", acMessage.pPlayer->GetConnectionId());
        return;
    }

    auto* const pServer = GameServer::Get();

    m_world.emplace<OwnerComponent>(cEntity, acMessage.pPlayer);

    auto& cellIdComponent = m_world.emplace<CellIdComponent>(cEntity, message.CellId);
    if (message.WorldSpaceId != GameId{})
    {
        cellIdComponent.WorldSpaceId = message.WorldSpaceId;
        cellIdComponent.CenterCoords = GridCellCoords::CalculateGridCellCoords(message.Position);
    }

    auto& characterComponent = m_world.emplace<CharacterComponent>(cEntity);
    characterComponent.ChangeFlags = message.ChangeFlags;
    characterComponent.SaveBuffer = std::move(message.AppearanceBuffer);
    characterComponent.BaseId = FormIdComponent(message.FormId);
    if (message.HasFaceTints)
        characterComponent.FaceTints = message.FaceTints;
    characterComponent.FactionsContent = message.FactionsContent;
    characterComponent.SetDead(message.CurrentActorData.IsDead);
    characterComponent.SetPlayer(isPlayer);
    characterComponent.SetWeaponDrawn(message.CurrentActorData.IsWeaponDrawn);
    characterComponent.SetDragon(message.IsDragon);
    characterComponent.SetMount(message.IsMount);
    characterComponent.SetPlayerSummon(message.IsPlayerSummon);
    if (message.HasVRAppearance)
    {
        characterComponent.InitialVRAppearance = message.InitialVRAppearance;
        characterComponent.HasVRAppearance = true;
    }

    auto& inventoryComponent = m_world.emplace<InventoryComponent>(cEntity);
    inventoryComponent.Content = message.CurrentActorData.InitialInventory;

    auto& actorValuesComponent = m_world.emplace<ActorValuesComponent>(cEntity);
    actorValuesComponent.CurrentActorValues = message.CurrentActorData.InitialActorValues;

    spdlog::debug("FormId: {:x}:{:x} - NpcId: {:x}:{:x} assigned to {:x}", gameId.ModId, gameId.BaseId, baseId.ModId, baseId.BaseId, acMessage.pPlayer->GetConnectionId());

    auto& movementComponent = m_world.emplace<MovementComponent>(cEntity);
    movementComponent.Tick = pServer->GetTick();
    movementComponent.Position = message.Position;
    movementComponent.Rotation = {message.Rotation.x, 0.f, message.Rotation.y};
    movementComponent.Sent = false;

    auto& animationComponent = m_world.emplace<AnimationComponent>(cEntity);
    if (HasAssignmentAction(message.LatestAction))
    {
        if (!IsValidAssignmentAction(message.LatestAction))
        {
            m_world.destroy(cEntity);
            spdlog::warn("Player {:x} sent an invalid initial animation action", acMessage.pPlayer->GetConnectionId());
            return;
        }
        animationComponent.CurrentAction = message.LatestAction;
        animationComponent.ActionsReplayCache.Append(animationComponent.CurrentAction);
    }

    if (isPlayer && message.HasVRAppearance)
    {
        auto& appearanceService = m_world.ctx().at<VRAppearanceRelayService>();
        if (!appearanceService.SeedAppearance(acMessage.pPlayer->GetId(), message.InitialVRAppearance))
        {
            spdlog::warn("Player {:x} initial appearance could not be retained",
                         acMessage.pPlayer->GetConnectionId());
            return;
        }
        assignmentRollback.pAppearanceService = &appearanceService;
        assignmentRollback.AppearancePlayerId = acMessage.pPlayer->GetId();
        assignmentRollback.AppearanceSequence = message.InitialVRAppearance.Sequence;
        assignmentRollback.AppearanceSeeded = true;
    }

    // Establish player-owned state before publishing the assignment response.
    // The rollback restores it if response serialization or queueing fails.
    if (isPlayer)
    {
        const auto pPlayer = acMessage.pPlayer;

        pPlayer->SetCharacter(cEntity);
        assignmentRollback.CharacterAssigned = true;
        if (message.HasQuestContent)
        {
            pPlayer->GetQuestLogComponent().QuestContent = std::move(replacementQuestContent);
            assignmentRollback.PreviousQuestContent = std::move(previousQuestContent);
            assignmentRollback.QuestChanged = true;
        }
        characterComponent.PlayerId = pPlayer->GetId();
    }

    AssignCharacterResponse response{};
    response.Cookie = message.Cookie;
    response.ServerId = World::ToInteger(cEntity);
    response.PlayerId = characterComponent.PlayerId;
    response.Owner = true;

    pServer->Send(acMessage.pPlayer->GetConnectionId(), response);

    // The owner now has a usable server ID. Event callbacks may publish it to
    // other systems and clients, so the assignment is committed first.
    assignmentRollback.Committed = true;

    auto& dispatcher = m_world.GetDispatcher();
    if (isPlayer)
        dispatcher.trigger(PlayerEnterWorldEvent(acMessage.pPlayer));
    dispatcher.trigger(CharacterSpawnedEvent(cEntity));
}
catch (...)
{
    LogCharacterServiceFailure("character creation");
}

bool CharacterService::TransferOwnership(Player* apPlayer, const uint32_t acServerId,
                                         const ActorData& acActorData) const noexcept try
{
    if (!apPlayer || !IsValidActorData(
                         acActorData,
                         SkyrimTogether::Protocol::IsVrClient(apPlayer->GetGameplayCapabilities())))
        return false;
    auto view = m_world.view<OwnerComponent>();
    const auto it = view.find(static_cast<entt::entity>(acServerId));
    if (it == view.end())
    {
        spdlog::warn("Client {:X} requested ownership of an entity that doesn't exist ({:X})!", apPlayer->GetConnectionId(), acServerId);
        return false;
    }

    const auto* character = m_world.try_get<CharacterComponent>(*it);
    if (character && !character->IsPlayer() &&
        !SkyrimTogether::Protocol::CanOwnNpc(apPlayer->GetGameplayCapabilities()))
    {
        spdlog::warn("VR client {:X} attempted NPC ownership claim without negotiated capability",
                     apPlayer->GetConnectionId());
        return false;
    }

    auto& characterOwnerComponent = view.get<OwnerComponent>(*it);
    auto* const pPreviousOwner = characterOwnerComponent.GetOwner();
    auto* const pActorValuesComponent = m_world.try_get<ActorValuesComponent>(*it);
    auto* const pInventoryComponent = m_world.try_get<InventoryComponent>(*it);
    auto* const pCharacterComponent = m_world.try_get<CharacterComponent>(*it);
    PendingActorData pendingActorData{};
    PrepareActorData(acActorData, pActorValuesComponent != nullptr, pInventoryComponent != nullptr,
                     pendingActorData);

    if (pPreviousOwner && pPreviousOwner != apPlayer)
    {
        NotifyRelinquishControl notify;
        notify.ServerId = acServerId;
        pPreviousOwner->Send(notify);
    }

    CommitActorData(pActorValuesComponent, pInventoryComponent, pCharacterComponent, acActorData,
                    pendingActorData);

    characterOwnerComponent.SetOwner(apPlayer);
    characterOwnerComponent.InvalidOwners.clear();
    if (auto* movementComponent = m_world.try_get<MovementComponent>(*it))
        movementComponent->HasTick = false;

    // Ownership is now committed. A later notification failure must not put
    // the entity back under an owner that has already relinquished control.
    if (!BroadcastActorData(apPlayer, *it, acActorData))
        LogCharacterServiceFailure("ownership actor-data fanout");

    if (character && !character->IsPlayer() &&
        SkyrimTogether::Protocol::HasCapability(
            apPlayer->GetGameplayCapabilities(),
            SkyrimTogether::Protocol::GameplayCapability::NpcOwnership))
    {
        NotifyOwnershipTransfer completion{};
        completion.ServerId = acServerId;
        // A zero token is the explicit completion acknowledgment, never a grant.
        completion.GrantToken = 0;
        try
        {
            apPlayer->Send(completion);
        }
        catch (...)
        {
            LogCharacterServiceFailure("ownership completion notification");
        }
    }

    spdlog::debug("\tOwnership claimed {:X}", acServerId);
    return true;
}
catch (...)
{
    LogCharacterServiceFailure("ownership transfer");
    return false;
}

void CharacterService::ExpireOwnershipGrants() const noexcept try
{
    const auto now = std::chrono::steady_clock::now();
    for (auto it = m_pendingOwnershipGrants.begin(); it != m_pendingOwnershipGrants.end();) {
        if (it->second.ExpiresAt <= now)
            it = m_pendingOwnershipGrants.erase(it);
        else
            ++it;
    }
}
catch (...)
{
    LogCharacterServiceFailure("ownership-grant expiry");
}

bool CharacterService::BuildActorData(const entt::entity acEntity, ActorData* apActorData) const noexcept try
{
    if (!apActorData)
        return false;

    ActorData actorData{};

    const auto* pActorValuesComponent = m_world.try_get<ActorValuesComponent>(acEntity);
    if (pActorValuesComponent)
    {
        actorData.InitialActorValues = pActorValuesComponent->CurrentActorValues;
    }

    const auto* pInventoryComponent = m_world.try_get<InventoryComponent>(acEntity);
    if (pInventoryComponent)
    {
        actorData.InitialInventory = pInventoryComponent->Content;
    }

    actorData.IsDead = false;
    const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(acEntity);
    if (pCharacterComponent)
    {
        actorData.IsDead = pCharacterComponent->IsDead();
        actorData.IsWeaponDrawn = pCharacterComponent->IsWeaponDrawn();
    }

    using std::swap;
    swap(*apActorData, actorData);
    return true;
}
catch (...)
{
    LogCharacterServiceFailure("actor-data snapshot");
    return false;
}

bool CharacterService::ApplyActorData(const entt::entity acEntity, const ActorData& acActorData) const noexcept try
{
    auto* pActorValuesComponent = m_world.try_get<ActorValuesComponent>(acEntity);
    auto* pInventoryComponent = m_world.try_get<InventoryComponent>(acEntity);
    auto* pCharacterComponent = m_world.try_get<CharacterComponent>(acEntity);

    // Construct every variable-sized replacement before changing world state.
    PendingActorData pendingActorData{};
    PrepareActorData(acActorData, pActorValuesComponent != nullptr, pInventoryComponent != nullptr,
                     pendingActorData);
    CommitActorData(pActorValuesComponent, pInventoryComponent, pCharacterComponent, acActorData,
                    pendingActorData);
    return true;
}
catch (...)
{
    LogCharacterServiceFailure("actor-data application");
    return false;
}

bool CharacterService::BroadcastActorData(Player* apPlayer, const entt::entity acEntity,
                                          const ActorData& acActorData) const noexcept try
{
    NotifySpawnData notifySpawnData;
    notifySpawnData.Id = World::ToInteger(acEntity);
    notifySpawnData.NewActorData = acActorData;

    return GameServer::Get()->SendToPlayersInRange(notifySpawnData, acEntity, apPlayer);
}
catch (...)
{
    LogCharacterServiceFailure("actor-data fanout");
    return false;
}

void CharacterService::ProcessFactionsChanges() const noexcept try
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 2000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    const auto characterView = m_world.view<CellIdComponent, CharacterComponent, OwnerComponent>();

    TiltedPhoques::Map<Player*, NotifyFactionsChanges> messages;

    for (auto entity : characterView)
    {
        auto& characterComponent = characterView.get<CharacterComponent>(entity);
        auto& cellIdComponent = characterView.get<CellIdComponent>(entity);
        auto& ownerComponent = characterView.get<OwnerComponent>(entity);

        // If we have nothing new to send skip this
        if (!characterComponent.IsDirtyFactions())
            continue;

        for (auto pPlayer : m_world.GetPlayerManager())
        {
            if (pPlayer == ownerComponent.GetOwner())
                continue;

            if (!cellIdComponent.IsInRange(pPlayer->GetCellComponent(), characterComponent.IsDragon()))
                continue;

            auto& message = messages[pPlayer];
            auto& change = message.Changes[World::ToInteger(entity)];

            change = characterComponent.FactionsContent;
        }

    }

    for (auto& [pPlayer, message] : messages)
    {
        if (!message.Changes.empty())
            pPlayer->Send(message);
    }

    for (auto entity : characterView)
    {
        auto& characterComponent = characterView.get<CharacterComponent>(entity);
        if (characterComponent.IsDirtyFactions())
            characterComponent.SetDirtyFactions(false);
    }

    lastSendTimePoint = now;
}
catch (...)
{
    // Dirty flags remain set so the next snapshot can retry the entire fanout.
    LogCharacterServiceFailure("faction snapshot fanout");
}

void CharacterService::ProcessMovementChanges() const noexcept try
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 1000ms / 50;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    const auto characterView = m_world.view<CharacterComponent, CellIdComponent, MovementComponent, AnimationComponent, OwnerComponent>();

    TiltedPhoques::Map<Player*, ServerReferencesMoveRequest> messages;

    for (auto pPlayer : m_world.GetPlayerManager())
    {
        auto& message = messages[pPlayer];

        message.Tick = GameServer::Get()->GetTick();
    }

    for (auto entity : characterView)
    {
        auto& characterComponent = characterView.get<CharacterComponent>(entity);
        auto& movementComponent = characterView.get<MovementComponent>(entity);
        auto& cellIdComponent = characterView.get<CellIdComponent>(entity);
        auto& ownerComponent = characterView.get<OwnerComponent>(entity);
        auto& animationComponent = characterView.get<AnimationComponent>(entity);

        // If we have nothing new to send skip this
        if (movementComponent.Sent == true && animationComponent.Actions.empty())
            continue;

        for (auto pPlayer : m_world.GetPlayerManager())
        {
            if (pPlayer == ownerComponent.GetOwner())
                continue;

            if (!cellIdComponent.IsInRange(pPlayer->GetCellComponent(), characterComponent.IsDragon()))
                continue;

            auto& message = messages[pPlayer];
            auto& update = message.Updates[World::ToInteger(entity)];
            auto& movement = update.UpdatedMovement;

            movement.CellId = cellIdComponent.Cell;
            movement.WorldSpaceId = cellIdComponent.WorldSpaceId;
            movement.Position = movementComponent.Position;

            movement.Rotation.x = movementComponent.Rotation.x;
            movement.Rotation.y = movementComponent.Rotation.z;

            movement.Direction = movementComponent.Direction;
            movement.Variables = movementComponent.Variables;

            update.ActionEvents = animationComponent.Actions;
        }
    }

    for (auto& [pPlayer, message] : messages)
    {
        if (!message.Updates.empty())
            pPlayer->Send(message);
    }

    m_world.view<AnimationComponent>().each([](AnimationComponent& animationComponent)
    {
        // Remove actions only after every recipient accepted its snapshot.
        animationComponent.Actions.clear();
    });

    m_world.view<MovementComponent>().each([](MovementComponent& movementComponent) { movementComponent.Sent = true; });

    lastSendTimePoint = now;
}
catch (...)
{
    // Preserve actions and unsent movement for the next snapshot attempt.
    LogCharacterServiceFailure("movement snapshot fanout");
}
