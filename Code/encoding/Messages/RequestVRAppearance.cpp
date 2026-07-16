#include <Messages/RequestVRAppearance.h>

void RequestVRAppearance::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Appearance.Serialize(aWriter);
}

void RequestVRAppearance::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    Appearance.Deserialize(aReader);
}
