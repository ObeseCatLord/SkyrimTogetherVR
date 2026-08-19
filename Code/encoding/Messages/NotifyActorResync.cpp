#include <Messages/NotifyActorResync.h>

void NotifyActorResync::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteVarInt(aWriter, CanonicalRevision);
    Snapshot.SerializeRaw(aWriter);
}

void NotifyActorResync::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    IsDecodedValid = true;
    ServerMessage::DeserializeRaw(aReader);
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RequestId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CanonicalRevision = Serialization::ReadVarInt(aReader);
    Snapshot = {};
    Snapshot.DeserializeRaw(aReader);
    IsDecodedValid = IsValid();
}
