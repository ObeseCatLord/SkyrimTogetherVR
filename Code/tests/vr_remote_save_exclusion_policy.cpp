#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/RemoteSaveExclusion.h>

#include <array>

namespace
{
namespace Policy = SkyrimTogetherVR::GameplayAdapter::RemoteSaveExclusion;
}

TEST_CASE("VR remote save exclusion pins the audited temporary operation", "[skyrim-vr][remote-save-exclusion]")
{
    constexpr std::array<std::uint8_t, 11> expectedPrologue{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xD2, 0x48, 0x8B, 0xD9,
    };

    REQUIRE(Policy::HasPinnedTargetConfiguration());
    REQUIRE(Policy::kSetTemporaryVrRva == 0x01A4A50);
    REQUIRE(Policy::kSetTemporaryVrPrologue == expectedPrologue);
}

TEST_CASE("VR remote save exclusion verifies the temporary form flag without layout writes", "[skyrim-vr][remote-save-exclusion]")
{
    REQUIRE(Policy::kTemporaryFormFlag == 0x00004000U);
    REQUIRE_FALSE(Policy::HasTemporaryFormFlag(0));
    REQUIRE(Policy::HasTemporaryFormFlag(Policy::kTemporaryFormFlag));
    REQUIRE(Policy::HasTemporaryFormFlag(0x80004000U));
}

TEST_CASE("VR remote save exclusion bounds repeated validation diagnostics", "[skyrim-vr][remote-save-exclusion]")
{
    REQUIRE_FALSE(Policy::ShouldLogValidationFailure(0));
    REQUIRE(Policy::ShouldLogValidationFailure(1));
    REQUIRE(Policy::ShouldLogValidationFailure(2));
    REQUIRE_FALSE(Policy::ShouldLogValidationFailure(3));
    REQUIRE(Policy::ShouldLogValidationFailure(4));
}
