#pragma once

#include "Message.h"

#include <Structs/VRProjectileEvent.h>

struct RequestVRProjectileEvent final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestVRProjectileEvent;

    RequestVRProjectileEvent()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestVRProjectileEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Projectile == acRhs.Projectile;
    }

    VRProjectileEvent Projectile{};
};
