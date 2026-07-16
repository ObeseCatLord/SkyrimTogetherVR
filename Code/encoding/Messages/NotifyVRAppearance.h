#pragma once

#include "Message.h"

#include <Structs/VRAppearance.h>

struct NotifyVRAppearance final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyVRAppearance;

    NotifyVRAppearance()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyVRAppearance& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Appearance == acRhs.Appearance;
    }

    std::uint32_t PlayerId{0};
    VRAppearance Appearance{};
};
