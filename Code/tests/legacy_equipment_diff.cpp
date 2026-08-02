#include <catch2/catch.hpp>

#include <Structs/LegacyEquipmentDiff.h>

namespace LegacyEquipment = SkyrimTogether::Encoding;

namespace
{
GameId MakeId(const std::uint32_t aBaseId)
{
    return {1, aBaseId};
}

Inventory::Entry MakeWornEntry(const GameId& acItem, const std::int32_t aCount,
                               const bool aRight, const bool aLeft, const std::uint8_t aFlags)
{
    Inventory::Entry entry{};
    entry.BaseId = acItem;
    entry.Count = aCount;
    entry.ExtraWorn = aRight;
    entry.ExtraWornLeft = aLeft;
    entry.EquipmentFlags = aFlags;
    return entry;
}
} // namespace

TEST_CASE("legacy equipment baseline retains only worn and magic state", "[legacy-equipment]")
{
    const auto wornItem = MakeId(0x101);
    const auto unwornItem = MakeId(0x102);
    const auto spell = MakeId(0x201);

    Inventory inventory{};
    inventory.Entries.push_back(MakeWornEntry(wornItem, 2, true, false, Inventory::Entry::kEquipmentWeapon));
    inventory.Entries.push_back(MakeWornEntry(unwornItem, 7, false, false, 0));
    inventory.CurrentMagicEquipment.LeftHandSpell = spell;

    const auto baseline = LegacyEquipment::CaptureEquipmentBaseline(inventory);

    REQUIRE(baseline.Entries.size() == 1);
    CHECK(baseline.Entries.front().BaseId == wornItem);
    CHECK(baseline.Entries.front().Count == 2);
    CHECK(baseline.Entries.front().ExtraWorn);
    CHECK_FALSE(baseline.Entries.front().ExtraWornLeft);
    CHECK(baseline.Entries.front().EquipmentFlags == Inventory::Entry::kEquipmentWeapon);
    CHECK(baseline.CurrentMagicEquipment.LeftHandSpell == spell);
}

TEST_CASE("legacy equipment diff reports a first baseline unequip", "[legacy-equipment]")
{
    const auto item = MakeId(0x301);
    Inventory authoritative{};
    authoritative.Entries.push_back(MakeWornEntry(item, 1, true, false, Inventory::Entry::kEquipmentWeapon));

    Inventory current{};
    current.Entries.push_back(MakeWornEntry(item, 1, false, false, Inventory::Entry::kEquipmentWeapon));

    const auto changes = LegacyEquipment::DeriveLegacyEquipmentChanges(
        LegacyEquipment::CaptureEquipmentBaseline(authoritative), current);

    REQUIRE(changes.Unequips.size() == 1);
    CHECK((changes.Unequips.front() == LegacyEquipment::LegacyEquipmentChange{
        item, LegacyEquipment::kLegacyRightHandEquipSlot, 1, true, false, false}));
    CHECK(changes.Equips.empty());
}

TEST_CASE("legacy equipment diff keeps the pre-mutation baseline for replacement ordering", "[legacy-equipment]")
{
    const auto oldItem = MakeId(0x401);
    const auto newItem = MakeId(0x402);
    Inventory authoritative{};
    authoritative.Entries.push_back(MakeWornEntry(oldItem, 1, true, false, Inventory::Entry::kEquipmentWeapon));
    const auto baseline = LegacyEquipment::CaptureEquipmentBaseline(authoritative);

    // The ordinary inventory mutation can replace metadata before the final
    // VR snapshot arrives; its diff must still originate from this baseline.
    authoritative.Entries.front() = MakeWornEntry(newItem, 1, true, false, Inventory::Entry::kEquipmentWeapon);
    const auto changes = LegacyEquipment::DeriveLegacyEquipmentChanges(baseline, authoritative);

    REQUIRE(changes.Unequips.size() == 1);
    REQUIRE(changes.Equips.size() == 1);
    CHECK(changes.Unequips.front().Item == oldItem);
    CHECK(changes.Unequips.front().Unequip);
    CHECK(changes.Equips.front().Item == newItem);
    CHECK_FALSE(changes.Equips.front().Unequip);
}

TEST_CASE("legacy equipment diff equips right-hand weapons and preserves zero classification", "[legacy-equipment]")
{
    const auto weapon = MakeId(0x501);
    const auto unclassified = MakeId(0x502);
    Inventory current{};
    current.Entries.push_back(MakeWornEntry(weapon, 1, true, false, Inventory::Entry::kEquipmentWeapon));
    current.Entries.push_back(MakeWornEntry(unclassified, 3, true, false, 0));
    current.Entries.push_back(MakeWornEntry(MakeId(0x503), 0, true, false, Inventory::Entry::kEquipmentWeapon));
    current.Entries.push_back(MakeWornEntry(GameId{}, 1, true, false, Inventory::Entry::kEquipmentWeapon));

    const auto changes = LegacyEquipment::DeriveLegacyEquipmentChanges(Inventory{}, current);

    REQUIRE(changes.Equips.size() == 2);
    CHECK((changes.Equips[0] == LegacyEquipment::LegacyEquipmentChange{
        weapon, LegacyEquipment::kLegacyRightHandEquipSlot, 1, false, false, false}));
    CHECK((changes.Equips[1] == LegacyEquipment::LegacyEquipmentChange{
        unclassified, GameId{}, 3, false, false, false}));
    CHECK(changes.Unequips.empty());
}

TEST_CASE("legacy equipment magic changes emit unequips before equips", "[legacy-equipment]")
{
    const auto oldSpell = MakeId(0x601);
    const auto newSpell = MakeId(0x602);
    Inventory previous{};
    previous.CurrentMagicEquipment.LeftHandSpell = oldSpell;
    Inventory current{};
    current.CurrentMagicEquipment.LeftHandSpell = newSpell;

    const auto changes = LegacyEquipment::DeriveLegacyEquipmentChanges(previous, current);

    REQUIRE(changes.Unequips.size() == 1);
    REQUIRE(changes.Equips.size() == 1);
    CHECK((changes.Unequips.front() == LegacyEquipment::LegacyEquipmentChange{
        oldSpell, LegacyEquipment::kLegacyLeftHandEquipSlot, 1, true, true, false}));
    CHECK((changes.Equips.front() == LegacyEquipment::LegacyEquipmentChange{
        newSpell, LegacyEquipment::kLegacyLeftHandEquipSlot, 1, false, true, false}));
}

TEST_CASE("legacy equipment diff ignores empty snapshots and duplicate state", "[legacy-equipment]")
{
    const auto item = MakeId(0x701);
    Inventory snapshot{};
    snapshot.Entries.push_back(MakeWornEntry(item, 2, true, false, Inventory::Entry::kEquipmentWeapon));

    const auto emptyChanges = LegacyEquipment::DeriveLegacyEquipmentChanges(Inventory{}, Inventory{});
    CHECK(emptyChanges.Unequips.empty());
    CHECK(emptyChanges.Equips.empty());

    const auto duplicateChanges = LegacyEquipment::DeriveLegacyEquipmentChanges(snapshot, snapshot);
    CHECK(duplicateChanges.Unequips.empty());
    CHECK(duplicateChanges.Equips.empty());
}
