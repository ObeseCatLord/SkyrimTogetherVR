#include <Messages/RequestVRPlanckPhysicsEvent.h>

void RequestVRPlanckPhysicsEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Event.Serialize(aWriter);
}

void RequestVRPlanckPhysicsEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    Event.Deserialize(aReader);
}
