#include <Messages/NotifyVRGrabEvent.h>

#include <TiltedCore/Serialization.hpp>

void NotifyVRGrabEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, PlayerId);
    Grab.Serialize(aWriter);
}

void NotifyVRGrabEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Grab.Deserialize(aReader);
}
