#include <catch2/catch.hpp>

#define TP_SKYRIM_VR 1
#define TP_SKYRIM_VR_STARTUP_MODE_TEST 1
#include "../client/SkyrimVM64.cpp"

namespace
{
using SkyrimTogetherVR::StartupMode::MainDrawStartupGate;
using SkyrimTogetherVR::StartupMode::MainDrawTransition;

TEST_CASE("VR startup modes only transition BeginMain from an active outermost draw", "[skyrim-vr][startup]")
{
    MainDrawStartupGate gate;

    CHECK(gate.TryBegin(VrUpdateMode::Off, true, 101) == MainDrawTransition::None);
    CHECK(gate.TryBegin(VrUpdateMode::Observe, true, 101) == MainDrawTransition::None);
    CHECK(gate.TryBegin(VrUpdateMode::Active, false, 101) == MainDrawTransition::None);
    CHECK(gate.TryBegin(VrUpdateMode::Active, true, 101) == MainDrawTransition::BeginMain);
}

TEST_CASE("VR startup transition is single-owner and does not retry a failed first attempt", "[skyrim-vr][startup]")
{
    MainDrawStartupGate gate;

    REQUIRE(gate.TryBegin(VrUpdateMode::Active, true, 101) == MainDrawTransition::BeginMain);
    CHECK(gate.TryBegin(VrUpdateMode::Active, true, 101) == MainDrawTransition::AlreadyAttempted);
    CHECK(gate.TryBegin(VrUpdateMode::Active, true, 202) == MainDrawTransition::OwnerMismatch);
}
} // namespace
