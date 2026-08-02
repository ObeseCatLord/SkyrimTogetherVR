#include <Services/ObjectService.h>

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

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr std::int32_t kMaximumObjectItemCount = 1'000'000;
constexpr std::int32_t kMaximumGridCoordinate = 1'000'000;

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
} // namespace

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_leaveCellConnection = aDispatcher.sink<PlayerLeaveCellEvent>().connect<&ObjectService::OnPlayerLeaveCellEvent>(this);
    m_assignObjectConnection = aDispatcher.sink<PacketEvent<AssignObjectsRequest>>().connect<&ObjectService::OnAssignObjectsRequest>(this);
    m_activateConnection = aDispatcher.sink<PacketEvent<ActivateRequest>>().connect<&ObjectService::OnActivate>(this);
    m_lockChangeConnection = aDispatcher.sink<PacketEvent<LockChangeRequest>>().connect<&ObjectService::OnLockChange>(this);
    m_scriptAnimationConnection = aDispatcher.sink<PacketEvent<ScriptAnimationRequest>>().connect<&ObjectService::OnScriptAnimationRequest>(this);
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
            objectData.CurrentLockData = objectComponent.CurrentLockData;

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
    NotifyActivate notifyActivate;
    notifyActivate.Id = acMessage.Packet.Id;
    notifyActivate.ActivatorId = acMessage.Packet.ActivatorId;
    notifyActivate.PreActivationOpenState = acMessage.Packet.PreActivationOpenState;

    for (auto pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer != acMessage.pPlayer && pPlayer->GetCellComponent().Cell == acMessage.Packet.CellId)
        {
            pPlayer->Send(notifyActivate);
        }
    }
}
catch (...)
{
    spdlog::error("Object activation relay failed");
}

void ObjectService::OnLockChange(const PacketEvent<LockChangeRequest>& acMessage) const noexcept try
{
    NotifyLockChange notifyLockChange;
    notifyLockChange.Id = acMessage.Packet.Id;
    notifyLockChange.IsLocked = acMessage.Packet.IsLocked;
    notifyLockChange.LockLevel = acMessage.Packet.LockLevel;

    auto objectView = m_world.view<FormIdComponent, ObjectComponent>();

    const auto iter = std::find_if(
        std::begin(objectView), std::end(objectView),
        [objectView, id = acMessage.Packet.Id](auto entity)
        {
            const auto& formIdComponent = objectView.get<FormIdComponent>(entity);
            return formIdComponent.Id == id;
        });

    if (iter != std::end(objectView))
    {
        auto& objectComponent = objectView.get<ObjectComponent>(*iter);
        objectComponent.CurrentLockData.IsLocked = acMessage.Packet.IsLocked;
        objectComponent.CurrentLockData.LockLevel = acMessage.Packet.LockLevel;
    }

    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer == acMessage.pPlayer)
            continue;

        if (pPlayer->GetCellComponent().Cell == acMessage.Packet.CellId)
            pPlayer->Send(notifyLockChange);
    }
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
