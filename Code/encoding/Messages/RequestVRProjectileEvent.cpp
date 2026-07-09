#include <Messages/RequestVRProjectileEvent.h>

void RequestVRProjectileEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Projectile.Serialize(aWriter);
}

void RequestVRProjectileEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Projectile.Deserialize(aReader);
}
