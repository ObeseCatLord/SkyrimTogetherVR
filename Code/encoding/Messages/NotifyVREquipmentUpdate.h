#pragma once

#include "Message.h"

#include <Structs/VREquipmentUpdate.h>

struct NotifyVREquipmentUpdate final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyVREquipmentUpdate;

    NotifyVREquipmentUpdate()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyVREquipmentUpdate& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Equipment == acRhs.Equipment;
    }

    uint32_t PlayerId{0};
    VREquipmentUpdate Equipment{};
};
