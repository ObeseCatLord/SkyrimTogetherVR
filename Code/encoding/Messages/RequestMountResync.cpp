#include <Messages/RequestMountResync.h>

void RequestMountResync::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, RiderId);
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteVarInt(aWriter, KnownRevision);
}

void RequestMountResync::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    IsDecodedValid = true;
    ClientMessage::DeserializeRaw(aReader);
    RiderId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RequestId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    KnownRevision = Serialization::ReadVarInt(aReader);
    IsDecodedValid = IsValid();
}
