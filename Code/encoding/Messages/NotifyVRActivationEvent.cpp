#include <Messages/NotifyVRActivationEvent.h>

#include <TiltedCore/Serialization.hpp>

void NotifyVRActivationEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, PlayerId);
    Activation.Serialize(aWriter);
}

void NotifyVRActivationEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Activation.Deserialize(aReader);
}
