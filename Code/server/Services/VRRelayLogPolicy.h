#pragma once

#include <cstdint>
#include <limits>

namespace VRRelayLogPolicy
{
inline bool RecordNoRoutableCharacter(std::uint64_t& arCount) noexcept
{
    if (arCount == std::numeric_limits<std::uint64_t>::max())
        return false;

    ++arCount;
    return (arCount & (arCount - 1)) == 0;
}
} // namespace VRRelayLogPolicy
