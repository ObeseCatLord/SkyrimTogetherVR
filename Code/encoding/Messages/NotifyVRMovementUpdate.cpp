#include <Messages/NotifyVRMovementUpdate.h>

#include <TiltedCore/Serialization.hpp>

void NotifyVRMovementUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, PlayerId);
    Movement.Serialize(aWriter);
}

void NotifyVRMovementUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Movement.Deserialize(aReader);
}
