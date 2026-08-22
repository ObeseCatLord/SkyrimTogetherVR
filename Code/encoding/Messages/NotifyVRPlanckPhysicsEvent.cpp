#include <Messages/NotifyVRPlanckPhysicsEvent.h>

#include <TiltedCore/Serialization.hpp>

void NotifyVRPlanckPhysicsEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, PlayerId);
    Event.Serialize(aWriter);
}

void NotifyVRPlanckPhysicsEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    PlayerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu;
    Event.Deserialize(aReader);
}
