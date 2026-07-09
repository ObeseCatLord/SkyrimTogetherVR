#pragma once

#include "Message.h"

#include <Structs/VREquipmentUpdate.h>

struct RequestVREquipmentUpdate final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestVREquipmentUpdate;

    RequestVREquipmentUpdate()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestVREquipmentUpdate& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Equipment == acRhs.Equipment;
    }

    VREquipmentUpdate Equipment{};
};
