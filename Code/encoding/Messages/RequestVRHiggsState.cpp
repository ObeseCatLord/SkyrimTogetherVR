#include <Messages/RequestVRHiggsState.h>

void RequestVRHiggsState::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    State.Serialize(aWriter);
}

void RequestVRHiggsState::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    State.Deserialize(aReader);
}
