#include <Messages/NotifyMountResync.h>

void NotifyMountResync::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, RiderId);
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteVarInt(aWriter, CanonicalRevision);
    Serialization::WriteVarInt(aWriter, MountId);
}

void NotifyMountResync::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    IsDecodedValid = true;
    ServerMessage::DeserializeRaw(aReader);
    RiderId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RequestId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CanonicalRevision = Serialization::ReadVarInt(aReader);
    MountId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    IsDecodedValid = IsValid();
}
