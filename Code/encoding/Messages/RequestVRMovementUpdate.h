#pragma once

#include "Message.h"

#include <Structs/VRMovementUpdate.h>

struct RequestVRMovementUpdate final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestVRMovementUpdate;

    RequestVRMovementUpdate()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestVRMovementUpdate& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Movement == acRhs.Movement;
    }

    VRMovementUpdate Movement{};
};
