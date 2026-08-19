#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Skyrim::Calendar
{
struct CalendarSnapshot
{
    float TimeScale{};
    float Time{};
    float Year{};
    float Month{};
    float Day{};
    float GameDaysPassed{};
};

[[nodiscard]] inline bool IsCalendarSnapshotValid(const CalendarSnapshot& a_snapshot) noexcept
{
    constexpr std::uint8_t kDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (!std::isfinite(a_snapshot.TimeScale) || !std::isfinite(a_snapshot.Time) ||
        !std::isfinite(a_snapshot.Year) || !std::isfinite(a_snapshot.Month) ||
        !std::isfinite(a_snapshot.Day) || !std::isfinite(a_snapshot.GameDaysPassed) ||
        a_snapshot.TimeScale < 0.0F || a_snapshot.TimeScale > 1000.0F ||
        a_snapshot.Time < 0.0F || a_snapshot.Time >= 24.0F ||
        a_snapshot.Year < 0.0F || a_snapshot.Year > 999.0F ||
        a_snapshot.Month < 0.0F || a_snapshot.Month >= 12.0F ||
        std::floor(a_snapshot.Year) != a_snapshot.Year || std::floor(a_snapshot.Month) != a_snapshot.Month ||
        std::floor(a_snapshot.Day) != a_snapshot.Day || a_snapshot.GameDaysPassed < 0.0F)
        return false;

    const auto month = static_cast<std::size_t>(a_snapshot.Month);
    return a_snapshot.Day >= 1.0F && a_snapshot.Day <= static_cast<float>(kDaysInMonth[month]);
}
}
