#pragma once

#include "BridgeEndpoint.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
// Executes the CommonLib-only world-state and conservative VR interaction
// subset of ApplyGameplayAction. The command pump performs session/epoch and
// negotiated-capability checks before calling this executor.
//
// SetCalendar encodes year/month in ValueA/ValueB and day/hour/timescale in
// ScalarA/ScalarB/ScalarC. VrPoseChunk writes Pitch, PitchOffset, 1stPRot,
// and 1stPRotDamped from ScalarA through ScalarD; VrCalibration similarly
// writes TurnDelta, Direction, SpeedSampled, and SpeedDamped. These are the
// only accepted pose lanes until a VRIK remote-actor API is validated.
class VRInteractionManager final
{
public:
    [[nodiscard]] static CommandStatus Execute(const CommandRecord& a_command) noexcept;
    static void ProcessPeriodic() noexcept;
    static void Reset() noexcept;
};
} // namespace SkyrimTogetherVR::GameplayAdapter
