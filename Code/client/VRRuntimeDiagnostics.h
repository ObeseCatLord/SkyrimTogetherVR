#pragma once

#include <spdlog/spdlog.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

namespace SkyrimTogetherVR
{
inline void LogRuntimeCheckpoint(const char* apCheckpoint) noexcept
{
#if TP_SKYRIM_VR
    try
    {
        spdlog::info("SkyrimTogetherVR runtime checkpoint={}", apCheckpoint);
        if (auto* pLogger = spdlog::default_logger_raw())
            pLogger->flush();
    }
    catch (...)
    {
    }
#else
    (void)apCheckpoint;
#endif
}
} // namespace SkyrimTogetherVR
