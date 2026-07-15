#pragma once

#include <atomic>

#include <PlayerCharacter.h>
#include <VR/VRMemorySafety.h>

#include <spdlog/spdlog.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

namespace SkyrimTogetherVR
{
inline PlayerCharacter* TryGetReadablePlayerForVR() noexcept
{
    auto* pPlayer = PlayerCharacter::Get();

#if TP_SKYRIM_VR
    if (!IsReadableVrMemory(pPlayer, sizeof(void*)))
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
