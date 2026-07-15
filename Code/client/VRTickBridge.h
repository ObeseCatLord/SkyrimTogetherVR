#pragma once

#include <cstdint>

namespace SkyrimTogetherVR::TickBridge
{
bool Initialize() noexcept;
void Activate() noexcept;
void Retire() noexcept;
std::uint32_t GetActivationThreadId() noexcept;
bool ConsumeUpdatePermit() noexcept;
std::uint32_t __cdecl Dispatch(std::uint64_t aEpoch, std::uint64_t aSequence, std::uint32_t aExecutorThreadId) noexcept;
std::uint32_t __cdecl CaptureBodyPose(std::uint64_t aEpoch, std::uint64_t aSequence, std::uint32_t aExecutorThreadId) noexcept;
} // namespace SkyrimTogetherVR::TickBridge
