#pragma once

#include <cstddef>
#include <cstdint>

namespace launcher
{
struct RuntimeVersion
{
    uint16_t Major{};
    uint16_t Minor{};
    uint16_t Revision{};
    uint16_t Build{};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return Major != 0 || Minor != 0 || Revision != 0 || Build != 0; }
    [[nodiscard]] constexpr bool IsSupported() const noexcept { return Major == 1 && Minor == 4 && Revision == 15 && Build == 0; }
    [[nodiscard]] constexpr bool Equals(const RuntimeVersion& acOther) const noexcept
    {
        return Major == acOther.Major && Minor == acOther.Minor && Revision == acOther.Revision && Build == acOther.Build;
    }
};

inline bool ParseRuntimeVersionString(const wchar_t* acpValue, size_t aLength, RuntimeVersion& aVersion) noexcept
{
    aVersion = {};
    constexpr size_t kMaximumVersionLength = 24;
    if (!acpValue || aLength < 8 || aLength > kMaximumVersionLength || acpValue[aLength - 1] != L'\0')
        return false;

    uint16_t parts[4]{};
    size_t position = 0;
    for (size_t partIndex = 0; partIndex < 4; ++partIndex)
    {
        if (position >= aLength - 1 || acpValue[position] < L'0' || acpValue[position] > L'9')
            return false;

        uint32_t value = 0;
        do
        {
            value = value * 10 + static_cast<uint32_t>(acpValue[position] - L'0');
            if (value > UINT16_MAX)
                return false;
            ++position;
        } while (position < aLength - 1 && acpValue[position] >= L'0' && acpValue[position] <= L'9');

        parts[partIndex] = static_cast<uint16_t>(value);
        if (partIndex != 3)
        {
            if (position >= aLength - 1 || acpValue[position] != L'.')
                return false;
            ++position;
        }
    }

    if (position != aLength - 1)
        return false;

    const RuntimeVersion parsed{parts[0], parts[1], parts[2], parts[3]};
    if (!parsed.IsValid())
        return false;

    aVersion = parsed;
    return true;
}
} // namespace launcher
