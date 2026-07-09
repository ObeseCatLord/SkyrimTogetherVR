#pragma once

#include "Message.h"

#include <Structs/VRActivationEvent.h>

struct RequestVRActivationEvent final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestVRActivationEvent;

    RequestVRActivationEvent()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestVRActivationEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Activation == acRhs.Activation;
    }

    VRActivationEvent Activation{};
};
