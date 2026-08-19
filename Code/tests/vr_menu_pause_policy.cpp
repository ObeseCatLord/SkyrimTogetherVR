#include <catch2/catch.hpp>

#include <Games/Skyrim/Interface/MenuPausePolicy.h>

using namespace SkyrimTogetherVR::MenuPausePolicy;

TEST_CASE("VR menu pause policy preserves the desktop parity allowlist")
{
    STATIC_REQUIRE(
        kAllowList ==
        std::array<std::string_view, 9>{"TweenMenu", "MagicMenu", "StatsMenu", "InventoryMenu", "MessageBoxMenu", "ContainerMenu", "FavoritesMenu", "Tutorial Menu", "Console"});
    REQUIRE(IsAllowlisted("InventoryMenu"));
    REQUIRE(IsAllowlisted("MagicMenu"));
    REQUIRE(IsAllowlisted("Tutorial Menu"));
    REQUIRE_FALSE(IsAllowlisted("MapMenu"));
    REQUIRE_FALSE(IsAllowlisted("Journal Menu"));
    REQUIRE_FALSE(IsAllowlisted("RaceSex Menu"));
}

TEST_CASE("VR menu pause policy requires an online client and supported runtime API")
{
    REQUIRE(ShouldUnpause("InventoryMenu", true, false, true));
    REQUIRE_FALSE(ShouldUnpause("InventoryMenu", false, false, true));
    REQUIRE_FALSE(ShouldUnpause("InventoryMenu", true, true, true));
    REQUIRE_FALSE(ShouldUnpause("InventoryMenu", true, false, false));
    REQUIRE_FALSE(ShouldUnpause("MapMenu", true, false, true));
}

TEST_CASE("VR menu pause policy clears only pause and freeze flags")
{
    constexpr std::uint32_t otherFlags = 0x8000 | 0x2000 | 0x100;
    constexpr std::uint32_t pausedFlags = otherFlags | kPausesGame | kFreezeFrameBackground | kFreezeFramePause;

    STATIC_REQUIRE(UnpausedFlags(pausedFlags) == otherFlags);
    STATIC_REQUIRE(UnpausedFlags(otherFlags) == otherFlags);
    STATIC_REQUIRE(RestoredFlags(otherFlags, kPausesGame | kFreezeFramePause) == (otherFlags | kPausesGame | kFreezeFramePause));
}

TEST_CASE("VR menu pause policy defers disconnect restoration until the menu is off-stack")
{
    REQUIRE(DecideAction("InventoryMenu", true, false, true, false, false) == Action::Unpause);
    REQUIRE(DecideAction("InventoryMenu", false, false, true, true, true) == Action::None);
    REQUIRE(DecideAction("InventoryMenu", false, false, true, false, true) == Action::Restore);
    REQUIRE(DecideAction("InventoryMenu", false, false, true, false, false) == Action::None);
}
