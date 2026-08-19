#pragma once

#include "CalendarSnapshot.h"
#include "Forms/TESGlobal.h"
#include "RuntimeLayout.h"
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TimeData
{
    using CommonLibCalendarOffsets = Skyrim::RuntimeLayout::CalendarCommonLibNgOffsets;
    using LocalTimeDataOffsets = Skyrim::RuntimeLayout::TimeDataLocalShimOffsets;

    static TimeData* Get() noexcept;

    using CalendarSnapshot = Skyrim::Calendar::CalendarSnapshot;

    [[nodiscard]] static bool IsCalendarSnapshotValid(const CalendarSnapshot& a_snapshot) noexcept
    {
        return Skyrim::Calendar::IsCalendarSnapshotValid(a_snapshot);
    }

    [[nodiscard]] TESGlobal* GetGameYearData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESGlobal*>(this, CommonLibCalendarOffsets::GameYear);
#else
        return GameYear;
#endif
    }

    [[nodiscard]] TESGlobal* GetGameMonthData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESGlobal*>(this, CommonLibCalendarOffsets::GameMonth);
#else
        return GameMonth;
#endif
    }

    [[nodiscard]] TESGlobal* GetGameDayData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESGlobal*>(this, CommonLibCalendarOffsets::GameDay);
#else
        return GameDay;
#endif
    }

    [[nodiscard]] TESGlobal* GetGameHourData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESGlobal*>(this, CommonLibCalendarOffsets::GameHour);
#else
        return GameHour;
#endif
    }

    [[nodiscard]] TESGlobal* GetGameDaysPassedData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESGlobal*>(this, CommonLibCalendarOffsets::GameDaysPassed);
#else
        return GameDaysPassed;
#endif
    }

    [[nodiscard]] TESGlobal* GetTimeScaleData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESGlobal*>(this, CommonLibCalendarOffsets::TimeScale);
#else
        return TimeScale;
#endif
    }

    [[nodiscard]] bool TryGetCalendarSnapshot(CalendarSnapshot& ar_snapshot) const noexcept
    {
        const auto* gameYear = GetGameYearData();
        const auto* gameMonth = GetGameMonthData();
        const auto* gameDay = GetGameDayData();
        const auto* gameHour = GetGameHourData();
        const auto* gameDaysPassed = GetGameDaysPassedData();
        const auto* timeScale = GetTimeScaleData();
        if (!gameYear || !gameMonth || !gameDay || !gameHour || !gameDaysPassed || !timeScale)
            return false;

        CalendarSnapshot candidate{
            timeScale->GetValueData(), gameHour->GetValueData(), gameYear->GetValueData(),
            gameMonth->GetValueData(), gameDay->GetValueData(), gameDaysPassed->GetValueData()};
        if (!IsCalendarSnapshotValid(candidate))
            return false;

        ar_snapshot = candidate;
        return true;
    }

    char pad0[0x8];            // 0x0000
    TESGlobal* GameYear;       // 0x0008
    TESGlobal* GameMonth;      // 0x0010
    TESGlobal* GameDay;        // 0x0018
    TESGlobal* GameHour;       // 0x0020
    TESGlobal* GameDaysPassed; // 0x0028
    TESGlobal* TimeScale;      // 0x0030
    std::uint32_t midnightsPassed; // 0x0038
    float rawDaysPassed;       // 0x003C
    char pad_40[0x88];         // 0x0040
};

static_assert(TimeData::CommonLibCalendarOffsets::GameYear == 0x8);
static_assert(TimeData::CommonLibCalendarOffsets::GameMonth == 0x10);
static_assert(TimeData::CommonLibCalendarOffsets::GameDay == 0x18);
static_assert(TimeData::CommonLibCalendarOffsets::GameHour == 0x20);
static_assert(TimeData::CommonLibCalendarOffsets::GameDaysPassed == 0x28);
static_assert(TimeData::CommonLibCalendarOffsets::TimeScale == 0x30);
static_assert(TimeData::CommonLibCalendarOffsets::RawDaysPassed == 0x3C);
static_assert(TimeData::CommonLibCalendarOffsets::Size == 0x40);
static_assert(offsetof(TimeData, GameYear) == TimeData::LocalTimeDataOffsets::GameYear);
static_assert(offsetof(TimeData, GameMonth) == TimeData::LocalTimeDataOffsets::GameMonth);
static_assert(offsetof(TimeData, GameDay) == TimeData::LocalTimeDataOffsets::GameDay);
static_assert(offsetof(TimeData, GameHour) == TimeData::LocalTimeDataOffsets::GameHour);
static_assert(offsetof(TimeData, GameDaysPassed) == TimeData::LocalTimeDataOffsets::GameDaysPassed);
static_assert(offsetof(TimeData, TimeScale) == TimeData::LocalTimeDataOffsets::TimeScale);
static_assert(offsetof(TimeData, midnightsPassed) == TimeData::LocalTimeDataOffsets::MidnightsPassed);
static_assert(offsetof(TimeData, rawDaysPassed) == TimeData::LocalTimeDataOffsets::RawDaysPassed);
static_assert(sizeof(TimeData) == TimeData::LocalTimeDataOffsets::Size);
