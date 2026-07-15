#pragma once

#include <spdlog/spdlog.h>

namespace SkyrimTogetherVR
{
inline void LogShutdownPhase(const char* apPhase) noexcept
{
    try
    {
        spdlog::info("SkyrimTogetherVR shutdown phase={}", apPhase);
        if (auto* pLogger = spdlog::default_logger_raw())
            pLogger->flush();
    }
    catch (...)
    {
    }
}
} // namespace SkyrimTogetherVR
