#pragma once

#include <cstdint>
#include <vector>

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Stl.hpp>

#include <Structs/Inventory.h>

namespace SkyrimTogether::Encoding
{
inline const GameId kLegacyRightHandEquipSlot{0, 0x00013F42};
inline const GameId kLegacyLeftHandEquipSlot{0, 0x00013F43};

struct LegacyEquipmentChange
{
    GameId Item{};
    GameId Slot{};
    std::uint32_t Count{};
    bool Unequip{};
    bool IsSpell{};
    bool IsShout{};

    [[nodiscard]] bool operator==(const LegacyEquipmentChange&) const noexcept = default;
};

struct LegacyEquipmentChanges
{
    // Consumers preserve the original protocol ordering by sending every
    // unequip before every equip.
    std::vector<LegacyEquipmentChange> Unequips{};
    std::vector<LegacyEquipmentChange> Equips{};
};

// Retains only state needed to derive legacy incremental notifications.
[[nodiscard]] Inventory CaptureEquipmentBaseline(const Inventory& acInventory);

// Derives the legacy incremental protocol from two complete worn snapshots.
[[nodiscard]] LegacyEquipmentChanges DeriveLegacyEquipmentChanges(
    const Inventory& acPrevious, const Inventory& acCurrent);
} // namespace SkyrimTogether::Encoding
