#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/DropHooks.h>

namespace
{
namespace Policy = SkyrimTogetherVR::GameplayAdapter::DropHookPolicy;

constexpr Policy::PendingDrop MakePendingDrop()
{
    return {
        .ActorAddress = 0x1000,
        .ObjectAddress = 0x2000,
        .ActorFormId = 0x14,
        .ObjectFormId = 0x1234,
        .StableUniqueId = 77,
        .Count = 3,
        .Generation = 1,
        .HasStableUniqueId = true,
    };
}

constexpr Policy::ContainerChangedEvent MakeMatchingEvent()
{
    return {
        .OldContainer = 0x14,
        .NewContainer = 0,
        .ObjectFormId = 0x1234,
        .UniqueId = 77,
        .Count = 3,
    };
}
} // namespace

TEST_CASE("VR drop hook pins only the verified PlayerCharacter override", "[skyrim-vr][drop]")
{
    constexpr std::array<std::uint8_t, 19> expectedPrologue{
        0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
        0x41, 0x57, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00,
    };

    REQUIRE(Policy::HasPinnedTargetConfiguration());
    REQUIRE(Policy::UsesDirectRvaHookTarget());
    REQUIRE(Policy::kPlayerCharacterVtableVrRva == 0x016E2230);
    REQUIRE(Policy::kPlayerCharacterDropObjectVtableSlot == 0xCD);
    REQUIRE(Policy::kPlayerCharacterDropObjectVtableEntryRva == 0x016E2898);
    REQUIRE(Policy::kPlayerDropObjectVrRva == 0x006C00F0);
    REQUIRE(Policy::kPlayerDropObjectVrPrologue == expectedPrologue);
    REQUIRE(Policy::IsExactPlayerDropObjectTarget(0x006C00F0));
    REQUIRE_FALSE(Policy::IsExactPlayerDropObjectTarget(0x00709970));
}

TEST_CASE("VR drop attribution consumes one exact matching pending drop", "[skyrim-vr][drop]")
{
    auto pending = MakePendingDrop();
    const auto event = MakeMatchingEvent();

    const auto first = Policy::ClassifyContainerChangedEvent(&pending, event, false, false);
    REQUIRE(first == Policy::ContainerChangedDisposition::MatchedDrop);
    REQUIRE(Policy::ShouldPublishInventoryDrop(first));

    pending.Consumed = true;
    const auto duplicate = Policy::ClassifyContainerChangedEvent(&pending, event, false, false);
    REQUIRE(duplicate == Policy::ContainerChangedDisposition::AlreadyConsumed);
    REQUIRE_FALSE(Policy::ShouldPublishInventoryDrop(duplicate));
}

TEST_CASE("VR consume and destruction removals are never inferred as drops", "[skyrim-vr][drop]")
{
    const auto consumed = Policy::ContainerChangedEvent{
        .OldContainer = 0x14,
        .NewContainer = 0,
        .ObjectFormId = 0x4567,
        .UniqueId = 0,
        .Count = 1,
    };

    const auto disposition = Policy::ClassifyContainerChangedEvent(nullptr, consumed, false, false);
    REQUIRE(disposition == Policy::ContainerChangedDisposition::NormalRemoval);
    REQUIRE_FALSE(Policy::ShouldPublishInventoryDrop(disposition));
}

TEST_CASE("VR failed or unmatched pending drops fail closed to removal", "[skyrim-vr][drop]")
{
    const auto pending = MakePendingDrop();
    auto mismatched = MakeMatchingEvent();
    mismatched.Count = 2;

    const auto disposition = Policy::ClassifyContainerChangedEvent(&pending, mismatched, false, false);
    REQUIRE(disposition == Policy::ContainerChangedDisposition::Mismatch);
    REQUIRE_FALSE(Policy::ShouldPublishInventoryDrop(disposition));
}

TEST_CASE("VR nested drop scopes fail closed even when event fields match", "[skyrim-vr][drop]")
{
    const auto pending = MakePendingDrop();
    const auto disposition = Policy::ClassifyContainerChangedEvent(
        &pending, MakeMatchingEvent(), false, true);

    REQUIRE(disposition == Policy::ContainerChangedDisposition::NestedAmbiguity);
    REQUIRE_FALSE(Policy::ShouldPublishInventoryDrop(disposition));
}

TEST_CASE("VR remote inventory suppression takes precedence over a pending drop", "[skyrim-vr][drop]")
{
    const auto pending = MakePendingDrop();
    const auto disposition = Policy::ClassifyContainerChangedEvent(
        &pending, MakeMatchingEvent(), true, true);

    REQUIRE(disposition == Policy::ContainerChangedDisposition::RemoteSuppressed);
    REQUIRE_FALSE(Policy::ShouldPublishInventoryDrop(disposition));
}

TEST_CASE("VR drop diagnostics use logarithmic aggregate logging", "[skyrim-vr][drop]")
{
    REQUIRE_FALSE(Policy::ShouldLogAggregate(0));
    REQUIRE(Policy::ShouldLogAggregate(1));
    REQUIRE(Policy::ShouldLogAggregate(2));
    REQUIRE_FALSE(Policy::ShouldLogAggregate(3));
    REQUIRE(Policy::ShouldLogAggregate(64));
}
