#pragma once

#include "Message.h"

#include <Structs/VRMovementUpdate.h>

struct NotifyVRMovementUpdate final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyVRMovementUpdate;

    NotifyVRMovementUpdate()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyVRMovementUpdate& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Movement == acRhs.Movement;
    }

    uint32_t PlayerId{0};
    VRMovementUpdate Movement{};
};
