#pragma once

#include "Message.h"

#include <Structs/VRPlanckPhysicsEvent.h>

struct NotifyVRPlanckPhysicsEvent final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyVRPlanckPhysicsEvent;

    NotifyVRPlanckPhysicsEvent()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyVRPlanckPhysicsEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Event == acRhs.Event;
    }

    uint32_t PlayerId{0};
    VRPlanckPhysicsEvent Event{};
};
