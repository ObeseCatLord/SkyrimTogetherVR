#pragma once

#include <cstdint>

namespace SkyrimTogether::Protocol
{
[[nodiscard]] constexpr bool IsNewerMovementTick(
    const bool a_hasAcceptedTick,
    const std::uint64_t a_lastAcceptedTick,
    const std::uint64_t a_incomingTick) noexcept
{
    if (!a_hasAcceptedTick)
        return true;

    const auto delta = a_incomingTick - a_lastAcceptedTick;
    return delta != 0 && delta < (std::uint64_t{1} << 63);
}
} // namespace SkyrimTogether::Protocol
