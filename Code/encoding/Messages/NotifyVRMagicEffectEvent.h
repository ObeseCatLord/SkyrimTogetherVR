#pragma once

#include "Message.h"

#include <Structs/VRMagicEffectEvent.h>

struct NotifyVRMagicEffectEvent final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyVRMagicEffectEvent;

    NotifyVRMagicEffectEvent()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyVRMagicEffectEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && MagicEffect == acRhs.MagicEffect;
    }

    uint32_t PlayerId{0};
    VRMagicEffectEvent MagicEffect{};
};
