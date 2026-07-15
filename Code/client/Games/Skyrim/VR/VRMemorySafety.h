#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace SkyrimTogetherVR
{
inline bool IsReadableVrMemory(const void* apAddress, std::size_t aSize = 1) noexcept
{
    if (!apAddress || aSize == 0)
        return false;

    const auto start = reinterpret_cast<std::uintptr_t>(apAddress);
    if (aSize - 1 > std::numeric_limits<std::uintptr_t>::max() - start)
        return false;

    const auto last = start + aSize - 1;
    auto current = start;
    while (current <= last)
    {
        MEMORY_BASIC_INFORMATION page{};
        if (VirtualQuery(reinterpret_cast<const void*>(current), &page, sizeof(page)) != sizeof(page))
            return false;

        if (page.State != MEM_COMMIT || (page.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;

        switch (page.Protect & 0xFFu)
        {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY: break;
        default: return false;
        }

        const auto pageBase = reinterpret_cast<std::uintptr_t>(page.BaseAddress);
        if (page.RegionSize > std::numeric_limits<std::uintptr_t>::max() - pageBase)
            return false;
        const auto pageEnd = pageBase + page.RegionSize;
        if (current < pageBase || current >= pageEnd)
            return false;
        if (last < pageEnd)
            return true;

        current = pageEnd;
    }

    return true;
}
} // namespace SkyrimTogetherVR
