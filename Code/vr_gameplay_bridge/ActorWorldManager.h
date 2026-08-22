#pragma once

#include "AvatarManager.h"

#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace OpenStatePolicy
{
inline constexpr std::uint8_t kNone = 0;
inline constexpr std::uint8_t kOpen = 1;
inline constexpr std::uint8_t kOpening = 2;
inline constexpr std::uint8_t kClosed = 3;
inline constexpr std::uint8_t kClosing = 4;
inline constexpr std::uint8_t kMaximumObservedOpenState = kClosing;

[[nodiscard]] constexpr bool IsObservedOpenState(const std::uint8_t a_state) noexcept
{
    return a_state <= kMaximumObservedOpenState;
}

[[nodiscard]] constexpr bool IsApplicableAuthoritativeOpenState(const std::uint8_t a_state) noexcept
{
    return a_state != kNone && IsObservedOpenState(a_state);
}

[[nodiscard]] constexpr bool IsOpenDirection(const std::uint8_t a_state) noexcept
{
    return a_state == kOpen || a_state == kOpening;
}

[[nodiscard]] constexpr bool IsStableResult(const std::uint8_t a_state, const bool a_open) noexcept
{
    return a_state == (a_open ? kOpen : kClosed);
}

[[nodiscard]] constexpr bool RequiresNativeSet(const std::uint8_t a_observed, const bool a_open) noexcept
{
    return !IsStableResult(a_observed, a_open);
}
} // namespace OpenStatePolicy

class ActorWorldManager final
{
public:
    [[nodiscard]] static CommandStatus Execute(const CommandRecord& a_command) noexcept;
    static void ProcessPeriodic() noexcept;
    static void Reset() noexcept;
};
} // namespace SkyrimTogetherVR::GameplayAdapter
