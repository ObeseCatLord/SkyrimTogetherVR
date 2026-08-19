#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/WeatherNativeAccess.h>

namespace
{
using namespace SkyrimTogetherVR::GameplayAdapter::WeatherNativeAccess;
}

TEST_CASE("VR Sky weather targets are pinned to the decrypted executable entries", "[skyrim-vr][weather]")
{
    constexpr std::array<std::uint8_t, 25> expectedForceWeatherPrologue{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
        0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
        0x0F, 0xB6, 0xD8, 0x48, 0x8B, 0xF2, 0x48, 0x8B,
        0xF9,
    };
    constexpr std::array<std::uint8_t, 25> expectedReleaseWeatherOverridePrologue{
        0x48, 0x83, 0x79, 0x60, 0x00, 0x74, 0x12, 0x81,
        0x89, 0xDC, 0x01, 0x00, 0x00, 0x00, 0x00, 0x20,
        0x00, 0x48, 0xC7, 0x41, 0x60, 0x00, 0x00, 0x00,
        0x00,
    };

    REQUIRE(HasPinnedTargetConfiguration());
    REQUIRE(kForceWeatherVrRva == 0x03C48C0);
    REQUIRE(kReleaseWeatherOverrideVrRva == 0x03C4970);
    REQUIRE(kForceWeatherOverrideArgument);
    REQUIRE(kForceWeatherVrPrologue == expectedForceWeatherPrologue);
    REQUIRE(kReleaseWeatherOverrideVrPrologue == expectedReleaseWeatherOverridePrologue);
}
