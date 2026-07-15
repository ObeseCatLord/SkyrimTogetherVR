#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace SkyrimTogetherVR
{
enum class MenuOpenState : std::uint8_t
{
    Unavailable,
    Closed,
    Open,
};

template <class TMap, class TReadable, class TReadFlags>
MenuOpenState
ProbeVrMenuOpen(const TMap& acMenuTable, const char* apName, std::size_t aReadableMenuSize, std::uint32_t aOnStackMask, TReadable&& aIsReadable, TReadFlags&& aReadFlags) noexcept
{
    if (!apName || aReadableMenuSize == 0 || aOnStackMask == 0)
        return MenuOpenState::Unavailable;

    using Entry = std::remove_pointer_t<decltype(acMenuTable.m_entries)>;
    constexpr std::uint32_t kMaximumMenuCapacity = 4096;

    const auto capacity = acMenuTable.m_size;
    if (capacity == 0 || capacity > kMaximumMenuCapacity || (capacity & (capacity - 1)) != 0 || acMenuTable.m_freeCount > capacity || acMenuTable.m_freeOffset > capacity ||
        !acMenuTable.m_entries)
    {
        return MenuOpenState::Unavailable;
    }

    if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(Entry))
        return MenuOpenState::Unavailable;

    if (!aIsReadable(acMenuTable.m_entries, static_cast<std::size_t>(capacity) * sizeof(Entry)))
        return MenuOpenState::Unavailable;

    for (std::uint32_t index = 0; index < capacity; ++index)
    {
        const auto& entry = acMenuTable.m_entries[index];
        if (entry.empty() || entry.key.data != apName)
            continue;

        auto* pMenu = entry.value.spMenu;
        if (!pMenu)
            return MenuOpenState::Closed;
        if (!aIsReadable(pMenu, aReadableMenuSize))
            return MenuOpenState::Unavailable;

        return (aReadFlags(pMenu) & aOnStackMask) != 0 ? MenuOpenState::Open : MenuOpenState::Closed;
    }

    return MenuOpenState::Closed;
}
} // namespace SkyrimTogetherVR
