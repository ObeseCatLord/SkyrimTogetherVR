#pragma once

#include "Message.h"

#include <Structs/VRGrabEvent.h>

struct RequestVRGrabEvent final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestVRGrabEvent;

    RequestVRGrabEvent()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestVRGrabEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Grab == acRhs.Grab;
    }

    VRGrabEvent Grab{};
};
