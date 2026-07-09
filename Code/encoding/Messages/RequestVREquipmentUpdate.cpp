#include <Messages/RequestVREquipmentUpdate.h>

void RequestVREquipmentUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Equipment.Serialize(aWriter);
}

void RequestVREquipmentUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Equipment.Deserialize(aReader);
}
