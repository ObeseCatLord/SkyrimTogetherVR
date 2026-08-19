#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/WaypointHooks.h>

namespace
{
namespace Waypoint = SkyrimTogetherVR::GameplayAdapter::WaypointHooks;
}

TEST_CASE("VR waypoint hook pins the independently verified local targets", "[skyrim-vr][waypoint]")
{
    constexpr std::array<std::uint8_t, 16> expectedSet{
        0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24,
    };
    constexpr std::array<std::uint8_t, 16> expectedRemove{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x48, 0x8D, 0x05, 0x1F, 0x20, 0x8C,
    };

    REQUIRE(Waypoint::HasPinnedTargetConfiguration());
    REQUIRE(Waypoint::kSetWaypointVrRva == 0x06C74D0);
    REQUIRE(Waypoint::kRemoveWaypointVrRva == 0x06C7630);
    REQUIRE(Waypoint::kSetWaypointVrPrologue == expectedSet);
    REQUIRE(Waypoint::kRemoveWaypointVrPrologue == expectedRemove);
}

TEST_CASE("VR waypoint capture rejects replay and duplicate publication", "[skyrim-vr][waypoint]")
{
    REQUIRE_FALSE(Waypoint::ShouldPublishObservedWaypoint(true, true, false));
    REQUIRE_FALSE(Waypoint::ShouldPublishObservedWaypoint(false, false, false));
    REQUIRE_FALSE(Waypoint::ShouldPublishObservedWaypoint(false, true, true));
    REQUIRE(Waypoint::ShouldPublishObservedWaypoint(false, true, false));
}

TEST_CASE("VR waypoint hook aggregate logging is bounded", "[skyrim-vr][waypoint]")
{
    REQUIRE_FALSE(Waypoint::ShouldLogAggregate(0));
    REQUIRE(Waypoint::ShouldLogAggregate(1));
    REQUIRE(Waypoint::ShouldLogAggregate(2));
    REQUIRE_FALSE(Waypoint::ShouldLogAggregate(3));
    REQUIRE(Waypoint::ShouldLogAggregate(64));
}
