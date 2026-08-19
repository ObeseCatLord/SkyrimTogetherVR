#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace SkyrimTogetherVR::MenuPausePolicy
{
inline constexpr std::uint32_t kPausesGame = 0x1;
inline constexpr std::uint32_t kFreezeFrameBackground = 0x20;
inline constexpr std::uint32_t kFreezeFramePause = 0x1000000;
inline constexpr std::uint32_t kClearedFlags = kPausesGame | kFreezeFrameBackground | kFreezeFramePause;

inline constexpr std::array<std::string_view, 9> kAllowList = {
    "TweenMenu", "MagicMenu", "StatsMenu", "InventoryMenu", "MessageBoxMenu", "ContainerMenu", "FavoritesMenu", "Tutorial Menu", "Console",
};

enum class Action : std::uint8_t
{
    None,
    Unpause,
    Restore,
};

[[nodiscard]] constexpr bool IsAllowlisted(const std::string_view aMenuName) noexcept
{
    for (const auto name : kAllowList)
    {
        if (name == aMenuName)
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool ShouldUnpause(const std::string_view aMenuName, const bool aClientOnline, const bool aSkyrimSoulsActive, const bool aRuntimeApiAvailable) noexcept
{
    return aClientOnline && !aSkyrimSoulsActive && aRuntimeApiAvailable && IsAllowlisted(aMenuName);
}

[[nodiscard]] constexpr Action DecideAction(
    const std::string_view aMenuName, const bool aClientOnline, const bool aSkyrimSoulsActive, const bool aRuntimeApiAvailable, const bool aMenuOnStack,
    const bool aPreviouslyModified) noexcept
{
    if (aMenuOnStack || aSkyrimSoulsActive || !aRuntimeApiAvailable || !IsAllowlisted(aMenuName))
        return Action::None;
    if (aClientOnline)
        return Action::Unpause;
    return aPreviouslyModified ? Action::Restore : Action::None;
}

[[nodiscard]] constexpr std::uint32_t UnpausedFlags(const std::uint32_t aFlags) noexcept
{
    return aFlags & ~kClearedFlags;
}

[[nodiscard]] constexpr std::uint32_t RestoredFlags(const std::uint32_t aFlags, const std::uint32_t aOriginalManagedFlags) noexcept
{
    return (aFlags & ~kClearedFlags) | (aOriginalManagedFlags & kClearedFlags);
}
} // namespace SkyrimTogetherVR::MenuPausePolicy
