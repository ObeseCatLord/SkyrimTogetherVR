#pragma once

#include "Message.h"

#include <Structs/VRAppearance.h>

struct RequestVRAppearance final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestVRAppearance;

    RequestVRAppearance()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestVRAppearance& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Appearance == acRhs.Appearance;
    }

    VRAppearance Appearance{};
};
