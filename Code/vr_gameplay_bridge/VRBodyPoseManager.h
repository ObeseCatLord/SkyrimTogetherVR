#pragma once

#include "BridgeEndpoint.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
// Applies the explicitly encoded remote-body pose subset of
// ApplyGameplayAction.  It deliberately has no dependency on VRIK's
// local-player API: the public VRIK interface has no remote-actor solver.
class VRBodyPoseManager final
{
public:
    [[nodiscard]] static CommandStatus Execute(const CommandRecord& a_command) noexcept;
    static void ProcessPending() noexcept;
    static void Reset() noexcept;
};
} // namespace SkyrimTogetherVR::GameplayAdapter
