#pragma once

#include "Message.h"

#include <Structs/VRProjectileEvent.h>

struct NotifyVRProjectileEvent final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyVRProjectileEvent;

    NotifyVRProjectileEvent()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyVRProjectileEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Projectile == acRhs.Projectile;
    }

    uint32_t PlayerId{0};
    VRProjectileEvent Projectile{};
};
