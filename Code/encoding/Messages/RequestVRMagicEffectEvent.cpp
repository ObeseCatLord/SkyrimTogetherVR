#include <Messages/RequestVRMagicEffectEvent.h>

void RequestVRMagicEffectEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    MagicEffect.Serialize(aWriter);
}

void RequestVRMagicEffectEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    MagicEffect.Deserialize(aReader);
}
