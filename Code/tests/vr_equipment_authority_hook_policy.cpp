#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/EquipmentAuthorityHooks.h>

#include <array>
#include <limits>

namespace
{
using namespace SkyrimTogetherVR::GameplayAdapter::EquipmentAuthorityHookPolicy;

constexpr std::array<Operation, 6> kAllOperations{
    Operation::EquipObject,
    Operation::UnequipObject,
    Operation::EquipSpell,
    Operation::UnequipSpell,
    Operation::EquipShout,
    Operation::UnequipShout,
};

constexpr std::array<std::uint8_t, 18> kExpectedEquipObject{
    0x40, 0x56, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83,
    0xEC, 0x38, 0x48, 0x3B, 0x15, 0x66, 0x18, 0x98, 0x02,
};
constexpr std::array<std::uint8_t, 37> kExpectedUnequipObject{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x3B, 0x15, 0xC3, 0x0F, 0x98, 0x02,
};
constexpr std::array<std::uint8_t, 18> kExpectedEquipSpell{
    0x40, 0x56, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83,
    0xEC, 0x38, 0x48, 0x3B, 0x15, 0x16, 0x1B, 0x98, 0x02,
};
constexpr std::array<std::uint8_t, 37> kExpectedUnequipSpell{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x3B, 0x15, 0x13, 0x12, 0x98, 0x02,
};
constexpr std::array<std::uint8_t, 18> kExpectedEquipShout{
    0x40, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x48, 0x83,
    0xEC, 0x38, 0x48, 0x3B, 0x15, 0xB6, 0x15, 0x98, 0x02,
};
constexpr std::array<std::uint8_t, 37> kExpectedUnequipShout{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x55,
    0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x3B, 0x15, 0x73, 0x0D, 0x98, 0x02,
};
constexpr std::array<std::uint8_t, 16> kExpectedPublicUnequipSpell{
    0x48, 0x85, 0xD2, 0x74, 0x56, 0x48, 0x89, 0x5C,
    0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
};
constexpr std::array<std::uint8_t, 16> kExpectedPublicUnequipShout{
    0x48, 0x83, 0xEC, 0x38, 0x48, 0x85, 0xD2, 0x74,
    0x1D, 0x4D, 0x85, 0xC0, 0x74, 0x18, 0x4C, 0x8D,
};
}

TEST_CASE("Equipment authority hooks pin exact Skyrim VR targets", "[skyrim-vr][equipment-authority]")
{
    REQUIRE(HasPinnedTargetConfiguration());
    REQUIRE(kEquipObjectVrRva == 0x0642E30);
    REQUIRE(kUnequipObjectVrRva == 0x06436C0);
    REQUIRE(kEquipSpellVrRva == 0x0642B80);
    REQUIRE(kUnequipSpellVrRva == 0x0643470);
    REQUIRE(kEquipShoutVrRva == 0x06430E0);
    REQUIRE(kUnequipShoutVrRva == 0x0643910);
    REQUIRE(kPublicUnequipSpellVrRva == 0x0641350);
    REQUIRE(kPublicUnequipShoutVrRva == 0x0641430);
    REQUIRE(kEquipObjectVrPrologue == kExpectedEquipObject);
    REQUIRE(kUnequipObjectVrPrologue == kExpectedUnequipObject);
    REQUIRE(kEquipSpellVrPrologue == kExpectedEquipSpell);
    REQUIRE(kUnequipSpellVrPrologue == kExpectedUnequipSpell);
    REQUIRE(kEquipShoutVrPrologue == kExpectedEquipShout);
    REQUIRE(kUnequipShoutVrPrologue == kExpectedUnequipShout);
    REQUIRE(kPublicUnequipSpellVrPrologue == kExpectedPublicUnequipSpell);
    REQUIRE(kPublicUnequipShoutVrPrologue == kExpectedPublicUnequipShout);
}

TEST_CASE("Equipment authority policy admits only exact remote replay", "[skyrim-vr][equipment-authority]")
{
    for (const auto operation : kAllOperations) {
        REQUIRE(Classify(operation, false, false, false, false) == Disposition::CallOriginal);
        REQUIRE(Classify(operation, true, false, false, false) == Disposition::Suppress);
        REQUIRE(Classify(operation, true, false, true, false) == Disposition::CallOriginal);
        REQUIRE(Classify(operation, true, true, true, true) == Disposition::Suppress);
    }
}

TEST_CASE("Equipment authority retains the object-only admitted inventory exception", "[skyrim-vr][equipment-authority]")
{
    REQUIRE(Classify(Operation::UnequipObject, true, false, false, true) == Disposition::CallOriginal);
    REQUIRE(Classify(Operation::EquipObject, true, false, false, true) == Disposition::Suppress);
    REQUIRE(Classify(Operation::EquipSpell, true, false, false, true) == Disposition::Suppress);
    REQUIRE(Classify(Operation::UnequipSpell, true, false, false, true) == Disposition::Suppress);
    REQUIRE(Classify(Operation::EquipShout, true, false, false, true) == Disposition::Suppress);
    REQUIRE(Classify(Operation::UnequipShout, true, false, false, true) == Disposition::Suppress);
}

TEST_CASE("Equipment authority scopes are depth-counted and non-wrapping", "[skyrim-vr][equipment-authority]")
{
    REQUIRE(CanEnterScope(0));
    REQUIRE(EnterScope(0) == 1);
    REQUIRE(EnterScope(EnterScope(0)) == 2);
    REQUIRE(LeaveScope(2) == 1);
    REQUIRE(LeaveScope(1) == 0);
    REQUIRE(LeaveScope(0) == 0);
    REQUIRE_FALSE(CanEnterScope(std::numeric_limits<std::uint32_t>::max()));
    REQUIRE(EnterScope(std::numeric_limits<std::uint32_t>::max()) == std::numeric_limits<std::uint32_t>::max());
}

TEST_CASE("Equipment authority requires synchronous spell and shout unequip", "[skyrim-vr][equipment-authority]")
{
    REQUIRE_FALSE(RequiresSynchronousUnequip(Operation::EquipObject));
    REQUIRE_FALSE(RequiresSynchronousUnequip(Operation::UnequipObject));
    REQUIRE_FALSE(RequiresSynchronousUnequip(Operation::EquipSpell));
    REQUIRE(RequiresSynchronousUnequip(Operation::UnequipSpell));
    REQUIRE_FALSE(RequiresSynchronousUnequip(Operation::EquipShout));
    REQUIRE(RequiresSynchronousUnequip(Operation::UnequipShout));
}

TEST_CASE("Equipment authority installs atomically and uninstalls in reverse", "[skyrim-vr][equipment-authority]")
{
    REQUIRE(kInstallOrder[0] == Operation::EquipObject);
    REQUIRE(kInstallOrder[5] == Operation::UnequipShout);
    REQUIRE(kUninstallOrder[0] == Operation::UnequipShout);
    REQUIRE(kUninstallOrder[5] == Operation::EquipObject);
    for (std::size_t index{}; index < kInstallOrder.size(); ++index)
        REQUIRE(kInstallOrder[index] == kUninstallOrder[kUninstallOrder.size() - 1 - index]);
}

TEST_CASE("Equipment authority aggregates logs at power-of-two suppression counts", "[skyrim-vr][equipment-authority]")
{
    REQUIRE_FALSE(ShouldLogAggregate(0));
    REQUIRE(ShouldLogAggregate(1));
    REQUIRE(ShouldLogAggregate(2));
    REQUIRE_FALSE(ShouldLogAggregate(3));
    REQUIRE(ShouldLogAggregate(4));
    REQUIRE_FALSE(ShouldLogAggregate(6));
    REQUIRE(ShouldLogAggregate(8));
}
