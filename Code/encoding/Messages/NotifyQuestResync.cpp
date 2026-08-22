#include <Messages/NotifyQuestResync.h>

void NotifyQuestResync::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, OwnerPlayerId);
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteVarInt(aWriter, CanonicalRevision);
    Serialization::WriteBool(aWriter, HasParty);
    Serialization::WriteVarInt(aWriter, PartyId);
    Snapshot.Serialize(aWriter);
}

void NotifyQuestResync::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    IsDecodedValid = true;
    ServerMessage::DeserializeRaw(aReader);
    OwnerPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RequestId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CanonicalRevision = Serialization::ReadVarInt(aReader);
    HasParty = Serialization::ReadBool(aReader);
    PartyId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Snapshot = {};
    Snapshot.Deserialize(aReader);
    if (!HasParty && PartyId != 0)
    {
        IsDecodedValid = false;
        return;
    }
    IsDecodedValid = IsValid();
}
