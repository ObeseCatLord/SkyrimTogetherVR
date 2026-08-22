#include <catch2/catch.hpp>

#include <Games/Skyrim/Interface/MenuPausePolicy.h>

using namespace SkyrimTogetherVR::MenuPausePolicy;

TEST_CASE("VR menu pause policy covers Skyrim VR gameplay pause menus only")
{
    STATIC_REQUIRE(
        kAllowList ==
        std::array<std::string_view, 19>{
            "TweenMenu", "MagicMenu", "StatsMenu", "InventoryMenu", "MessageBoxMenu", "ContainerMenu", "FavoritesMenu", "Tutorial Menu", "Console", "BarterMenu",
            "Book Menu", "Crafting Menu", "Dialogue Menu", "GiftMenu", "Lockpicking Menu", "Quantity Menu", "Sleep/Wait Menu", "Training Menu", "LevelUp Menu"});
    STATIC_REQUIRE(HasUniqueAllowlistNames());

    REQUIRE(IsAllowlisted("InventoryMenu"));
    REQUIRE(IsAllowlisted("MagicMenu"));
    REQUIRE(IsAllowlisted("Tutorial Menu"));
    REQUIRE(IsAllowlisted("BarterMenu"));
    REQUIRE(IsAllowlisted("Book Menu"));
    REQUIRE(IsAllowlisted("Crafting Menu"));
    REQUIRE(IsAllowlisted("Dialogue Menu"));
    REQUIRE(IsAllowlisted("GiftMenu"));
    REQUIRE(IsAllowlisted("Lockpicking Menu"));
    REQUIRE(IsAllowlisted("Quantity Menu"));
    REQUIRE(IsAllowlisted("Sleep/Wait Menu"));
    REQUIRE(IsAllowlisted("Training Menu"));
    REQUIRE(IsAllowlisted("LevelUp Menu"));

    REQUIRE_FALSE(IsAllowlisted("Loading Menu"));
    REQUIRE_FALSE(IsAllowlisted("Main Menu"));
    REQUIRE_FALSE(IsAllowlisted("TitleSequence Menu"));
    REQUIRE_FALSE(IsAllowlisted("Fader Menu"));
    REQUIRE_FALSE(IsAllowlisted("HUD Menu"));
    REQUIRE_FALSE(IsAllowlisted("RaceSex Menu"));
    REQUIRE_FALSE(IsAllowlisted("Journal Menu"));
    REQUIRE_FALSE(IsAllowlisted("MapMenu"));
}

TEST_CASE("VR menu pause policy requires an underlying connected transport and supported runtime API")
{
    const bool connectedBeforeAuthentication = true;
    const bool disconnectedTransport = false;

    REQUIRE(ShouldUnpause("InventoryMenu", connectedBeforeAuthentication, false, true));
    REQUIRE_FALSE(ShouldUnpause("InventoryMenu", disconnectedTransport, false, true));
    REQUIRE_FALSE(ShouldUnpause("InventoryMenu", true, true, true));
    REQUIRE_FALSE(ShouldUnpause("InventoryMenu", true, false, false));
    REQUIRE_FALSE(ShouldUnpause("Journal Menu", true, false, true));
    REQUIRE_FALSE(ShouldUnpause("MapMenu", true, false, true));
    REQUIRE_FALSE(ShouldUnpause("Loading Menu", true, false, true));

    REQUIRE(DecideAction("InventoryMenu", true, false, true, false, false) == Action::Unpause);
    REQUIRE(DecideAction("Journal Menu", true, false, true, false, false) == Action::None);
    REQUIRE(DecideAction("MapMenu", true, false, true, false, false) == Action::None);
}

TEST_CASE("VR menu pause policy permits only private creator-time cross-thread mutation off-stack")
{
    REQUIRE(CanMutateFlags(MutationContext::Creator, false, false));
    REQUIRE_FALSE(CanMutateFlags(MutationContext::Creator, false, true));
    REQUIRE(CanMutateFlags(MutationContext::PeriodicScan, true, false));
    REQUIRE_FALSE(CanMutateFlags(MutationContext::PeriodicScan, false, false));
    REQUIRE_FALSE(CanMutateFlags(MutationContext::PeriodicScan, true, true));
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
    const bool connectedTransport = true;
    const bool disconnectedTransport = false;

    REQUIRE(DecideAction("InventoryMenu", connectedTransport, false, true, false, false) == Action::Unpause);
    REQUIRE(DecideAction("InventoryMenu", disconnectedTransport, false, true, true, true) == Action::None);
    REQUIRE(DecideAction("InventoryMenu", disconnectedTransport, false, true, false, true) == Action::Restore);
    REQUIRE(DecideAction("InventoryMenu", disconnectedTransport, false, true, false, false) == Action::None);
}
