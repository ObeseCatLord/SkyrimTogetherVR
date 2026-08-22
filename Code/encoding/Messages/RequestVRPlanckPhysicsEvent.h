#pragma once

#include "Message.h"

#include <Structs/VRPlanckPhysicsEvent.h>

struct RequestVRPlanckPhysicsEvent final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestVRPlanckPhysicsEvent;

    RequestVRPlanckPhysicsEvent()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestVRPlanckPhysicsEvent& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Event == acRhs.Event;
    }

    VRPlanckPhysicsEvent Event{};
};
