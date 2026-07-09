#pragma once

#include "Message.h"

#include <Structs/VRMagicEffectEvent.h>

struct RequestVRMagicEffectEvent final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestVRMagicEffectEvent;

    RequestVRMagicEffectEvent()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestVRMagicEffectEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && MagicEffect == acRhs.MagicEffect;
    }

    VRMagicEffectEvent MagicEffect{};
};
