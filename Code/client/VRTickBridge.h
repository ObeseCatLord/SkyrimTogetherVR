#pragma once

#include <cstdint>

namespace SkyrimTogetherVR::TickBridge
{
bool Initialize() noexcept;
void Activate() noexcept;
void Retire() noexcept;
std::uint32_t __cdecl Dispatch(std::uint64_t aEpoch) noexcept;
} // namespace SkyrimTogetherVR::TickBridge
