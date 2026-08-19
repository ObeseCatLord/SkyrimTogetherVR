#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/ProgressionHooks.h>

namespace
{
namespace Progression = SkyrimTogetherVR::GameplayAdapter::ProgressionHooks;
}

TEST_CASE("Progression hook filters the exact desktop combat-skill set")
{
    REQUIRE(Progression::IsCombatSkillActorValue(6));
    REQUIRE(Progression::IsCombatSkillActorValue(9));
    REQUIRE(Progression::IsCombatSkillActorValue(18));
    REQUIRE(Progression::IsCombatSkillActorValue(22));
    REQUIRE_FALSE(Progression::IsCombatSkillActorValue(10));
    REQUIRE_FALSE(Progression::IsCombatSkillActorValue(23));
}

TEST_CASE("Progression hook only publishes a valid unsuppressed positive delta")
{
    constexpr auto maximum = 100000.0F;
    REQUIRE(Progression::ShouldPublishExactExperience(6, 1.0F, maximum, false, false));
    REQUIRE_FALSE(Progression::ShouldPublishExactExperience(6, 0.0F, maximum, false, false));
    REQUIRE_FALSE(Progression::ShouldPublishExactExperience(6, -1.0F, maximum, false, false));
    REQUIRE_FALSE(Progression::ShouldPublishExactExperience(6, maximum + 1.0F, maximum, false, false));
    REQUIRE_FALSE(Progression::ShouldPublishExactExperience(10, 1.0F, maximum, false, false));
    REQUIRE_FALSE(Progression::ShouldPublishExactExperience(6, 1.0F, maximum, true, false));
    REQUIRE_FALSE(Progression::ShouldPublishExactExperience(6, 1.0F, maximum, false, true));
}

TEST_CASE("Progression hook preserves the independently verified VR target contract")
{
    REQUIRE(Progression::kAddSkillExperienceVrAddressLibraryId == 39413);
    REQUIRE(Progression::kAddSkillExperienceVrRva == 0x06C30B0);
    REQUIRE(Progression::kCalculateExperienceVrRva == 0x03F2FF0);
    REQUIRE(Progression::kAddSkillExperienceVrPrologue[0] == 0x48);
    REQUIRE(Progression::kAddSkillExperienceVrPrologue[22] == 0x00);
    REQUIRE(Progression::kCalculateExperienceVrPrologue[0] == 0x81);
    REQUIRE(Progression::kCalculateExperienceVrPrologue[15] == 0x48);
}
