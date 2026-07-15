#pragma once

#include "BridgeEndpoint.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
[[nodiscard]] CommandPumpResult ProcessCommands(
    std::uint32_t a_callerProcessId,
    std::uint32_t a_callerThreadId,
    std::uint64_t a_lifecycleEpoch,
    std::uint32_t a_maxCommands) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter
