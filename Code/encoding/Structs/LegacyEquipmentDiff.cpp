#include <Structs/LegacyEquipmentDiff.h>

#include <algorithm>
#include <utility>

namespace SkyrimTogether::Encoding
{
namespace
{
struct LegacyPhysicalEquipmentState
{
    GameId Item{};
    GameId Slot{};
    std::uint32_t Count{};

    [[nodiscard]] bool operator==(const LegacyPhysicalEquipmentState&) const noexcept = default;
};

[[nodiscard]] bool LessGameId(const GameId& acLeft, const GameId& acRight) noexcept
{
    return acLeft.ModId != acRight.ModId ? acLeft.ModId < acRight.ModId : acLeft.BaseId < acRight.BaseId;
}

[[nodiscard]] std::vector<LegacyPhysicalEquipmentState> ExpandLegacyPhysicalState(const Inventory& acInventory)
{
    std::vector<LegacyPhysicalEquipmentState> result;
    result.reserve(acInventory.Entries.size() * 2);
    for (const auto& entry : acInventory.Entries) {
        if (!entry.IsWorn() || !entry.BaseId || entry.Count <= 0)
            continue;

        const auto count = static_cast<std::uint32_t>(entry.Count);
        const bool weapon = (entry.EquipmentFlags & Inventory::Entry::kEquipmentWeapon) != 0;
        const bool ammo = (entry.EquipmentFlags & Inventory::Entry::kEquipmentAmmo) != 0;
        if (entry.ExtraWorn)
            result.push_back({entry.BaseId, weapon && !ammo ? kLegacyRightHandEquipSlot : GameId{}, count});
        if (entry.ExtraWornLeft)
            result.push_back({entry.BaseId, kLegacyLeftHandEquipSlot, count});
    }
    std::sort(result.begin(), result.end(), [](const auto& acLeft, const auto& acRight) noexcept {
        if (acLeft.Item != acRight.Item)
            return LessGameId(acLeft.Item, acRight.Item);
        if (acLeft.Slot != acRight.Slot)
            return LessGameId(acLeft.Slot, acRight.Slot);
        return acLeft.Count < acRight.Count;
    });
    return result;
}

void AppendPhysicalChanges(const Inventory& acPrevious, const Inventory& acCurrent,
                           LegacyEquipmentChanges& arChanges)
{
    const auto previous = ExpandLegacyPhysicalState(acPrevious);
    const auto current = ExpandLegacyPhysicalState(acCurrent);
    for (const auto& state : previous) {
        if (std::find(current.begin(), current.end(), state) == current.end())
            arChanges.Unequips.push_back({state.Item, state.Slot, state.Count, true, false, false});
    }
    for (const auto& state : current) {
        if (std::find(previous.begin(), previous.end(), state) == previous.end())
            arChanges.Equips.push_back({state.Item, state.Slot, state.Count, false, false, false});
    }
}

void AppendMagicChange(const GameId& acOld, const GameId& acNew, const GameId& acSlot,
                       const bool aShout, LegacyEquipmentChanges& arChanges)
{
    if (acOld == acNew)
        return;
    if (acOld)
        arChanges.Unequips.push_back({acOld, acSlot, 1, true, !aShout, aShout});
    if (acNew)
        arChanges.Equips.push_back({acNew, acSlot, 1, false, !aShout, aShout});
}
} // namespace

Inventory CaptureEquipmentBaseline(const Inventory& acInventory)
{
    Inventory baseline{};
    baseline.CurrentMagicEquipment = acInventory.CurrentMagicEquipment;
    for (const auto& entry : acInventory.Entries) {
        if (!entry.IsWorn())
            continue;

        Inventory::Entry copiedEntry{};
        copiedEntry.BaseId = entry.BaseId;
        copiedEntry.Count = entry.Count;
        copiedEntry.ExtraWorn = entry.ExtraWorn;
        copiedEntry.ExtraWornLeft = entry.ExtraWornLeft;
        copiedEntry.EquipmentFlags = entry.EquipmentFlags;
        baseline.Entries.emplace_back(std::move(copiedEntry));
    }
    return baseline;
}

LegacyEquipmentChanges DeriveLegacyEquipmentChanges(const Inventory& acPrevious, const Inventory& acCurrent)
{
    LegacyEquipmentChanges changes{};
    AppendPhysicalChanges(acPrevious, acCurrent, changes);
    AppendMagicChange(acPrevious.CurrentMagicEquipment.LeftHandSpell, acCurrent.CurrentMagicEquipment.LeftHandSpell,
                      kLegacyLeftHandEquipSlot, false, changes);
    AppendMagicChange(acPrevious.CurrentMagicEquipment.RightHandSpell, acCurrent.CurrentMagicEquipment.RightHandSpell,
                      kLegacyRightHandEquipSlot, false, changes);
    AppendMagicChange(acPrevious.CurrentMagicEquipment.Shout, acCurrent.CurrentMagicEquipment.Shout,
                      GameId{}, true, changes);
    return changes;
}
} // namespace SkyrimTogether::Encoding
