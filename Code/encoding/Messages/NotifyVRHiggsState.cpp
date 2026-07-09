#include <Messages/NotifyVRHiggsState.h>

#include <TiltedCore/Serialization.hpp>

void NotifyVRHiggsState::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, PlayerId);
    State.Serialize(aWriter);
}

void NotifyVRHiggsState::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    State.Deserialize(aReader);
}
