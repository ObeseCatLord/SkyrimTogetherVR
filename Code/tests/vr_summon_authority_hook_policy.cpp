#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/SummonAuthorityHooks.h>

#include <array>
#include <cstdint>

namespace
{
namespace Policy = SkyrimTogetherVR::GameplayAdapter::SummonAuthorityHookPolicy;
}

TEST_CASE("Summon authority hook pins the exact Skyrim VR factory and registration contract", "[skyrim-vr][summon-authority]")
{
    constexpr std::array<std::uint8_t, 37> expectedPrologue{
        0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x48,
        0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89,
        0x5C, 0x24, 0x50, 0x48, 0x89, 0x6C, 0x24, 0x58, 0x49, 0x8B,
        0xF0, 0x48, 0x8B, 0xEA, 0x4C, 0x8B, 0xF1,
    };

    REQUIRE(Policy::HasPinnedFactoryTargetConfiguration());
    REQUIRE(Policy::HasPinnedFactoryRegistrationContract());
    REQUIRE(Policy::HasPinnedTargetConfiguration());
    REQUIRE(Policy::UsesDirectRvaFactoryTarget());
    REQUIRE(Policy::kSummonCreatureEffectFactoryVrRva == 0x0569920);
    REQUIRE(Policy::kSummonCreatureEffectFactoryExtent == 0xB1);
    REQUIRE(Policy::kSummonCreatureEffectFactoryVrPrologue == expectedPrologue);
    REQUIRE(Policy::kSummonCreatureEffectRegistrationThunkVrRva == 0x00902B0);
    REQUIRE(Policy::kSummonCreatureEffectRegistrationFactoryLoadVrRva == 0x00902BE);
    REQUIRE(Policy::kSummonCreatureEffectRegistrationArchetypeLoadVrRva == 0x00902D1);
    REQUIRE(Policy::kSummonCreatureEffectRegistrationHelperVrRva == 0x0556930);
    REQUIRE(Policy::kSummonCreatureEffectArchetype == 0x12);
}

TEST_CASE("Summon authority suppresses every managed or retiring caster without a remote magic bypass", "[skyrim-vr][summon-authority]")
{
    using Disposition = Policy::Disposition;

    REQUIRE(Policy::MustUseManagedRemoteActorLease());
    REQUIRE_FALSE(Policy::RemoteMagicApplicationBypassesSuppression());
    REQUIRE(Policy::Classify(false, false) == Disposition::CallOriginal);
    REQUIRE(Policy::Classify(true, false) == Disposition::Suppress);
    REQUIRE(Policy::Classify(false, true) == Disposition::Suppress);
    REQUIRE(Policy::Classify(true, true) == Disposition::Suppress);
}

TEST_CASE("Summon authority preserves the factory's nullable dispatcher contract", "[skyrim-vr][summon-authority]")
{
    using Disposition = Policy::Disposition;

    REQUIRE(Policy::DispatcherAcceptsNullFactoryResult());
    REQUIRE(Policy::NullFactoryResultSkipsPostInitialization());
    REQUIRE(Policy::IsExactlyOneOriginalCallPolicy(Disposition::Suppress, true, 0));
    REQUIRE_FALSE(Policy::IsExactlyOneOriginalCallPolicy(Disposition::Suppress, true, 1));
    REQUIRE(Policy::IsExactlyOneOriginalCallPolicy(Disposition::CallOriginal, true, 1));
    REQUIRE_FALSE(Policy::IsExactlyOneOriginalCallPolicy(Disposition::CallOriginal, false, 1));
    REQUIRE_FALSE(Policy::IsExactlyOneOriginalCallPolicy(Disposition::CallOriginal, true, 0));
    REQUIRE_FALSE(Policy::IsExactlyOneOriginalCallPolicy(Disposition::CallOriginal, true, 2));
}

TEST_CASE("Summon authority suppression logging is logarithmically bounded", "[skyrim-vr][summon-authority]")
{
    REQUIRE_FALSE(Policy::ShouldLogAggregate(0));
    REQUIRE(Policy::ShouldLogAggregate(1));
    REQUIRE(Policy::ShouldLogAggregate(2));
    REQUIRE_FALSE(Policy::ShouldLogAggregate(3));
    REQUIRE(Policy::ShouldLogAggregate(4));
    REQUIRE_FALSE(Policy::ShouldLogAggregate(6));
    REQUIRE(Policy::ShouldLogAggregate(8));
}
