#include <Messages/NotifyVRAppearance.h>

#include <TiltedCore/Serialization.hpp>

void NotifyVRAppearance::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, PlayerId);
    Appearance.Serialize(aWriter);
}

void NotifyVRAppearance::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    PlayerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Appearance.Deserialize(aReader);
}
