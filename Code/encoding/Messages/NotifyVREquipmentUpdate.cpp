#include <Messages/NotifyVREquipmentUpdate.h>

#include <TiltedCore/Serialization.hpp>

void NotifyVREquipmentUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, PlayerId);
    Equipment.Serialize(aWriter);
}

void NotifyVREquipmentUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Equipment.Deserialize(aReader);
}
