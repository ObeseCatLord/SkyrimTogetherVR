#pragma once

#include "Message.h"

#include <Structs/VRPoseUpdate.h>

struct RequestVRPoseUpdate final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestVRPoseUpdate;

    RequestVRPoseUpdate()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestVRPoseUpdate& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Pose == acRhs.Pose;
    }

    VRPoseUpdate Pose{};
};
