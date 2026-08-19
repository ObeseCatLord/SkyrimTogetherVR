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

enum class MutationContext : std::uint8_t
{
    Creator,
    PeriodicScan,
};

// Transport callbacks publish exact connection edges for creator threads;
// the owner-thread UI scan also refreshes the same atomic snapshot.
void PublishTransportConnectionState(bool aConnected) noexcept;

// Creator wrappers run before Skyrim places the new menu on its active queue,
// so they may mutate an off-stack menu even when they are not on TickBridge's
// owner thread. Periodic scans must remain on that owner thread.
[[nodiscard]] constexpr bool CanMutateFlags(
    const MutationContext aContext, const bool aIsVrUiOwnerThread, const bool aMenuOnStack) noexcept
{
    return !aMenuOnStack && (aContext == MutationContext::Creator || aIsVrUiOwnerThread);
}

[[nodiscard]] constexpr bool IsAllowlisted(const std::string_view aMenuName) noexcept
{
    for (const auto name : kAllowList)
    {
        if (name == aMenuName)
            return true;
    }
    return false;
}

// Match the desktop queue hook: authentication is not enough.  The underlying
// transport must be connected so opening a menu cannot stall the handshake.
[[nodiscard]] constexpr bool ShouldUnpause(
    const std::string_view aMenuName, const bool aTransportConnected, const bool aSkyrimSoulsActive, const bool aRuntimeApiAvailable) noexcept
{
    return aTransportConnected && !aSkyrimSoulsActive && aRuntimeApiAvailable && IsAllowlisted(aMenuName);
}

[[nodiscard]] constexpr Action DecideAction(
    const std::string_view aMenuName, const bool aTransportConnected, const bool aSkyrimSoulsActive, const bool aRuntimeApiAvailable, const bool aMenuOnStack,
    const bool aPreviouslyModified) noexcept
{
    if (aMenuOnStack || aSkyrimSoulsActive || !aRuntimeApiAvailable || !IsAllowlisted(aMenuName))
        return Action::None;
    if (aTransportConnected)
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
