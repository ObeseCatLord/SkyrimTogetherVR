#include <Messages/RequestVRCombatHitEvent.h>

void RequestVRCombatHitEvent::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Hit.Serialize(aWriter);
}

void RequestVRCombatHitEvent::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Hit.Deserialize(aReader);
}
