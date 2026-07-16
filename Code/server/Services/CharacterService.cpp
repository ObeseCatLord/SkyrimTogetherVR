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
#include <vr_common/VRAnimationGraphProtocol.h>

#include <Setting.h>

#include <cmath>
namespace
{
Console::Setting bEnableXpSync{"Gameplay:bEnableXpSync", "Syncs combat XP within the party", true};
constexpr auto kOwnershipGrantLifetime = std::chrono::seconds(5);

[[nodiscard]] bool MatchesOwnershipGrantSession(const Player* apPlayer, const ConnectionId_t aConnectionId,
                                                const std::uint64_t aConnectionGeneration,
                                                const std::uint64_t aSessionNonce) noexcept
{
    return apPlayer && apPlayer->GetConnectionId() == aConnectionId &&
           apPlayer->GetConnectionGeneration() == aConnectionGeneration &&
           apPlayer->GetClientSessionNonce() == aSessionNonce;
}
}

CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
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

void CharacterService::Serialize(World& aRegistry, entt::entity aEntity, CharacterSpawnRequest* apSpawnRequest) noexcept
{
    const auto& characterComponent = aRegistry.get<CharacterComponent>(aEntity);

    apSpawnRequest->ServerId = World::ToInteger(aEntity);
    apSpawnRequest->AppearanceBuffer = characterComponent.SaveBuffer;
    apSpawnRequest->ChangeFlags = characterComponent.ChangeFlags;
    apSpawnRequest->FaceTints = characterComponent.FaceTints;
    apSpawnRequest->FactionsContent = characterComponent.FactionsContent;
    apSpawnRequest->IsDead = characterComponent.IsDead();
    apSpawnRequest->IsPlayer = characterComponent.IsPlayer();
    apSpawnRequest->IsWeaponDrawn = characterComponent.IsWeaponDrawn();
    apSpawnRequest->IsPlayerSummon = characterComponent.IsPlayerSummon();
    apSpawnRequest->PlayerId = characterComponent.PlayerId;

    const auto* pFormIdComponent = aRegistry.try_get<FormIdComponent>(aEntity);
    if (pFormIdComponent)
    {
        apSpawnRequest->FormId = pFormIdComponent->Id;
    }

    const auto* pInventoryComponent = aRegistry.try_get<InventoryComponent>(aEntity);
    if (pInventoryComponent)
    {
        apSpawnRequest->InventoryContent = pInventoryComponent->Content;
    }

    const auto* pActorValuesComponent = aRegistry.try_get<ActorValuesComponent>(aEntity);
    if (pActorValuesComponent)
    {
        apSpawnRequest->InitialActorValues = pActorValuesComponent->CurrentActorValues;
    }

    if (characterComponent.BaseId)
    {
        apSpawnRequest->BaseId = characterComponent.BaseId.Id;
    }

    const auto* pMovementComponent = aRegistry.try_get<MovementComponent>(aEntity);
    if (pMovementComponent)
    {
        apSpawnRequest->Position = pMovementComponent->Position;
        apSpawnRequest->Rotation.x = pMovementComponent->Rotation.x;
        apSpawnRequest->Rotation.y = pMovementComponent->Rotation.z;
    }

    const auto* pCellIdComponent = aRegistry.try_get<CellIdComponent>(aEntity);
    if (pCellIdComponent)
    {
        apSpawnRequest->CellId = pCellIdComponent->Cell;
    }

    auto& animationComponent = aRegistry.get<AnimationComponent>(aEntity);
    apSpawnRequest->ActionsToReplay = animationComponent.ActionsReplayCache.FormRefinedReplayChain();
}

void CharacterService::OnUpdate(const UpdateEvent&) const noexcept
{
    ExpireOwnershipGrants();
    ProcessFactionsChanges();
    ProcessMovementChanges();
}

void CharacterService::OnCharacterExteriorCellChange(const CharacterExteriorCellChangeEvent& acEvent) const noexcept
{
    CharacterSpawnRequest spawnMessage;
    Serialize(m_world, acEvent.Entity, &spawnMessage);

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

void CharacterService::OnCharacterInteriorCellChange(const CharacterInteriorCellChangeEvent& acEvent) const noexcept
{
    CharacterSpawnRequest spawnMessage;
    Serialize(m_world, acEvent.Entity, &spawnMessage);

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

void CharacterService::OnAssignCharacterRequest(const PacketEvent<AssignCharacterRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;
    const auto& refId = message.ReferenceId;

    const auto isPlayer = (refId.ModId == 0 && refId.BaseId == 0x14);
    const auto isCustom = isPlayer || refId.ModId == std::numeric_limits<uint32_t>::max();
    if (!isPlayer && !SkyrimTogether::Protocol::CanOwnNpc(acMessage.pPlayer->GetGameplayCapabilities()))
    {
        spdlog::warn("VR client {:X} attempted NPC assignment without negotiated NPC ownership capability",
                     acMessage.pPlayer->GetConnectionId());
        return;
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
                    TransferOwnership(acMessage.pPlayer, World::ToInteger(*itor), acMessage.Packet.CurrentActorData);
                    isOwner = true;
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

void CharacterService::OnOwnershipTransferRequest(const PacketEvent<RequestOwnershipTransfer>& acMessage) const noexcept
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

    if (std::find(characterOwnerComponent.InvalidOwners.begin(), characterOwnerComponent.InvalidOwners.end(), acMessage.pPlayer) ==
        characterOwnerComponent.InvalidOwners.end())
        characterOwnerComponent.InvalidOwners.push_back(acMessage.pPlayer);
    m_pendingOwnershipGrants.erase(message.ServerId);

    m_world.GetDispatcher().trigger(OwnershipTransferEvent(cEntity));
}

void CharacterService::OnOwnershipTransferEvent(const OwnershipTransferEvent& acEvent) const noexcept
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
            ownerComponent.SetOwner(pPlayer);
            if (auto* movementComponent = m_world.try_get<MovementComponent>(acEvent.Entity))
                movementComponent->HasTick = false;
            pPlayer->Send(response);
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

void CharacterService::OnCharacterRemoveEvent(const CharacterRemoveEvent& acEvent) const noexcept
{
    m_pendingOwnershipGrants.erase(acEvent.ServerId);
    const auto entity = static_cast<entt::entity>(acEvent.ServerId);
    if (!m_world.valid(entity) || !m_world.all_of<OwnerComponent>(entity))
        return;
    const auto& characterOwnerComponent = m_world.get<OwnerComponent>(entity);

    GameServer::Get()->GetWorld().GetScriptService().HandleCharacterDestoy(entity);

    NotifyRemoveCharacter response;
    response.ServerId = acEvent.ServerId;

    for (auto pPlayer : m_world.GetPlayerManager())
    {
        if (characterOwnerComponent.GetOwner() == pPlayer)
            continue;

        pPlayer->Send(response);
    }

    m_world.destroy(entity);
    spdlog::debug("Character destroyed {:X}", acEvent.ServerId);
}

void CharacterService::OnOwnershipClaimRequest(const PacketEvent<RequestOwnershipClaim>& acMessage) const noexcept
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
        TransferOwnership(acMessage.pPlayer, message.ServerId, message.NewActorData);
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

    TransferOwnership(acMessage.pPlayer, message.ServerId, message.NewActorData);
}

void CharacterService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) const noexcept
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

void CharacterService::OnCharacterSpawned(const CharacterSpawnedEvent& acEvent) const noexcept
{
    CharacterSpawnRequest message;
    Serialize(m_world, acEvent.Entity, &message);

    const auto& ownerComp = m_world.get<OwnerComponent>(acEvent.Entity);
    if (!GameServer::Get()->SendToPlayersInRange(message, acEvent.Entity, ownerComp.GetOwner()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

    GameServer::Get()->GetWorld().GetScriptService().HandleCharacterSpawn(acEvent.Entity);
}

void CharacterService::OnReferencesMoveRequest(const PacketEvent<ClientReferencesMoveRequest>& acMessage) const noexcept
{
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
        movementComponent.Tick = message.Tick;
        movementComponent.HasTick = true;

        auto& update = entry.second;
        auto& movement = update.UpdatedMovement;

        movementComponent.Position = movement.Position;
        movementComponent.Rotation = glm::vec3(movement.Rotation.x, 0.f, movement.Rotation.y);
        movementComponent.Variables = movement.Variables;
        movementComponent.Direction = movement.Direction;

        cellIdComponent.Cell = movement.CellId;
        cellIdComponent.WorldSpaceId = movement.WorldSpaceId;
        cellIdComponent.CenterCoords = GridCellCoords::CalculateGridCellCoords(movement.Position.x, movement.Position.y);

        for (auto& action : update.ActionEvents)
        {
            auto [canceled, reason] = GameServer::Get()->GetWorld().GetScriptService().HandleCharacterMove(entity);
            if (canceled)
                continue;

            animationComponent.CurrentAction = action;

            animationComponent.Actions.push_back(animationComponent.CurrentAction);
        }

        animationComponent.ActionsReplayCache.AppendAll(update.ActionEvents);

        movementComponent.Sent = false;
    }
}

void CharacterService::OnActorActionRequest(const PacketEvent<ClientActorActionRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    const auto& action = message.Action;
    if (!acMessage.pPlayer ||
        !SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(),
            SkyrimTogether::Protocol::GameplayCapability::ExactAnimationActions) ||
        message.ServerId == 0 || action.ActionId == 0 || (action.Type & ~0x7u) != 0 ||
        action.EventName.size() > 127 || action.TargetEventName.size() > 127 ||
        action.Variables.Booleans.size() != SkyrimTogetherVR::AnimationGraphProtocol::kBooleanCount ||
        action.Variables.Floats.size() != SkyrimTogetherVR::AnimationGraphProtocol::kFloatCount ||
        action.Variables.Integers.size() != SkyrimTogetherVR::AnimationGraphProtocol::kIntegerCount ||
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
    animationComponent.CurrentAction = message.Action;
    animationComponent.Actions.push_back(animationComponent.CurrentAction);
    animationComponent.ActionsReplayCache.AppendAll(Vector<ActionEvent>{animationComponent.CurrentAction});
}

void CharacterService::OnFactionsChanges(const PacketEvent<RequestFactionsChanges>& acMessage) const noexcept
{
    OwnerView<CharacterComponent> view(m_world, acMessage.GetSender());

    auto& message = acMessage.Packet;

    for (auto& [id, factions] : message.Changes)
    {
        auto it = view.find(static_cast<entt::entity>(id));

        if (it == std::end(view) || view.get<OwnerComponent>(*it).GetOwner() != acMessage.pPlayer)
            continue;

        auto& characterComponent = view.get<CharacterComponent>(*it);
        characterComponent.FactionsContent = factions;
        characterComponent.SetDirtyFactions(true);
    }
}

void CharacterService::OnMountRequest(const PacketEvent<MountRequest>& acMessage) const noexcept
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

void CharacterService::OnNewPackageRequest(const PacketEvent<NewPackageRequest>& acMessage) const noexcept
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

void CharacterService::OnRequestRespawn(const PacketEvent<RequestRespawn>& acMessage) const noexcept
{
    auto view = m_world.view<OwnerComponent, CharacterComponent>();
    auto it = view.find(static_cast<entt::entity>(acMessage.Packet.ActorId));
    if (it == view.end())
    {
        spdlog::warn("No OwnerComponent found for actor id {:X}", acMessage.Packet.ActorId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);

    // Replay cache needs to be cleared when a character respawns
    m_world.try_get<AnimationComponent>(*it)->ActionsReplayCache.Clear();

    if (ownerComponent.GetOwner() == acMessage.pPlayer)
    {
        if (!acMessage.Packet.AppearanceBuffer.empty())
        {
            auto& characterComponent = view.get<CharacterComponent>(*it);
            characterComponent.SaveBuffer = acMessage.Packet.AppearanceBuffer;
            characterComponent.ChangeFlags = acMessage.Packet.ChangeFlags;
        }

        NotifyRespawn notify;
        notify.ActorId = acMessage.Packet.ActorId;

        if (!GameServer::Get()->SendToPlayersInRange(notify, *it, acMessage.GetSender()))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    }
    else
    {
        CharacterSpawnRequest message;
        Serialize(m_world, *it, &message);

        acMessage.GetSender()->Send(message);
    }
}

void CharacterService::OnSyncExperienceRequest(const PacketEvent<SyncExperienceRequest>& acMessage) const noexcept
{
    if (!bEnableXpSync || !acMessage.pPlayer || !std::isfinite(acMessage.Packet.Experience) ||
        acMessage.Packet.Experience <= 0.0F || acMessage.Packet.Experience > 100000.0F)
        return;

    NotifySyncExperience notify;
    notify.Experience = acMessage.Packet.Experience;

    const auto& partyComponent = acMessage.pPlayer->GetParty();
    GameServer::Get()->SendToParty(notify, partyComponent, acMessage.GetSender());
}

void CharacterService::OnDialogueRequest(const PacketEvent<DialogueRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    NotifyDialogue notify{};
    notify.ServerId = message.ServerId;
    notify.SoundFilename = message.SoundFilename;

    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void CharacterService::OnSubtitleRequest(const PacketEvent<SubtitleRequest>& acMessage) const noexcept
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

void CharacterService::CreateCharacter(const PacketEvent<AssignCharacterRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    const auto gameId = message.ReferenceId;
    const auto baseId = message.FormId;

    const auto cEntity = m_world.create();
    const auto isTemporary = gameId.ModId == std::numeric_limits<uint32_t>::max();
    const auto isPlayer = (gameId.ModId == 0 && gameId.BaseId == 0x14);
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

    m_world.emplace<AnimationComponent>(cEntity);

    // If this is a player character store a ref and trigger an event
    if (isPlayer)
    {
        const auto pPlayer = acMessage.pPlayer;

        pPlayer->SetCharacter(cEntity);
        if (message.HasQuestContent)
            pPlayer->GetQuestLogComponent().QuestContent = message.QuestContent;
        characterComponent.PlayerId = pPlayer->GetId();

        auto& dispatcher = m_world.GetDispatcher();
        dispatcher.trigger(PlayerEnterWorldEvent(pPlayer));
    }

    AssignCharacterResponse response{};
    response.Cookie = message.Cookie;
    response.ServerId = World::ToInteger(cEntity);
    response.PlayerId = characterComponent.PlayerId;
    response.Owner = true;

    pServer->Send(acMessage.pPlayer->GetConnectionId(), response);

    auto& dispatcher = m_world.GetDispatcher();
    dispatcher.trigger(CharacterSpawnedEvent(cEntity));
}

void CharacterService::TransferOwnership(Player* apPlayer, const uint32_t acServerId,
                                         const ActorData& acActorData) const noexcept
{
    if (!apPlayer)
        return;
    auto view = m_world.view<OwnerComponent>();
    const auto it = view.find(static_cast<entt::entity>(acServerId));
    if (it == view.end())
    {
        spdlog::warn("Client {:X} requested ownership of an entity that doesn't exist ({:X})!", apPlayer->GetConnectionId(), acServerId);
        return;
    }

    const auto* character = m_world.try_get<CharacterComponent>(*it);
    if (character && !character->IsPlayer() &&
        !SkyrimTogether::Protocol::CanOwnNpc(apPlayer->GetGameplayCapabilities()))
    {
        spdlog::warn("VR client {:X} attempted NPC ownership claim without negotiated capability",
                     apPlayer->GetConnectionId());
        return;
    }

    auto& characterOwnerComponent = view.get<OwnerComponent>(*it);

    if (characterOwnerComponent.GetOwner() && characterOwnerComponent.GetOwner() != apPlayer)
    {
        NotifyRelinquishControl notify;
        notify.ServerId = acServerId;
        characterOwnerComponent.pOwner->Send(notify);
    }

    characterOwnerComponent.SetOwner(apPlayer);
    characterOwnerComponent.InvalidOwners.clear();
    if (auto* movementComponent = m_world.try_get<MovementComponent>(*it))
        movementComponent->HasTick = false;

    BroadcastActorData(apPlayer, *it, acActorData);

    if (character && !character->IsPlayer() &&
        SkyrimTogether::Protocol::HasCapability(
            apPlayer->GetGameplayCapabilities(),
            SkyrimTogether::Protocol::GameplayCapability::NpcOwnership))
    {
        NotifyOwnershipTransfer completion{};
        completion.ServerId = acServerId;
        // A zero token is the explicit completion acknowledgment, never a grant.
        completion.GrantToken = 0;
        apPlayer->Send(completion);
    }

    spdlog::debug("\tOwnership claimed {:X}", acServerId);
}

void CharacterService::ExpireOwnershipGrants() const noexcept
{
    const auto now = std::chrono::steady_clock::now();
    for (auto it = m_pendingOwnershipGrants.begin(); it != m_pendingOwnershipGrants.end();) {
        if (it->second.ExpiresAt <= now)
            it = m_pendingOwnershipGrants.erase(it);
        else
            ++it;
    }
}

ActorData CharacterService::BuildActorData(const entt::entity acEntity) const noexcept
{
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

    return actorData;
}

void CharacterService::ApplyActorData(const entt::entity acEntity, const ActorData& acActorData) const noexcept
{
    auto* pActorValuesComponent = m_world.try_get<ActorValuesComponent>(acEntity);
    if (pActorValuesComponent)
    {
        pActorValuesComponent->CurrentActorValues = acActorData.InitialActorValues;
    }

    auto* pInventoryComponent = m_world.try_get<InventoryComponent>(acEntity);
    if (pInventoryComponent)
    {
        pInventoryComponent->Content = acActorData.InitialInventory;
    }

    auto* pCharacterComponent = m_world.try_get<CharacterComponent>(acEntity);
    if (pCharacterComponent)
    {
        pCharacterComponent->SetDead(acActorData.IsDead);
        pCharacterComponent->SetWeaponDrawn(acActorData.IsWeaponDrawn);
    }
}

void CharacterService::BroadcastActorData(Player* apPlayer, const entt::entity acEntity,
                                          const ActorData& acActorData) const noexcept
{
    ApplyActorData(acEntity, acActorData);

    NotifySpawnData notifySpawnData;
    notifySpawnData.Id = World::ToInteger(acEntity);
    notifySpawnData.NewActorData = acActorData;

    GameServer::Get()->SendToPlayersInRange(notifySpawnData, acEntity, apPlayer);
}

void CharacterService::ProcessFactionsChanges() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 2000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

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

        characterComponent.SetDirtyFactions(false);
    }

    for (auto [pPlayer, message] : messages)
    {
        if (!message.Changes.empty())
            pPlayer->Send(message);
    }
}

void CharacterService::ProcessMovementChanges() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 1000ms / 50;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

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

    m_world.view<AnimationComponent>().each([](AnimationComponent& animationComponent)
    {
        // Remove actions we've sent
        animationComponent.Actions.clear();
    });

    m_world.view<MovementComponent>().each([](MovementComponent& movementComponent) { movementComponent.Sent = true; });

    for (auto& [pPlayer, message] : messages)
    {
        if (!message.Updates.empty())
            pPlayer->Send(message);
    }
}
