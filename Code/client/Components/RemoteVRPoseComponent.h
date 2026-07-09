#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/VRPoseUpdate.h>

struct RemoteVRPoseComponent
{
    RemoteVRPoseComponent(uint32_t aPlayerId, const VRPoseUpdate& acPose) noexcept
        : PlayerId(aPlayerId)
        , Pose(acPose)
    {
    }

    uint32_t PlayerId{};
    VRPoseUpdate Pose{};
};
