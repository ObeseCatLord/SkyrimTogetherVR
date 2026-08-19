#pragma once

#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::FaderRecovery
{
// SKSE lifecycle delivery can precede the command-pump owner. This records an
// invalidation only; ProcessOnCommandPumpOwner performs all game/UI work.
void NotifyLifecycleTransition() noexcept;

// Call only after BridgeEndpoint::ValidateCommandPump has accepted the caller.
void ProcessOnCommandPumpOwner(std::uint64_t aServerInstanceNonce, std::uint64_t aConnectionGeneration) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::FaderRecovery
