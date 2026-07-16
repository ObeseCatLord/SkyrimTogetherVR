#include <Messages/NotifyOwnershipTransfer.h>

void NotifyOwnershipTransfer::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteVarInt(aWriter, GrantToken);
}

void NotifyOwnershipTransfer::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    GrantToken = Serialization::ReadVarInt(aReader);
}
