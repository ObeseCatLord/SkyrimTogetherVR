#include <Messages/RequestVRActivationEvent.h>

void RequestVRActivationEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Activation.Serialize(aWriter);
}

void RequestVRActivationEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Activation.Deserialize(aReader);
}
