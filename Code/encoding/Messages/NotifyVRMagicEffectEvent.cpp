#include <Messages/NotifyVRMagicEffectEvent.h>

#include <TiltedCore/Serialization.hpp>

void NotifyVRMagicEffectEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, PlayerId);
    MagicEffect.Serialize(aWriter);
}

void NotifyVRMagicEffectEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    MagicEffect.Deserialize(aReader);
}
