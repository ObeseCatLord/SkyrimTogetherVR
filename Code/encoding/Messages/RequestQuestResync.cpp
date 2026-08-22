#include <Messages/RequestQuestResync.h>

void RequestQuestResync::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, OwnerPlayerId);
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteVarInt(aWriter, KnownRevision);
}

void RequestQuestResync::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    IsDecodedValid = true;
    ClientMessage::DeserializeRaw(aReader);
    OwnerPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RequestId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    KnownRevision = Serialization::ReadVarInt(aReader);
    IsDecodedValid = IsValid();
}
