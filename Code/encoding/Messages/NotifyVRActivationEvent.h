#pragma once

#include "Message.h"

#include <Structs/VRActivationEvent.h>

struct NotifyVRActivationEvent final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyVRActivationEvent;

    NotifyVRActivationEvent()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyVRActivationEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Activation == acRhs.Activation;
    }

    uint32_t PlayerId{0};
    VRActivationEvent Activation{};
};
