#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/ActorAuthorityHooks.h>

#include <array>

namespace
{
using namespace SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHookPolicy;
}

TEST_CASE("Actor authority hooks pin verified Skyrim VR authority targets", "[skyrim-vr][actor-authority]")
{
    constexpr std::array<std::uint8_t, 21> expectedRotateX{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0x29, 0x74, 0x24, 0x20, 0x48, 0x8B, 0xD9, 0x0F, 0x28, 0xF1, 0x0F, 0x2E, 0x71, 0x48,
    };
    constexpr std::array<std::uint8_t, 21> expectedRotateY{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0x29, 0x74, 0x24, 0x20, 0x48, 0x8B, 0xD9, 0x0F, 0x28, 0xF1, 0x0F, 0x2E, 0x71, 0x4C,
    };
    constexpr std::array<std::uint8_t, 21> expectedRotateZ{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0x29, 0x74, 0x24, 0x20, 0x48, 0x8B, 0xD9, 0x0F, 0x28, 0xF1, 0x0F, 0x2E, 0x71, 0x50,
    };

    REQUIRE(SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHooks::kManagedRemoteActorRegistryCapacity == 64);
    REQUIRE(HasPinnedTargetConfiguration());
    REQUIRE(kDoDamageAddressLibraryId == 36345);
    REQUIRE(kDoDamageVrRva == 0x05DE930);
    REQUIRE(kDoDamageVrPrologue[0] == 0x48);
    REQUIRE(kDoDamageVrPrologue[18] == 0x0F);
    REQUIRE(kDoDamageVrPrologue.back() == 0x00);
    REQUIRE(kAddDeathItemsAddressLibraryId == 36218);
    REQUIRE(kAddDeathItemsVrRva == 0x05D80A0);
    REQUIRE(kAddDeathItemsVrPrologue[0] == 0x40);
    REQUIRE(kAddDeathItemsVrPrologue[14] == 0xEC);
    REQUIRE(kAddDeathItemsVrPrologue.back() == 0x70);
    REQUIRE(kApplyValueActiveEffectVrRva == 0x056E070);
    REQUIRE(kApplyValueActiveEffectVrPrologue.size() == 32);
    REQUIRE(kApplyValueActiveEffectVrPrologue[0] == 0x48);
    REQUIRE(kApplyValueActiveEffectVrPrologue[28] == 0xA0);
    REQUIRE(kApplyValueActiveEffectVrPrologue.back() == 0xFF);
    REQUIRE(kRestoreActorValueAddressLibraryId == 37513);
    REQUIRE(kRestoreActorValueVrRva == 0x06296B0);
    REQUIRE(kRestoreActorValueVrPrologue.size() == 32);
    REQUIRE(kRestoreActorValueVrPrologue[0] == 0x48);
    REQUIRE(kRestoreActorValueVrPrologue[10] == 0x0F);
    REQUIRE(kRestoreActorValueVrPrologue.back() == 0xDA);
    REQUIRE(kPredictLethalDoDamageVrRva == 0x05ECEA0);
    REQUIRE(kPredictLethalDoDamageVrPrologue.size() == 32);
    REQUIRE(kPredictLethalDoDamageVrPrologue[0] == 0x48);
    REQUIRE(kPredictLethalDoDamageVrPrologue[11] == 0xBA);
    REQUIRE(kPredictLethalDoDamageVrPrologue.back() == 0xC0);
    REQUIRE(kGenericSetPositionVrRva == 0x002A8010);
    REQUIRE(kGenericSetPositionVrPrologue.size() == 21);
    REQUIRE(kGenericSetPositionVrPrologue[0] == 0x48);
    REQUIRE(kGenericSetPositionVrPrologue[16] == 0x56);
    REQUIRE(kGenericSetPositionVrPrologue.back() == 0x20);
    REQUIRE(kActorSetPositionVrRva == 0x005DC380);
    REQUIRE(kActorSetPositionVrPrologue.size() == 20);
    REQUIRE(kActorSetPositionVrPrologue[0] == 0x40);
    REQUIRE(kActorSetPositionVrPrologue[15] == 0xFE);
    REQUIRE(kActorSetPositionVrPrologue.back() == 0x48);
    REQUIRE(kMoveToImplVrRva == 0x009E90E0);
    REQUIRE(kMoveToImplVrPrologue.size() == 16);
    REQUIRE(kMoveToImplVrPrologue[0] == 0x48);
    REQUIRE(kMoveToImplVrPrologue.back() == 0x57);
    REQUIRE(kRootMotionControllerProcessorVrRva == 0x005E0E20);
    REQUIRE(kRootMotionControllerProcessorVrPrologue.size() == 28);
    REQUIRE(kRootMotionControllerProcessorVrPrologue[0] == 0x48);
    REQUIRE(kRootMotionControllerProcessorVrPrologue[16] == 0xA8);
    REQUIRE(kRootMotionControllerProcessorVrPrologue.back() == 0x00);
    REQUIRE(kRotateXVrRva == 0x002A7D80);
    REQUIRE(kRotateXVrPrologue == expectedRotateX);
    REQUIRE(kRotateYVrRva == 0x002A7E40);
    REQUIRE(kRotateYVrPrologue == expectedRotateY);
    REQUIRE(kRotateZVrRva == 0x002A7F00);
    REQUIRE(kRotateZVrPrologue == expectedRotateZ);
}

TEST_CASE("Actor authority retirement exposes a fail-closed result contract", "[skyrim-vr][actor-authority]")
{
    using Result = SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHooks::ManagedRemoteActorRetirementResult;

    REQUIRE(static_cast<std::uint8_t>(Result::Quiescent) == 0);
    REQUIRE(static_cast<std::uint8_t>(Result::InvalidActor) == 1);
    REQUIRE(static_cast<std::uint8_t>(Result::NotRegistered) == 2);
    REQUIRE(static_cast<std::uint8_t>(Result::AlreadyRetiring) == 3);
    REQUIRE(static_cast<std::uint8_t>(Result::ReaderDrainTimedOut) == 4);
    REQUIRE(static_cast<std::uint8_t>(Result::RegistryInconsistent) == 5);
}

TEST_CASE("Actor authority lease operation exposes retiring state without raw membership", "[skyrim-vr][actor-authority]")
{
    using Disposition = SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHooks::ManagedRemoteActorOperationDisposition;

    REQUIRE(static_cast<std::uint8_t>(Disposition::UnmanagedOrInvalid) == 0);
    REQUIRE(static_cast<std::uint8_t>(Disposition::ManagedRemote) == 1);
    REQUIRE(static_cast<std::uint8_t>(Disposition::Retiring) == 2);
}

TEST_CASE("Actor authority hooks retain remote-root authority only during replay", "[skyrim-vr][actor-authority]")
{
    REQUIRE(ShouldCallRemoteRootMutationOriginal(false, false));
    REQUIRE(ShouldCallRemoteRootMutationOriginal(false, true));
    REQUIRE_FALSE(ShouldCallRemoteRootMutationOriginal(true, false));
    REQUIRE(ShouldCallRemoteRootMutationOriginal(true, true));
}

TEST_CASE("Actor authority hooks retain managed-remote rotation only during replay", "[skyrim-vr][actor-authority]")
{
    REQUIRE(ShouldCallRemoteRotationOriginal(false, false));
    REQUIRE(ShouldCallRemoteRotationOriginal(false, true));
    REQUIRE_FALSE(ShouldCallRemoteRotationOriginal(true, false));
    REQUIRE(ShouldCallRemoteRotationOriginal(true, true));
}

TEST_CASE("Actor authority keeps invisibility correction inside managed-actor ownership", "[skyrim-vr][actor-authority]")
{
    using Disposition = InvisibilityCorrectionDisposition;

    REQUIRE(ClassifyInvisibilityCorrection(false, false, false, false) == Disposition::InvalidActor);
    REQUIRE(ClassifyInvisibilityCorrection(true, true, true, false) == Disposition::LocalPlayer);
    REQUIRE(ClassifyInvisibilityCorrection(true, false, false, false) == Disposition::NotManagedRemote);
    REQUIRE(ClassifyInvisibilityCorrection(true, false, true, true) == Disposition::Retiring);
    REQUIRE(ClassifyInvisibilityCorrection(true, false, true, false) == Disposition::Correct);

    using Result = SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHooks::ManagedRemoteInvisibilityCorrectionResult;
    REQUIRE(static_cast<std::uint8_t>(Result::Corrected) == 0);
    REQUIRE(static_cast<std::uint8_t>(Result::InvalidActor) == 1);
    REQUIRE(static_cast<std::uint8_t>(Result::LocalPlayer) == 2);
    REQUIRE(static_cast<std::uint8_t>(Result::NotManagedRemote) == 3);
    REQUIRE(static_cast<std::uint8_t>(Result::Retiring) == 4);
    REQUIRE(static_cast<std::uint8_t>(Result::Failed) == 5);
}

TEST_CASE("Actor authority hooks classify every damage ownership branch", "[skyrim-vr][actor-authority]")
{
    REQUIRE(ClassifyDoDamage({false, false, false, false, false, false, false, false}) == DoDamageDisposition::CallOriginal);
    REQUIRE(ClassifyDoDamage({false, false, false, true, false, true, false, false}) == DoDamageDisposition::SuppressWithoutOriginal);
    REQUIRE(ClassifyDoDamage({false, false, false, true, false, true, false, true}) == DoDamageDisposition::CallOriginal);
    REQUIRE(ClassifyDoDamage({false, false, false, false, false, true, false, false}) == DoDamageDisposition::SuppressWithPredictedLethal);
    REQUIRE(ClassifyDoDamage({true, true, false, false, true, false, false, false}) == DoDamageDisposition::SuppressWithPredictedLethal);
    REQUIRE(ClassifyDoDamage({true, false, false, false, true, false, false, false}) == DoDamageDisposition::CallOriginalAndPublishRemoteNpcHealthDelta);
    REQUIRE(ClassifyDoDamage({true, false, false, false, false, false, false, false}) == DoDamageDisposition::SuppressWithPredictedLethal);
    REQUIRE(ClassifyDoDamage({true, false, false, false, false, true, false, false}) == DoDamageDisposition::SuppressWithPredictedLethal);
    REQUIRE(ClassifyDoDamage({true, false, true, false, false, false, false, true}) == DoDamageDisposition::SuppressWithoutOriginal);
    REQUIRE(ClassifyDoDamage({false, false, false, false, false, true, true, true}) == DoDamageDisposition::SuppressWithoutOriginal);
}

TEST_CASE("Actor authority hooks validate only the targeted remote-NPC health delta shape", "[skyrim-vr][actor-authority]")
{
    const TargetedRemoteNpcHealthDelta valid{0, 0x1234, kHealthActorValue, -17.5F};
    REQUIRE(IsValidTargetedRemoteNpcHealthDelta(valid));
    REQUIRE_FALSE(IsValidTargetedRemoteNpcHealthDelta({1, 0x1234, kHealthActorValue, -17.5F}));
    REQUIRE_FALSE(IsValidTargetedRemoteNpcHealthDelta({0, 0, kHealthActorValue, -17.5F}));
    REQUIRE_FALSE(IsValidTargetedRemoteNpcHealthDelta({0, kPlayerReferenceFormId, kHealthActorValue, -17.5F}));
    REQUIRE_FALSE(IsValidTargetedRemoteNpcHealthDelta({0, 0x1234, kHealthActorValue + 1, -17.5F}));
    REQUIRE_FALSE(IsValidTargetedRemoteNpcHealthDelta({0, 0x1234, kHealthActorValue, 0.0F}));
    REQUIRE_FALSE(IsValidTargetedRemoteNpcHealthDelta({0, 0x1234, kHealthActorValue, kMaximumHealthDeltaMagnitude + 1.0F}));
    REQUIRE_FALSE(IsValidTargetedRemoteNpcHealthDelta({0, 0x1234, kHealthActorValue, std::numeric_limits<float>::infinity()}));
    REQUIRE_FALSE(IsValidTargetedRemoteNpcHealthDelta({0, 0x1234, kHealthActorValue, std::numeric_limits<float>::quiet_NaN()}));
}

TEST_CASE("Actor authority hooks suppress generated death loot for every managed remote actor", "[skyrim-vr][actor-authority]")
{
    REQUIRE(ShouldCallAddDeathItemsOriginal(false));
    REQUIRE_FALSE(ShouldCallAddDeathItemsOriginal(true));
}

TEST_CASE("Actor authority replay bypass is restricted to root mutation", "[skyrim-vr][actor-authority]")
{
    REQUIRE(ShouldCallRemoteRootMutationOriginal(true, true));
    REQUIRE_FALSE(ShouldCallAddDeathItemsOriginal(true));
    REQUIRE_FALSE(ShouldCallApplyValueActiveEffectOriginal(true, 10.0F, kHealthActorValue));
    REQUIRE_FALSE(ShouldCallRestoreActorValueOriginal(true, static_cast<std::int32_t>(kHealthActorValue)));
}

TEST_CASE("Actor authority hooks suppress only positive managed-remote ActiveEffect health", "[skyrim-vr][actor-authority]")
{
    REQUIRE(ResolveActiveEffectActorValue(7, kUseActiveEffectActorValue) == 7);
    REQUIRE(ResolveActiveEffectActorValue(7, kHealthActorValue) == kHealthActorValue);
    REQUIRE(ResolveActiveEffectActorValue(kHealthActorValue, 8) == 8);

    REQUIRE(ShouldCallApplyValueActiveEffectOriginal(false, 10.0F, kHealthActorValue));
    REQUIRE_FALSE(ShouldCallApplyValueActiveEffectOriginal(true, 10.0F, kHealthActorValue));
    REQUIRE(ShouldCallApplyValueActiveEffectOriginal(true, -10.0F, kHealthActorValue));
    REQUIRE(ShouldCallApplyValueActiveEffectOriginal(true, 0.0F, kHealthActorValue));
    REQUIRE(ShouldCallApplyValueActiveEffectOriginal(true, 10.0F, kHealthActorValue + 1));
}

TEST_CASE("Actor authority hooks suppress managed-remote RestoreActorValue health", "[skyrim-vr][actor-authority]")
{
    REQUIRE(ShouldCallRestoreActorValueOriginal(false, static_cast<std::int32_t>(kHealthActorValue)));
    REQUIRE_FALSE(ShouldCallRestoreActorValueOriginal(true, static_cast<std::int32_t>(kHealthActorValue)));
    REQUIRE(ShouldCallRestoreActorValueOriginal(true, static_cast<std::int32_t>(kHealthActorValue) + 1));
}

TEST_CASE("Actor authority hooks retain desktop predicted-lethal return for suppressed remote-player damage", "[skyrim-vr][actor-authority]")
{
    REQUIRE(IsPredictedDoDamageLethal(20.0F, -20.0F));
    REQUIRE(IsPredictedDoDamageLethal(20.0F, -25.0F));
    REQUIRE_FALSE(IsPredictedDoDamageLethal(20.0F, -19.0F));
    REQUIRE_FALSE(IsPredictedDoDamageLethal(20.0F, std::numeric_limits<float>::quiet_NaN()));
    REQUIRE_FALSE(IsPredictedDoDamageLethal(std::numeric_limits<float>::infinity(), -20.0F));
    REQUIRE_FALSE(IsPredictedDoDamageLethal(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()));
}
