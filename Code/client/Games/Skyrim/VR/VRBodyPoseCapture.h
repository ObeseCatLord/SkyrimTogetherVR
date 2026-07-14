#pragma once

#include <cstdint>

#include <Structs/VRPoseUpdate.h>

namespace SkyrimTogetherVR::BodyPoseCapture
{
bool Activate() noexcept;
void Retire() noexcept;
bool CaptureFromPostHiggs(std::uint64_t aCallbackSequence) noexcept;
VRBodyPoseData CopyLatestFresh(std::uint64_t aMaxAgeMilliseconds = 250) noexcept;
} // namespace SkyrimTogetherVR::BodyPoseCapture
