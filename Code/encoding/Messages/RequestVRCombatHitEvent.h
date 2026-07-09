#pragma once

#include "Message.h"

#include <Structs/VRCombatHitEvent.h>

struct RequestVRCombatHitEvent final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestVRCombatHitEvent;

    RequestVRCombatHitEvent()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestVRCombatHitEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Hit == acRhs.Hit;
    }

    VRCombatHitEvent Hit{};
};
