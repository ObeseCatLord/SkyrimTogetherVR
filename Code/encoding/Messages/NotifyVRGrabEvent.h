#pragma once

#include "Message.h"

#include <Structs/VRGrabEvent.h>

struct NotifyVRGrabEvent final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyVRGrabEvent;

    NotifyVRGrabEvent()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyVRGrabEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Grab == acRhs.Grab;
    }

    uint32_t PlayerId{0};
    VRGrabEvent Grab{};
};
