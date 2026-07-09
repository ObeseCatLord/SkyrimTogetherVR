#include <Messages/RequestVRGrabEvent.h>

void RequestVRGrabEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Grab.Serialize(aWriter);
}

void RequestVRGrabEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Grab.Deserialize(aReader);
}
