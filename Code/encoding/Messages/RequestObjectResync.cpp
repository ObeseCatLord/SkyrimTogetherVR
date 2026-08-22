#include <Messages/RequestObjectResync.h>

void RequestObjectResync::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteVarInt(aWriter, KnownRevision);
}

void RequestObjectResync::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    IsDecodedValid = true;
    ClientMessage::DeserializeRaw(aReader);
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RequestId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    KnownRevision = Serialization::ReadVarInt(aReader);
    IsDecodedValid = IsValid();
}
