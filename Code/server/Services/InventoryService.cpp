#include "InventoryService.h"

#include <Components.h>
#include <World.h>
#include <GameServer.h>
#include <Game/Player.h>
#include <Events/CharacterRemoveEvent.h>
#include <Events/PlayerLeaveEvent.h>

#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/DrawWeaponRequest.h>

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <Setting.h>
#include <Structs/GameplayCapabilities.h>
namespace
{
Console::Setting bEnableItemDrops{"Gameplay:bEnableItemDrops", "(Experimental) Syncs dropped items by players", false};
constexpr std::size_t kMaximumEquipmentSnapshotEntries = 64;
constexpr std::int32_t kMaximumEquipmentCount = 10'000;
constexpr GameId kRightHandEquipSlot{0, 0x00013F42};
constexpr GameId kLeftHandEquipSlot{0, 0x00013F43};
constexpr auto kVREquipmentCapability =
    SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VREquipmentRelay);

[[nodiscard]] bool IsZeroExtraData(const Inventory::Entry& acEntry) noexcept
{
    return acEntry.ExtraCharge == 0.0F && !acEntry.ExtraEnchantId && acEntry.ExtraEnchantCharge == 0 &&
           acEntry.EnchantData.Effects.empty() && !acEntry.ExtraHealth && !acEntry.ExtraPoisonId &&
           acEntry.ExtraPoisonCount == 0 && acEntry.ExtraSoulLevel == 0 &&
           !acEntry.ExtraEnchantRemoveUnequip && !acEntry.IsQuestItem;
}

[[nodiscard]] std::int64_t AuthoritativeCount(const Inventory& acInventory, const GameId& acItem) noexcept
{
    std::int64_t result{};
    for (const auto& entry : acInventory.Entries) {
        if (entry.BaseId == acItem && entry.Count > 0)
            result += entry.Count;
    }
    return result;
}

[[nodiscard]] bool IsValidFinalEquipmentRequest(const RequestEquipmentChanges& acMessage,
                                                 const Inventory& acAuthoritativeInventory) noexcept
{
    if (acMessage.TransactionId == 0 || acMessage.ItemId || acMessage.EquipSlotId || acMessage.Count != 0 ||
        acMessage.Unequip || acMessage.IsSpell || acMessage.IsShout || acMessage.IsAmmo ||
        acMessage.CurrentInventory.Entries.size() > kMaximumEquipmentSnapshotEntries)
        return false;

    std::unordered_set<GameId> seen;
    for (const auto& entry : acMessage.CurrentInventory.Entries) {
        if (!entry.BaseId || entry.Count <= 0 || entry.Count > kMaximumEquipmentCount || !entry.IsWorn() ||
            !IsZeroExtraData(entry) || !seen.emplace(entry.BaseId).second ||
            AuthoritativeCount(acAuthoritativeInventory, entry.BaseId) < entry.Count)
            return false;
    }

    // MagicEquipment serializes only optional GameIds, so no non-zero magic
    // form can be represented without its complete GameId payload.
    return true;
}

[[nodiscard]] bool IsValidEquipmentRequest(const PacketEvent<RequestEquipmentChanges>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;
    const bool snapshotOnly = !message.ItemId && !message.EquipSlotId && message.Count == 0 && !message.Unequip &&
                              !message.IsSpell && !message.IsShout && !message.IsAmmo;
    if (message.ServerId == 0 || (!snapshotOnly && (!message.ItemId || message.Count == 0)) ||
        message.Count > static_cast<std::uint32_t>(kMaximumEquipmentCount) ||
        message.CurrentInventory.Entries.size() > kMaximumEquipmentSnapshotEntries ||
        (message.IsSpell && message.IsShout) || ((message.IsSpell || message.IsShout) && message.IsAmmo) ||
        ((message.IsSpell || message.IsShout) && message.Count != 1) ||
        (message.IsSpell && message.EquipSlotId != kLeftHandEquipSlot &&
         message.EquipSlotId != kRightHandEquipSlot) ||
        (message.IsShout && message.EquipSlotId) ||
        (!message.IsSpell && !message.IsShout && message.EquipSlotId &&
         message.EquipSlotId != kLeftHandEquipSlot && message.EquipSlotId != kRightHandEquipSlot))
        return false;
    for (const auto& entry : message.CurrentInventory.Entries) {
        if (!entry.BaseId || entry.Count <= 0 || entry.Count > kMaximumEquipmentCount || !entry.IsWorn())
            return false;
        if (std::count_if(message.CurrentInventory.Entries.begin(), message.CurrentInventory.Entries.end(),
                [&entry](const Inventory::Entry& acOther) { return acOther.BaseId == entry.BaseId; }) != 1)
            return false;
    }
    if (!snapshotOnly) {
        if (message.IsSpell) {
            const auto& magic = message.CurrentInventory.CurrentMagicEquipment;
            const auto& finalSelection = message.EquipSlotId == kLeftHandEquipSlot ?
                magic.LeftHandSpell : magic.RightHandSpell;
            if ((!message.Unequip && finalSelection != message.ItemId) ||
                (message.Unequip && finalSelection == message.ItemId))
                return false;
        } else if (message.IsShout) {
            const auto& finalSelection = message.CurrentInventory.CurrentMagicEquipment.Shout;
            if ((!message.Unequip && finalSelection != message.ItemId) ||
                (message.Unequip && finalSelection == message.ItemId))
                return false;
        } else {
            const auto finalItem = std::find_if(
                message.CurrentInventory.Entries.begin(), message.CurrentInventory.Entries.end(),
                [&message](const Inventory::Entry& acEntry) { return acEntry.BaseId == message.ItemId; });
            if ((!message.Unequip && finalItem == message.CurrentInventory.Entries.end()) ||
                (message.Unequip && finalItem != message.CurrentInventory.Entries.end()))
                return false;
        }
    }
    return true;
}
}

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_inventoryChangeConnection = aDispatcher.sink<PacketEvent<RequestInventoryChanges>>().connect<&InventoryService::OnInventoryChanges>(this);
    m_equipmentChangeConnection = aDispatcher.sink<PacketEvent<RequestEquipmentChanges>>().connect<&InventoryService::OnEquipmentChanges>(this);
    m_drawWeaponConnection = aDispatcher.sink<PacketEvent<DrawWeaponRequest>>().connect<&InventoryService::OnWeaponDrawnRequest>(this);
    m_playerLeaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&InventoryService::OnPlayerLeave>(this);
    m_characterRemoveConnection = aDispatcher.sink<CharacterRemoveEvent>().connect<&InventoryService::OnCharacterRemove>(this);
}

void InventoryService::OnInventoryChanges(const PacketEvent<RequestInventoryChanges>& acMessage) noexcept
{
    auto& message = acMessage.Packet;
    const auto entity = static_cast<entt::entity>(message.ServerId);
    if (!acMessage.pPlayer || message.ServerId == 0 || !message.Item.BaseId || message.Item.Count == 0 ||
        message.Item.Count < -kMaximumEquipmentCount || message.Item.Count > kMaximumEquipmentCount ||
        !m_world.valid(entity) || !m_world.all_of<InventoryComponent, CellIdComponent>(entity))
        return;

    const bool ownsCharacter = m_world.all_of<CharacterComponent, OwnerComponent>(entity) &&
                               m_world.get<OwnerComponent>(entity).GetOwner() == acMessage.pPlayer;
    const bool observesObject = m_world.all_of<ObjectComponent>(entity) &&
                                m_world.get<CellIdComponent>(entity).Cell == acMessage.pPlayer->GetCellComponent().Cell;
    if (!ownsCharacter && !observesObject)
        return;

    auto& inventoryComponent = m_world.get<InventoryComponent>(entity);
    inventoryComponent.Content.AddOrRemoveEntry(message.Item);

    if (!message.UpdateClients)
        return;

    NotifyInventoryChanges notify;
    notify.ServerId = message.ServerId;
    notify.Item = message.Item;

    notify.Drop = bEnableItemDrops ? message.Drop : false;

    if (!GameServer::Get()->SendToPlayersInRange(notify, entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void InventoryService::OnEquipmentChanges(const PacketEvent<RequestEquipmentChanges>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    if (!acMessage.pPlayer || message.ServerId == 0)
        return;

    auto view = m_world.view<CharacterComponent, OwnerComponent, InventoryComponent>();
    const auto it = view.find(static_cast<entt::entity>(message.ServerId));
    if (it == view.end() || view.get<OwnerComponent>(*it).GetOwner() != acMessage.pPlayer)
        return;

    auto& inventoryComponent = view.get<InventoryComponent>(*it);

    if (message.TransactionId != 0) {
        if (!SkyrimTogether::Protocol::HasCapability(
                acMessage.pPlayer->GetGameplayCapabilities(),
                SkyrimTogether::Protocol::GameplayCapability::VREquipmentRelay) ||
            !IsValidFinalEquipmentRequest(message, inventoryComponent.Content))
            return;

        const auto key = (static_cast<uint64_t>(acMessage.pPlayer->GetId()) << 32) | message.ServerId;
        if (m_equipmentTransactions.size() >= 512 && m_equipmentTransactions.find(key) == m_equipmentTransactions.end())
            return;
        auto& ledger = m_equipmentTransactions[key];
        if (ledger.ClientSessionNonce != acMessage.pPlayer->GetClientSessionNonce() ||
            ledger.ConnectionGeneration != acMessage.pPlayer->GetConnectionGeneration()) {
            ledger = {acMessage.pPlayer->GetClientSessionNonce(), acMessage.pPlayer->GetConnectionGeneration(), 0};
        }
        if (message.TransactionId <= ledger.LastTransactionId)
            return;

        // Every final-state field, including all worn counts, was checked
        // against this authoritative inventory before this single mutation.
        inventoryComponent.Content.UpdateEquipment(message.CurrentInventory);
        ledger.LastTransactionId = message.TransactionId;

        NotifyEquipmentChanges notify{};
        notify.ServerId = message.ServerId;
        notify.TransactionId = message.TransactionId;
        notify.FinalEquipment = message.CurrentInventory;
        const entt::entity origin = static_cast<entt::entity>(message.ServerId);
        // Final snapshots are understood only by the staged VR receiver. Do
        // not fan them out to the native legacy receiver, and do not derive a
        // multi-message desktop delta here: either would expose a partial
        // final state. Transaction-zero desktop events below retain their
        // existing all-client legacy fanout.
        if (!GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
                notify, origin, kVREquipmentCapability, acMessage.GetSender()))
            spdlog::error("{}: SendToPlayersWithCapabilitiesInRange failed", __FUNCTION__);
        return;
    }

    // Transaction-zero remains the desktop single-event protocol.  A zero
    // item snapshot is no longer accepted: complete snapshots require a
    // non-zero transaction ID and therefore cannot be partially broadcast.
    if (!message.ItemId || !IsValidEquipmentRequest(acMessage))
        return;
    if (!message.IsSpell && !message.IsShout &&
        AuthoritativeCount(inventoryComponent.Content, message.ItemId) < message.Count)
        return;

    // This only updates worn metadata on existing authoritative inventory;
    // it never adds counts from an equipment snapshot.
    inventoryComponent.Content.UpdateEquipment(message.CurrentInventory);

    NotifyEquipmentChanges notify;
    notify.ServerId = message.ServerId;
    notify.ItemId = message.ItemId;
    notify.EquipSlotId = message.EquipSlotId;
    notify.Count = message.Count;
    notify.Unequip = message.Unequip;
    notify.IsSpell = message.IsSpell;
    notify.IsShout = message.IsShout;

    const entt::entity cOrigin = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cOrigin, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void InventoryService::OnWeaponDrawnRequest(const PacketEvent<DrawWeaponRequest>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto it = characterView.find(static_cast<entt::entity>(message.Id));

    if (it != std::end(characterView) && characterView.get<OwnerComponent>(*it).GetOwner() == acMessage.pPlayer)
    {
        auto& characterComponent = characterView.get<CharacterComponent>(*it);
        characterComponent.SetWeaponDrawn(message.IsWeaponDrawn);
        spdlog::debug("Updating weapon drawn state {:x}:{}", message.Id, message.IsWeaponDrawn);
    }
}

void InventoryService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (!acEvent.pPlayer)
        return;
    const auto playerId = acEvent.pPlayer->GetId();
    std::erase_if(m_equipmentTransactions, [playerId](const auto& acEntry) noexcept {
        return static_cast<std::uint32_t>(acEntry.first >> 32) == playerId;
    });
}

void InventoryService::OnCharacterRemove(const CharacterRemoveEvent& acEvent) noexcept
{
    if (acEvent.ServerId == 0)
        return;
    std::erase_if(m_equipmentTransactions, [&acEvent](const auto& acEntry) noexcept {
        return static_cast<std::uint32_t>(acEntry.first) == acEvent.ServerId;
    });
}
