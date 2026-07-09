#include <Messages/NotifyVRCombatHitEvent.h>

#include <TiltedCore/Serialization.hpp>

void NotifyVRCombatHitEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, PlayerId);
    Hit.Serialize(aWriter);
}

void NotifyVRCombatHitEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Hit.Deserialize(aReader);
}
