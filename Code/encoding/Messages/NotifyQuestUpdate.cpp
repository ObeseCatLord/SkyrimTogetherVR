
#include <Messages/NotifyQuestUpdate.h>
#include <TiltedCore/Serialization.hpp>

void NotifyQuestUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, OwnerPlayerId);
    Serialization::WriteVarInt(aWriter, CanonicalRevision);
    Id.Serialize(aWriter);
    aWriter.WriteBits(Stage, 16);
    aWriter.WriteBits(Status, 8);
    aWriter.WriteBits(ClientQuestType, 8);
}

void NotifyQuestUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    OwnerPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CanonicalRevision = Serialization::ReadVarInt(aReader);
    Id.Deserialize(aReader);

    uint64_t tmp;
    aReader.ReadBits(tmp, 16);
    Stage = tmp & 0xFFFF;

    aReader.ReadBits(tmp, 8);
    Status = tmp & 0xFF;

    aReader.ReadBits(tmp, 8);
    ClientQuestType = tmp & 0xFF;
}
