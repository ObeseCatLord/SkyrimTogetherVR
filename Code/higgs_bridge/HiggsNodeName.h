#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace SkyrimTogetherVR::HiggsBridge
{
[[nodiscard]] constexpr bool IsLineSafeNodeNameByte(const unsigned char aByte) noexcept
{
    // Retain printable ASCII and UTF-8 bytes while excluding C0 controls and DEL.
    return aByte >= 0x20 && aByte != 0x7F;
}

// The handoff is line-oriented, so reserve the final byte for a terminator and
// reject a source name that cannot be represented without control bytes.
template <std::size_t N>
[[nodiscard]] bool CopyLineSafeNodeName(const char* apSource, char (&arDestination)[N],
                                        std::uint8_t& arLength) noexcept
{
    static_assert(N > 1);
    static_assert(N - 1 <= std::numeric_limits<std::uint8_t>::max());

    arDestination[0] = '\0';
    arLength = 0;
    if (!apSource)
        return false;

    for (std::size_t index = 0; index < N; ++index)
    {
        const auto byte = static_cast<unsigned char>(apSource[index]);
        if (byte == '\0')
        {
            arDestination[index] = '\0';
            arLength = static_cast<std::uint8_t>(index);
            return true;
        }
        if (index == N - 1 || !IsLineSafeNodeNameByte(byte))
        {
            arDestination[0] = '\0';
            return false;
        }
        arDestination[index] = static_cast<char>(byte);
    }

    return false;
}
} // namespace SkyrimTogetherVR::HiggsBridge
