#pragma once

#include <Windows.h>

#include <atomic>

#include <PlayerCharacter.h>

#include <spdlog/spdlog.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

namespace SkyrimTogetherVR
{
inline bool IsReadablePlayerPage(const void* apAddress) noexcept
{
    MEMORY_BASIC_INFORMATION page{};
    if (!apAddress || VirtualQuery(apAddress, &page, sizeof(page)) != sizeof(page))
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
    case PAGE_EXECUTE_WRITECOPY: return true;
    default: return false;
    }
}

inline PlayerCharacter* TryGetReadablePlayerForVR() noexcept
{
    auto* pPlayer = PlayerCharacter::Get();

#if TP_SKYRIM_VR
    if (!IsReadablePlayerPage(pPlayer))
    {
        static std::atomic_flag s_reportedUnreadablePlayer = ATOMIC_FLAG_INIT;
        if (!s_reportedUnreadablePlayer.test_and_set(std::memory_order_relaxed))
            spdlog::warn("SkyrimTogetherVR player singleton is not readable yet; deferring player-derived work");
        return nullptr;
    }
#endif

    return pPlayer;
}
} // namespace SkyrimTogetherVR
