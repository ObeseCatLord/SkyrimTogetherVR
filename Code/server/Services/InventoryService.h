#pragma once

#include <Events/PacketEvent.h>

#include <cstdint>
#include <unordered_map>

struct World;
struct UpdateEvent;
struct RequestObjectInventoryChanges;
struct RequestInventoryChanges;
struct RequestEquipmentChanges;
struct DrawWeaponRequest;
struct PlayerLeaveEvent;
struct CharacterRemoveEvent;

/**
 * @brief Relays inventory/equipment changes and updates the server side state.
 */
class InventoryService
{
public:
    InventoryService(World& aWorld, entt::dispatcher& aDispatcher);

    /**
     * @brief Relays inventory changes to other clients and updates server side inventories.
     */
    void OnInventoryChanges(const PacketEvent<RequestInventoryChanges>& acMessage) noexcept;
    /**
     * @brief Relays equipment changes to other clients and updates server side equipment.
     */
    void OnEquipmentChanges(const PacketEvent<RequestEquipmentChanges>& acMessage) noexcept;
    /**
     * @brief Relays weapon draw changes to other clients and updates server side weapon draw state.
     */
    void OnWeaponDrawnRequest(const PacketEvent<DrawWeaponRequest>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    void OnCharacterRemove(const CharacterRemoveEvent& acEvent) noexcept;

private:
    struct EquipmentTransactionLedger
    {
        uint64_t ClientSessionNonce{};
        uint64_t ConnectionGeneration{};
        uint64_t LastTransactionId{};
    };

    World& m_world;

    // Keyed by owner and character entity. Session values make a reconnect a
    // new bounded replay domain.
    std::unordered_map<uint64_t, EquipmentTransactionLedger> m_equipmentTransactions{};

    entt::scoped_connection m_inventoryChangeConnection;
    entt::scoped_connection m_equipmentChangeConnection;
    entt::scoped_connection m_drawWeaponConnection;
    entt::scoped_connection m_playerLeaveConnection;
    entt::scoped_connection m_characterRemoveConnection;
};
