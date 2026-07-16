#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Inventory.h>

struct RequestEquipmentChanges final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestEquipmentChanges;

    RequestEquipmentChanges()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestEquipmentChanges() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestEquipmentChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && TransactionId == acRhs.TransactionId &&
               ItemId == acRhs.ItemId && EquipSlotId == acRhs.EquipSlotId && Count == acRhs.Count &&
               Unequip == acRhs.Unequip && IsSpell == acRhs.IsSpell && IsShout == acRhs.IsShout &&
               IsAmmo == acRhs.IsAmmo && CurrentInventory == acRhs.CurrentInventory &&
               CurrentInventory.CurrentMagicEquipment == acRhs.CurrentInventory.CurrentMagicEquipment;
    }

    uint32_t ServerId{};
    // Non-zero selects the bounded final-state transaction form.  Legacy
    // desktop equipment events retain TransactionId == 0.
    uint64_t TransactionId{};
    GameId ItemId{};
    GameId EquipSlotId{};
    uint32_t Count{};
    bool Unequip = false;
    bool IsSpell = false;
    bool IsShout = false;
    bool IsAmmo = false;

    Inventory CurrentInventory{};
};
