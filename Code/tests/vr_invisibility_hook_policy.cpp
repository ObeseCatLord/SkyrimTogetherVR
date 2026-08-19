#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/InvisibilityHooks.h>

#include <array>

namespace
{
namespace Policy = SkyrimTogetherVR::GameplayAdapter::InvisibilityHookPolicy;
}

TEST_CASE("Invisibility finish hook pins the verified Skyrim VR target", "[skyrim-vr][invisibility]")
{
    constexpr std::array<std::uint8_t, 27> expectedPrologue{
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0xF9,
        0xE8, 0xA2, 0xEA, 0x01, 0x00, 0x48, 0x8B, 0x4F, 0x50,
        0x48, 0x85, 0xC9, 0x0F, 0x84, 0xD2, 0x00, 0x00, 0x00,
    };

    REQUIRE(Policy::HasPinnedTargetConfiguration());
    REQUIRE(Policy::kInvisibilityEffectFinishVrRva == 0x0054F500);
    REQUIRE(Policy::kInvisibilityEffectFinishVrPrologue == expectedPrologue);
}

TEST_CASE("Invisibility finish failure logging is bounded", "[skyrim-vr][invisibility]")
{
    REQUIRE_FALSE(Policy::ShouldLogAggregate(0));
    REQUIRE(Policy::ShouldLogAggregate(1));
    REQUIRE(Policy::ShouldLogAggregate(2));
    REQUIRE_FALSE(Policy::ShouldLogAggregate(3));
    REQUIRE(Policy::ShouldLogAggregate(64));
}
