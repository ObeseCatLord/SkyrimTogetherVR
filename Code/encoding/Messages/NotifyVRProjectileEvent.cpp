#include <Messages/NotifyVRProjectileEvent.h>

#include <TiltedCore/Serialization.hpp>

void NotifyVRProjectileEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, PlayerId);
    Projectile.Serialize(aWriter);
}

void NotifyVRProjectileEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Projectile.Deserialize(aReader);
}
