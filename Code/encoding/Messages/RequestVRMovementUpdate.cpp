#include <Messages/RequestVRMovementUpdate.h>

void RequestVRMovementUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Movement.Serialize(aWriter);
}

void RequestVRMovementUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Movement.Deserialize(aReader);
}
