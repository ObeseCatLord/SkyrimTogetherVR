#include <Messages/ClientActorActionRequest.h>
#include <TiltedCore/Serialization.hpp>

void ClientActorActionRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Action.GenerateDifferential(ActionEvent{}, aWriter);
}

void ClientActorActionRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Action = ActionEvent{};
    Action.ApplyDifferential(aReader);
}
