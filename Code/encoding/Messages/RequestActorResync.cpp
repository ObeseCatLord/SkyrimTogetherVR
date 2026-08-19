#include <Messages/RequestActorResync.h>

void RequestActorResync::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteVarInt(aWriter, KnownActorRevision);
    Serialization::WriteVarInt(aWriter, KnownEquipmentRevision);
    Serialization::WriteVarInt(aWriter, Scope);
}

void RequestActorResync::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    IsDecodedValid = true;
    ClientMessage::DeserializeRaw(aReader);
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RequestId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    KnownActorRevision = Serialization::ReadVarInt(aReader);
    KnownEquipmentRevision = Serialization::ReadVarInt(aReader);
    Scope = static_cast<std::uint8_t>(Serialization::ReadVarInt(aReader) & 0xFF);
    IsDecodedValid = IsValid();
}
