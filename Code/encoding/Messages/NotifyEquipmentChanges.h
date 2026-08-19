#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Inventory.h>

struct NotifyEquipmentChanges final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyEquipmentChanges;

    NotifyEquipmentChanges()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyEquipmentChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && TransactionId == acRhs.TransactionId &&
               CanonicalRevision == acRhs.CanonicalRevision && ResyncRequestId == acRhs.ResyncRequestId &&
               ItemId == acRhs.ItemId && EquipSlotId == acRhs.EquipSlotId && Count == acRhs.Count &&
               Unequip == acRhs.Unequip && IsSpell == acRhs.IsSpell && IsShout == acRhs.IsShout &&
               FinalEquipment == acRhs.FinalEquipment &&
               FinalEquipment.CurrentMagicEquipment == acRhs.FinalEquipment.CurrentMagicEquipment;
    }

    uint32_t ServerId{};
    // A non-zero transaction carries the complete final worn inventory and
    // magic selection. Transaction-zero is the original single-event form.
    uint64_t TransactionId{};
    // Server-issued final-equipment revision and, for a direct recovery
    // response, the request it satisfies. Both are zero for legacy deltas.
    uint32_t CanonicalRevision{};
    uint32_t ResyncRequestId{};
    GameId ItemId{};
    GameId EquipSlotId{};
    uint32_t Count{};
    bool Unequip = false;
    bool IsSpell = false;
    bool IsShout = false;

    Inventory FinalEquipment{};
};
