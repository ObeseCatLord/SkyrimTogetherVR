#pragma once

#include "Message.h"

#include <Structs/VRCombatHitEvent.h>

struct NotifyVRCombatHitEvent final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyVRCombatHitEvent;

    NotifyVRCombatHitEvent()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyVRCombatHitEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Hit == acRhs.Hit;
    }

    uint32_t PlayerId{0};
    VRCombatHitEvent Hit{};
};
