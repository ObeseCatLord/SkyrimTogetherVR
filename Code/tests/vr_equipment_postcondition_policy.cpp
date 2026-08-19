#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/EquipmentPostconditionPolicy.h>

namespace
{
using namespace SkyrimTogetherVR::GameplayAdapter::EquipmentPostconditionPolicy;

State CompleteState()
{
    return {
        kCompleteStateFields,
        {{0x100, 2, true, false, true, false}, {0x200, 1, false, true, false, false}, {0x300, 20, true, false, false, true}},
        0x200,
        0x100,
        0x400,
        0,
        0x500,
    };
}

State DualWieldState()
{
    auto state = CompleteState();
    state.Worn[0] = {0x100, 2, true, true, true, false};
    state.LeftObject = 0x100;
    state.RightObject = 0x100;
    return state;
}
}

TEST_CASE("Equipment postcondition accepts the complete observed final state", "[skyrim-vr][equipment-postcondition]")
{
    const auto requested = CompleteState();
    const auto observed = CompleteState();

    REQUIRE(MatchesFinalState(requested, observed));
}

TEST_CASE("Equipment postcondition rejects a mismatched final field", "[skyrim-vr][equipment-postcondition]")
{
    const auto requested = CompleteState();
    auto observed = CompleteState();
    observed.Worn[2].Ammo = false;

    REQUIRE_FALSE(MatchesFinalState(requested, observed));

    observed = CompleteState();
    observed.LeftObject = 0;
    REQUIRE_FALSE(MatchesFinalState(requested, observed));
}

TEST_CASE("Equipment postcondition fails closed when an observed field is missing", "[skyrim-vr][equipment-postcondition]")
{
    const auto requested = CompleteState();
    auto observed = CompleteState();
    observed.AvailableFields = HandObjects | SelectedMagic;

    REQUIRE_FALSE(MatchesFinalState(requested, observed));
    REQUIRE_FALSE(IsObjectWorn(observed, 0x100, false));
}

TEST_CASE("Equipment postcondition proves unequip from observed state", "[skyrim-vr][equipment-postcondition]")
{
    auto observed = CompleteState();

    REQUIRE_FALSE(IsObjectUnequipped(observed, 0x100));
    REQUIRE_FALSE(MatchesSelectedSpell(observed, 0x400, true, false));
    REQUIRE_FALSE(MatchesSelectedShout(observed, 0x500, false));

    observed.Worn.erase(observed.Worn.begin());
    observed.RightObject = 0;
    observed.LeftSpell = 0;
    observed.Shout = 0;

    REQUIRE(IsObjectUnequipped(observed, 0x100));
    REQUIRE(MatchesSelectedSpell(observed, 0x400, true, false));
    REQUIRE(MatchesSelectedShout(observed, 0x500, false));
}

TEST_CASE("Equipment postcondition proves a left-hand unequip without clearing the right hand", "[skyrim-vr][equipment-postcondition]")
{
    auto observed = DualWieldState();

    REQUIRE_FALSE(IsObjectUnequipped(observed, 0x100, true));

    observed.Worn[0] = {0x100, 2, true, false, true, false};
    observed.LeftObject = 0;

    REQUIRE(IsObjectUnequipped(observed, 0x100, true));
    REQUIRE_FALSE(IsObjectUnequipped(observed, 0x100, false));
    REQUIRE_FALSE(IsObjectUnequipped(observed, 0x100));
}

TEST_CASE("Equipment postcondition proves a right-hand unequip without clearing the left hand", "[skyrim-vr][equipment-postcondition]")
{
    auto observed = DualWieldState();

    REQUIRE_FALSE(IsObjectUnequipped(observed, 0x100, false));

    observed.Worn[0] = {0x100, 2, false, true, true, false};
    observed.LeftObject = 0x100;
    observed.RightObject = 0;

    REQUIRE(IsObjectUnequipped(observed, 0x100, false));
    REQUIRE_FALSE(IsObjectUnequipped(observed, 0x100, true));
    REQUIRE_FALSE(IsObjectUnequipped(observed, 0x100));
}

TEST_CASE("Equipment postcondition requires global object absence for a slotless unequip", "[skyrim-vr][equipment-postcondition]")
{
    auto observed = DualWieldState();

    REQUIRE_FALSE(IsObjectUnequipped(observed, 0x100));

    observed.Worn.erase(observed.Worn.begin());
    observed.LeftObject = 0;
    observed.RightObject = 0;

    REQUIRE(IsObjectUnequipped(observed, 0x100));
}
