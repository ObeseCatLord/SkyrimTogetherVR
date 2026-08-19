#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/QuestNativeAccess.h>

using namespace SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess;

TEST_CASE("VR quest SetStage target is pinned to the decrypted executable entry", "[skyrim-vr][quest]")
{
    constexpr std::array<std::uint8_t, 24> expected{
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0xF6, 0x81,
        0xDC, 0x00, 0x00, 0x00, 0x01, 0x44, 0x0F, 0xB7,
        0xC2, 0x48, 0x8B, 0xF9, 0x74, 0x72, 0x48, 0x8D,
    };

    REQUIRE(HasPinnedTargetConfiguration());
    REQUIRE(kSetStageVrPrologue == expected);
}

TEST_CASE("VR quest stage history uses the engine done bit", "[skyrim-vr][quest]")
{
    REQUIRE(IsStageRecordDone(20, 0x01, 20));
    REQUIRE(IsStageRecordDone(20, 0x03, 20));
    REQUIRE_FALSE(IsStageRecordDone(20, 0x00, 20));
    REQUIRE_FALSE(IsStageRecordDone(20, 0x01, 30));
}
