#pragma once

#include "Message.h"

#include <Structs/VRPoseUpdate.h>

struct NotifyVRPoseUpdate final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyVRPoseUpdate;

    NotifyVRPoseUpdate()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyVRPoseUpdate& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Pose == acRhs.Pose;
    }

    uint32_t PlayerId{0};
    VRPoseUpdate Pose{};
};
