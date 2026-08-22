#include <Services/ObjectService.h>
#include <server/Services/ServerAuthorityPolicy.h>
#include <server/Services/RevisionedRecoveryPolicy.h>

#include <GameServer.h>
#include <World.h>
#include <Components.h>

#include <Events/PlayerLeaveCellEvent.h>

#include <Messages/ActivateRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/NotifyLockChange.h>
#include <Messages/AssignObjectsRequest.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Messages/NotifyScriptAnimation.h>
#include <Messages/NotifyObjectResync.h>
#include <Messages/RequestObjectResync.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <type_traits>

namespace
{
constexpr std::int32_t kMaximumObjectItemCount = 1'000'000;
constexpr std::int32_t kMaximumGridCoordinate = 1'000'000;
constexpr std::uint64_t kAuthorityRejectionLogInterval = 128;
constexpr std::size_t kMaximumObjectSnapshotRevisions = 4096;

enum class ObjectInteractionRejection : std::size_t
{
    MissingSender,
    MissingOwnedCharacter,
    UnknownTarget,
    SpoofedTarget,
    SpoofedCell,
    SpoofedActivator,
    OutOfInterestRange,
    Count,
};

constexpr std::array<const char*, static_cast<std::size_t>(ObjectInteractionRejection::Count)>
    kObjectInteractionRejectionNames{
        "missing sender",
        "missing authenticated player character",
        "unassigned target",
        "spoofed target",
        "spoofed cell",
        "spoofed activator",
        "target outside canonical interest range",
    };
std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(ObjectInteractionRejection::Count)>
    g_objectInteractionRejections{};

void LogObjectInteractionRejection(const ObjectInteractionRejection aReason) noexcept
{
    const auto index = static_cast<std::size_t>(aReason);
    const auto aggregate = g_objectInteractionRejections[index].fetch_add(1, std::memory_order_relaxed) + 1;
    if (aggregate != 1 && aggregate % kAuthorityRejectionLogInterval != 0)
        return;
    try
    {
        spdlog::warn("Object interaction rejected: {} (aggregate={})",
                     kObjectInteractionRejectionNames[index], aggregate);
    }
    catch (...)
    {
    }
}

[[nodiscard]] bool IsValidObjectInventory(const Inventory& acInventory) noexcept
{
    if (!acInventory.IsDecodedValid || acInventory.Entries.size() > AssignObjectsRequest::kMaximumTotalInventoryEntries ||
        acInventory.CurrentMagicEquipment.LeftHandSpell || acInventory.CurrentMagicEquipment.RightHandSpell ||
        acInventory.CurrentMagicEquipment.Shout)
        return false;

    std::size_t effectCount{};
    for (const auto& entry : acInventory.Entries)
    {
        if (!entry.IsValidMutation() || entry.Count <= 0 || entry.Count > kMaximumObjectItemCount ||
            entry.ExtraWorn || entry.ExtraWornLeft ||
            entry.EquipmentFlags != 0 ||
            entry.EnchantData.Effects.size() > AssignObjectsRequest::kMaximumTotalInventoryEffects - effectCount)
            return false;
        effectCount += entry.EnchantData.Effects.size();
    }
    return true;
}

[[nodiscard]] bool IsValidObjectAssignment(
    const ObjectData& acObject,
    const CellIdComponent& acPlayerCell) noexcept
{
    return acObject.IsDecodedValid && acObject.ServerId == 0 && !acObject.IsSenderFirst && acObject.Id &&
           acObject.CellId && acObject.CellId == acPlayerCell.Cell &&
           acObject.WorldSpaceId == acPlayerCell.WorldSpaceId &&
           std::abs(static_cast<std::int64_t>(acObject.CurrentCoords.X)) <= kMaximumGridCoordinate &&
           std::abs(static_cast<std::int64_t>(acObject.CurrentCoords.Y)) <= kMaximumGridCoordinate &&
           IsValidObjectInventory(acObject.CurrentInventory);
}

struct CreatedObjectRollback
{
    explicit CreatedObjectRollback(World& aWorld) noexcept : WorldRef(aWorld) {}
    ~CreatedObjectRollback() noexcept
    {
        if (Committed)
            return;
        for (std::size_t index = Count; index > 0; --index)
        {
            try
            {
                WorldRef.destroy(Entities[index - 1]);
            }
            catch (...)
            {
            }
        }
    }

    void Record(const entt::entity aEntity) noexcept { Entities[Count++] = aEntity; }
    void Commit() noexcept { Committed = true; }

    World& WorldRef;
    std::array<entt::entity, AssignObjectsRequest::kMaximumWireObjects> Entities{};
    std::size_t Count{};
    bool Committed{};
};

template <class TMessage>
[[nodiscard]] bool ResolveAuthorizedObjectInteraction(
    World& arWorld, const PacketEvent<TMessage>& acMessage, const bool aCheckActivator,
    entt::entity& arTarget, std::uint32_t& arActivatorId) noexcept
{
    if (!acMessage.pPlayer)
    {
        LogObjectInteractionRejection(ObjectInteractionRejection::MissingSender);
        return false;
    }

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !arWorld.valid(*character) ||
        !arWorld.all_of<OwnerComponent, CharacterComponent, CellIdComponent>(*character) ||
        arWorld.get<OwnerComponent>(*character).GetOwner() != acMessage.pPlayer ||
        !arWorld.get<CharacterComponent>(*character).IsPlayer())
    {
        LogObjectInteractionRejection(ObjectInteractionRejection::MissingOwnedCharacter);
        return false;
    }

    auto objectView = arWorld.view<FormIdComponent, ObjectComponent, CellIdComponent>();
    const auto object = std::find_if(
        objectView.begin(), objectView.end(), [&objectView, id = acMessage.Packet.Id](const entt::entity aEntity) {
            return objectView.get<FormIdComponent>(aEntity).Id == id;
        });
    if (object == objectView.end())
    {
        LogObjectInteractionRejection(ObjectInteractionRejection::UnknownTarget);
        return false;
    }

    arTarget = *object;
    arActivatorId = World::ToInteger(*character);
    if (arActivatorId == 0)
    {
        LogObjectInteractionRejection(ObjectInteractionRejection::MissingOwnedCharacter);
        return false;
    }
    const auto& senderCell = acMessage.pPlayer->GetCellComponent();
    const auto& characterCell = arWorld.get<CellIdComponent>(*character);
    const auto& targetCell = objectView.get<CellIdComponent>(arTarget);
    const auto authority = SkyrimTogether::ServerAuthorityPolicy::ObjectInteractionAuthority{
        true,
        true,
        objectView.get<FormIdComponent>(arTarget).Id == acMessage.Packet.Id,
        targetCell.Cell == acMessage.Packet.CellId,
        [&]() noexcept {
            if constexpr (std::is_same_v<TMessage, ActivateRequest>)
                return !aCheckActivator || arActivatorId == acMessage.Packet.ActivatorId;
            return true;
        }(),
        senderCell.IsInRange(characterCell, false),
        senderCell.IsInRange(targetCell, false),
        characterCell.IsInRange(targetCell, false),
    };
    if (SkyrimTogether::ServerAuthorityPolicy::IsAuthorizedObjectInteraction(authority))
        return true;

    if (!authority.RequestedTargetIsCanonical)
        LogObjectInteractionRejection(ObjectInteractionRejection::SpoofedTarget);
    else if (!authority.RequestedCellIsCanonical)
        LogObjectInteractionRejection(ObjectInteractionRejection::SpoofedCell);
    else if (!authority.RequestedActivatorIsCanonical)
        LogObjectInteractionRejection(ObjectInteractionRejection::SpoofedActivator);
    else
        LogObjectInteractionRejection(ObjectInteractionRejection::OutOfInterestRange);
    return false;
}
} // namespace

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_leaveCellConnection = aDispatcher.sink<PlayerLeaveCellEvent>().connect<&ObjectService::OnPlayerLeaveCellEvent>(this);
    m_assignObjectConnection = aDispatcher.sink<PacketEvent<AssignObjectsRequest>>().connect<&ObjectService::OnAssignObjectsRequest>(this);
    m_activateConnection = aDispatcher.sink<PacketEvent<ActivateRequest>>().connect<&ObjectService::OnActivate>(this);
    m_lockChangeConnection = aDispatcher.sink<PacketEvent<LockChangeRequest>>().connect<&ObjectService::OnLockChange>(this);
    m_scriptAnimationConnection = aDispatcher.sink<PacketEvent<ScriptAnimationRequest>>().connect<&ObjectService::OnScriptAnimationRequest>(this);
    m_objectResyncConnection = aDispatcher.sink<PacketEvent<RequestObjectResync>>().connect<&ObjectService::OnObjectResyncRequest>(this);
    m_objectDestroyConnection = m_world.on_destroy<ObjectComponent>().connect<&ObjectService::OnObjectDestroy>(this);
}

std::uint64_t ObjectService::NextObjectSnapshotRevision(
    const std::uint32_t aServerId, const std::uint64_t aKnownRevision) noexcept
{
    if (aServerId == 0)
        return 0;
    try
    {
        auto it = m_objectSnapshotRevisions.find(aServerId);
        if (it == m_objectSnapshotRevisions.end())
        {
            if (m_objectSnapshotRevisions.size() >= kMaximumObjectSnapshotRevisions)
                return 0;
            it = m_objectSnapshotRevisions.emplace(aServerId, 0).first;
        }
        if (!SkyrimTogether::RevisionedRecovery::CanIssueSnapshot(it->second, aKnownRevision))
            return 0;
        return ++it->second;
    }
    catch (...)
    {
        return 0;
    }
}

void ObjectService::OnObjectDestroy(entt::registry&, const entt::entity aEntity) noexcept
{
    m_objectSnapshotRevisions.erase(World::ToInteger(aEntity));
}

// TODO(cosideci): the cell handling of objects need to be revamped.
// We already store the location and worldspace of the mod through CellIdComponent.
// Clients need a message saying the entity was destroyed.
void ObjectService::OnPlayerLeaveCellEvent(const PlayerLeaveCellEvent& acEvent) noexcept try
{
    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer->GetCellComponent().Cell == acEvent.OldCell)
            return;
    }

    auto objectView = m_world.view<ObjectComponent, CellIdComponent>();
    Vector<entt::entity> toDestroy;

    for (auto entity : objectView)
    {
        const auto& cellIdComponent = objectView.get<CellIdComponent>(entity);

        if (cellIdComponent.Cell != acEvent.OldCell)
            continue;

        toDestroy.push_back(entity);
    }

    for (auto& entity : toDestroy)
    {
        m_world.destroy(entity);
    }
}
catch (...)
{
    spdlog::error("Object cleanup skipped after an allocation failure");
}

// NOTE: this whole system kinda relies on all objects in a cell being static.
// This is fine for containers and doors, but if this system is expanded, think of temporaries.
void ObjectService::OnAssignObjectsRequest(const PacketEvent<AssignObjectsRequest>& acMessage) noexcept try
{
    if (!acMessage.pPlayer || !acMessage.Packet.IsDecodedValid ||
        acMessage.Packet.Objects.size() > AssignObjectsRequest::kMaximumWireObjects)
        return;

    std::array<GameId, AssignObjectsRequest::kMaximumWireObjects> objectIds{};
    std::size_t objectIdCount{};
    const auto& playerCell = acMessage.pPlayer->GetCellComponent();
    for (const auto& object : acMessage.Packet.Objects)
    {
        if (!IsValidObjectAssignment(object, playerCell) ||
            std::find(objectIds.begin(), objectIds.begin() + objectIdCount, object.Id) !=
                objectIds.begin() + objectIdCount)
            return;
        objectIds[objectIdCount++] = object.Id;
    }

    auto view = m_world.view<FormIdComponent, ObjectComponent, InventoryComponent>();

    AssignObjectsResponse response;
    response.Objects.reserve(acMessage.Packet.Objects.size());
    CreatedObjectRollback rollback{m_world};

    for (const ObjectData& object : acMessage.Packet.Objects)
    {
        const auto iter = std::find_if(
            std::begin(view), std::end(view),
            [view, id = object.Id](auto entity)
            {
                const auto& formIdComponent = view.get<FormIdComponent>(entity);
                return formIdComponent.Id == id;
            });

        if (iter != std::end(view))
        {
            ObjectData objectData;
            objectData.ServerId = World::ToInteger(*iter);

            auto& formIdComponent = view.get<FormIdComponent>(*iter);
            objectData.Id = formIdComponent.Id;

            auto& objectComponent = view.get<ObjectComponent>(*iter);
            // The first owner established canonical state; a joining sender
            // receives it and cannot replace it with a local observation.
            objectData.CurrentLockData = objectComponent.CurrentLockData;
            objectData.HasCurrentOpenState = objectComponent.HasCurrentOpenState;
            objectData.CurrentOpenState = objectComponent.CurrentOpenState;

            auto& inventoryComponent = view.get<InventoryComponent>(*iter);
            objectData.CurrentInventory = inventoryComponent.Content;

            objectData.IsSenderFirst = false;

            response.Objects.push_back(objectData);
        }
        else
        {
            const auto cEntity = m_world.create();
            rollback.Record(cEntity);

            m_world.emplace<FormIdComponent>(cEntity, object.Id);

            auto& objectComponent = m_world.emplace<ObjectComponent>(cEntity, acMessage.pPlayer);
            objectComponent.CurrentLockData = object.CurrentLockData;
            objectComponent.HasCurrentOpenState = object.HasCurrentOpenState;
            objectComponent.CurrentOpenState = object.CurrentOpenState;

            m_world.emplace<CellIdComponent>(cEntity, object.CellId, object.WorldSpaceId, object.CurrentCoords);
            auto& inventoryComp = m_world.emplace<InventoryComponent>(cEntity);
            inventoryComp.Content = object.CurrentInventory;

            ObjectData objectData;
            objectData.Id = object.Id;
            objectData.ServerId = World::ToInteger(cEntity);
            objectData.IsSenderFirst = true;

            response.Objects.push_back(objectData);
        }
    }

    if (!response.Objects.empty())
        acMessage.pPlayer->Send(response);
    rollback.Commit();
}
catch (...)
{
    spdlog::error("Object assignment rejected after an allocation, world mutation, or send failure");
}

void ObjectService::OnActivate(const PacketEvent<ActivateRequest>& acMessage) const noexcept try
{
    entt::entity target{};
    std::uint32_t activatorId{};
    if (!acMessage.Packet.IsDecodedValid || !acMessage.Packet.IsValid() ||
        !ResolveAuthorizedObjectInteraction(m_world, acMessage, true, target, activatorId))
        return;

    NotifyActivate notifyActivate;
    notifyActivate.Id = acMessage.Packet.Id;
    notifyActivate.ActivatorId = activatorId;
    notifyActivate.PreActivationOpenState = acMessage.Packet.PreActivationOpenState;
    notifyActivate.HasPostActivationOpenState = acMessage.Packet.HasPostActivationOpenState;
    notifyActivate.PostActivationOpenState = acMessage.Packet.PostActivationOpenState;

    auto& objectComponent = m_world.get<ObjectComponent>(target);
    if (acMessage.Packet.HasPostActivationOpenState) {
        objectComponent.HasCurrentOpenState = true;
        objectComponent.CurrentOpenState = acMessage.Packet.PostActivationOpenState;
    }

    if (!GameServer::Get()->SendToPlayersInRange(notifyActivate, target, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
catch (...)
{
    spdlog::error("Object activation relay failed");
}

void ObjectService::OnObjectResyncRequest(const PacketEvent<RequestObjectResync>& acMessage) noexcept try
{
    const auto& request = acMessage.Packet;
    if (!acMessage.pPlayer || !request.IsDecodedValid || !request.IsValid() ||
        !SkyrimTogether::Protocol::IsVrGameplayClient(acMessage.pPlayer->GetGameplayCapabilities()) ||
        !SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(),
            SkyrimTogether::Protocol::GameplayCapability::RevisionedCanonicalRecovery))
        return;

    const auto entity = static_cast<entt::entity>(request.ServerId);
    if (!m_world.valid(entity) ||
        !m_world.all_of<FormIdComponent, ObjectComponent, InventoryComponent, CellIdComponent>(entity))
        return;
    const auto& cell = m_world.get<CellIdComponent>(entity);
    if (!acMessage.pPlayer->GetCellComponent().IsInRange(cell, false))
        return;
    const auto revision = NextObjectSnapshotRevision(request.ServerId, request.KnownRevision);
    if (revision == 0)
        return;

    const auto& form = m_world.get<FormIdComponent>(entity);
    const auto& object = m_world.get<ObjectComponent>(entity);
    const auto& inventory = m_world.get<InventoryComponent>(entity);
    NotifyObjectResync response{};
    response.ServerId = request.ServerId;
    response.RequestId = request.RequestId;
    response.CanonicalRevision = revision;
    response.Snapshot.ServerId = request.ServerId;
    response.Snapshot.Id = form.Id;
    response.Snapshot.CellId = cell.Cell;
    response.Snapshot.WorldSpaceId = cell.WorldSpaceId;
    response.Snapshot.CurrentCoords = cell.CenterCoords;
    response.Snapshot.CurrentLockData = object.CurrentLockData;
    response.Snapshot.CurrentInventory = inventory.Content;
    response.Snapshot.HasCurrentOpenState = object.HasCurrentOpenState;
    response.Snapshot.CurrentOpenState = object.CurrentOpenState;
    acMessage.pPlayer->Send(response);
}
catch (...)
{
    spdlog::error("Object resynchronization rejected after an allocation or serialization failure");
}

void ObjectService::OnLockChange(const PacketEvent<LockChangeRequest>& acMessage) const noexcept try
{
    entt::entity target{};
    std::uint32_t ignoredActivatorId{};
    if (!ResolveAuthorizedObjectInteraction(m_world, acMessage, false, target, ignoredActivatorId))
        return;

    NotifyLockChange notifyLockChange;
    notifyLockChange.Id = acMessage.Packet.Id;
    notifyLockChange.IsLocked = acMessage.Packet.IsLocked;
    notifyLockChange.LockLevel = acMessage.Packet.LockLevel;

    auto& objectComponent = m_world.get<ObjectComponent>(target);
    objectComponent.CurrentLockData.IsLocked = acMessage.Packet.IsLocked;
    objectComponent.CurrentLockData.LockLevel = acMessage.Packet.LockLevel;

    if (!GameServer::Get()->SendToPlayersInRange(notifyLockChange, target, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
catch (...)
{
    spdlog::error("Object lock relay failed");
}

void ObjectService::OnScriptAnimationRequest(const PacketEvent<ScriptAnimationRequest>& acMessage) noexcept try
{
    auto& packet = acMessage.Packet;

    NotifyScriptAnimation message{};
    message.FormID = packet.FormID;
    message.Animation = packet.Animation;
    message.EventName = packet.EventName;

    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        pPlayer->Send(message);
    }
}
catch (...)
{
    spdlog::error("Object script-animation relay failed");
}
