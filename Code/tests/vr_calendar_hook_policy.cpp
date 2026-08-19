#include <catch2/catch.hpp>

#include <Games/Skyrim/CalendarSnapshot.h>
#include <Structs/TimeModel.h>
#include <vr_gameplay_bridge/CalendarHooks.h>

#include <limits>

namespace
{
using namespace SkyrimTogetherVR::GameplayAdapter::CalendarHooks;
}

TEST_CASE("VR calendar hook locks only a complete operational session", "[skyrim-vr][calendar]")
{
    REQUIRE_FALSE(ShouldSuppressCalendarUpdate(false, true, 1, 1, 1, 1));
    REQUIRE_FALSE(ShouldSuppressCalendarUpdate(true, false, 1, 1, 1, 1));
    REQUIRE_FALSE(ShouldSuppressCalendarUpdate(true, true, 0, 1, 0, 1));
    REQUIRE_FALSE(ShouldSuppressCalendarUpdate(true, true, 1, 0, 1, 0));
    REQUIRE_FALSE(ShouldSuppressCalendarUpdate(true, true, 1, 1, 2, 1));
    REQUIRE_FALSE(ShouldSuppressCalendarUpdate(true, true, 1, 1, 1, 2));
    REQUIRE(ShouldSuppressCalendarUpdate(true, true, 1, 1, 1, 1));
}

TEST_CASE("VR calendar hook pins the verified direct Address Library target", "[skyrim-vr][calendar]")
{
    constexpr std::array<std::uint8_t, 24> expectedPrologue{
        0x40, 0x53, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x41, 0x30, 0x48, 0x8B, 0xD9, 0x0F, 0x29, 0x74, 0x24, 0x70, 0x0F, 0x28, 0xF1,
    };

    REQUIRE(HasPinnedTargetConfiguration());
    REQUIRE(kCalendarUpdateVrAddressId == 35402);
    REQUIRE(kCalendarUpdateVrRva == 0x05AD8F0);
    REQUIRE(kCalendarUpdateVrPrologue == expectedPrologue);
}

TEST_CASE("VR calendar hook classifies spans without address arithmetic overflow", "[skyrim-vr][calendar]")
{
    REQUIRE(IsSpanWithinSegment(0x1000, 0x100, 0x1000, 0x100));
    REQUIRE(IsSpanWithinSegment(0x1000, 0x100, 0x10F0, 0x10));
    REQUIRE_FALSE(IsSpanWithinSegment(0x1000, 0x100, 0x10F0, 0x11));
    REQUIRE_FALSE(IsSpanWithinSegment(0x1000, 0x100, 0x0FFF, 0x1));
    REQUIRE_FALSE(IsSpanWithinSegment(0x1000, 0x100, 0x1000, 0));
    REQUIRE_FALSE(IsSpanWithinSegment(
        std::numeric_limits<std::uintptr_t>::max() - 0x10,
        0x20,
        std::numeric_limits<std::uintptr_t>::max() - 0x10,
        0x20));
    REQUIRE_FALSE(IsSpanWithinSegment(
        std::numeric_limits<std::uintptr_t>::max() - 0x10,
        0x20,
        std::numeric_limits<std::uintptr_t>::max() - 0x08,
        0x10));
}

TEST_CASE("VR calendar hook advances only bounded authoritative elapsed time", "[skyrim-vr][calendar]")
{
    REQUIRE(PlanAuthoritativeAdvance(false, 1.0F, 20.0F).Disposition == AuthoritativeAdvanceDisposition::PassThrough);
    REQUIRE(PlanAuthoritativeAdvance(true, -1.0F, 20.0F).Disposition == AuthoritativeAdvanceDisposition::Reanchor);
    REQUIRE(PlanAuthoritativeAdvance(true, 1.0F, 1001.0F).Disposition == AuthoritativeAdvanceDisposition::Reanchor);

    const auto normal = PlanAuthoritativeAdvance(true, 1.0F, 20.0F);
    REQUIRE(normal.Disposition == AuthoritativeAdvanceDisposition::Advance);
    REQUIRE(normal.ChunkCount == 1);
    REQUIRE(normal.MaximumChunkSeconds == 2160.0F);

    const auto highScale = PlanAuthoritativeAdvance(true, 300.0F, 1000.0F);
    REQUIRE(highScale.Disposition == AuthoritativeAdvanceDisposition::Advance);
    REQUIRE(highScale.MaximumChunkSeconds > 43.19F);
    REQUIRE(highScale.MaximumChunkSeconds < 43.21F);
    REQUIRE(highScale.ChunkCount == 7);

    const auto longSuspend = PlanAuthoritativeAdvance(true, 7200.0F, 1000.0F);
    REQUIRE(longSuspend.Disposition == AuthoritativeAdvanceDisposition::Advance);
    REQUIRE(longSuspend.ChunkCount == kMaximumAuthoritativeAdvanceChunks);
    REQUIRE(longSuspend.MaximumChunkSeconds * 1000.0F <=
            kMaximumGameHoursPerAdvanceChunk * 3600.0F);

    const auto paused = PlanAuthoritativeAdvance(true, 1.0F, 0.0F);
    REQUIRE(paused.Disposition == AuthoritativeAdvanceDisposition::Advance);
    REQUIRE(paused.ChunkCount == 0);
}

TEST_CASE("VR calendar reservation prevents reentrant duplicate advancement", "[skyrim-vr][calendar]")
{
    REQUIRE(CanReserveAuthoritativeAdvance(true, false, 7));
    REQUIRE_FALSE(CanReserveAuthoritativeAdvance(true, true, 7));
    REQUIRE_FALSE(CanReserveAuthoritativeAdvance(false, false, 7));
    REQUIRE_FALSE(CanReserveAuthoritativeAdvance(true, false, 0));

    REQUIRE(IsAuthoritativeReservationCurrent(true, 7, true, 7));
    REQUIRE_FALSE(IsAuthoritativeReservationCurrent(false, 7, true, 7));
    REQUIRE_FALSE(IsAuthoritativeReservationCurrent(true, 0, true, 7));
}

TEST_CASE("VR calendar reservation invalidates stale completion on epoch change", "[skyrim-vr][calendar]")
{
    REQUIRE(NextAuthoritativeTickEpoch(0) == 1);
    REQUIRE(NextAuthoritativeTickEpoch(7) == 8);
    REQUIRE(NextAuthoritativeTickEpoch(std::numeric_limits<std::uint64_t>::max()) == 0);

    REQUIRE_FALSE(IsAuthoritativeReservationCurrent(true, 7, false, 8));
    REQUIRE_FALSE(IsAuthoritativeReservationCurrent(true, 7, true, 8));
    REQUIRE(IsAuthoritativeReservationCurrent(true, 8, true, 8));
}

TEST_CASE("VR calendar snapshots preserve raw elapsed-day semantics", "[skyrim-vr][calendar]")
{
    REQUIRE(IsCalendarSnapshotInvariant(58.5F, 58.0F, 12.0F, 20.0F));
    REQUIRE_FALSE(IsCalendarSnapshotInvariant(423.5F, 58.0F, 12.0F, 20.0F));
    REQUIRE_FALSE(IsCalendarSnapshotInvariant(58.5F, 58.5F, 12.0F, 20.0F));
    REQUIRE_FALSE(IsCalendarSnapshotInvariant(58.5F, 58.0F, 24.0F, 20.0F));
}

TEST_CASE("VR authentication accepts only complete observed calendar globals", "[skyrim-vr][calendar]")
{
    Skyrim::Calendar::CalendarSnapshot valid{20.0F, 12.0F, 4.0F, 1.0F, 28.0F, 423.5F};
    REQUIRE(Skyrim::Calendar::IsCalendarSnapshotValid(valid));

    valid.Day = 29.0F;
    REQUIRE_FALSE(Skyrim::Calendar::IsCalendarSnapshotValid(valid));
    valid.Day = 28.0F;
    valid.Time = 24.0F;
    REQUIRE_FALSE(Skyrim::Calendar::IsCalendarSnapshotValid(valid));
    valid.Time = 12.0F;
    valid.GameDaysPassed = -1.0F;
    REQUIRE_FALSE(Skyrim::Calendar::IsCalendarSnapshotValid(valid));
}

TEST_CASE("VR calendar accepts engine date boundaries", "[skyrim-vr][calendar]")
{
    Skyrim::Calendar::CalendarSnapshot snapshot{20.0F, 12.0F, 999.0F, 0.0F, 31.0F, 12.5F};
    REQUIRE(Skyrim::Calendar::IsCalendarSnapshotValid(snapshot));

    snapshot.Day = 32.0F;
    REQUIRE_FALSE(Skyrim::Calendar::IsCalendarSnapshotValid(snapshot));
    snapshot.Month = 11.0F;
    snapshot.Day = 31.0F;
    REQUIRE(Skyrim::Calendar::IsCalendarSnapshotValid(snapshot));
    snapshot.Year = 1000.0F;
    REQUIRE_FALSE(Skyrim::Calendar::IsCalendarSnapshotValid(snapshot));
}

TEST_CASE("VR calendar defaults use Skyrim's zero-based January", "[skyrim-vr][calendar]")
{
    const TimeModel model{};
    REQUIRE(model.Year == 1);
    REQUIRE(model.Month == 0);
    REQUIRE(model.Day == 1);
}
