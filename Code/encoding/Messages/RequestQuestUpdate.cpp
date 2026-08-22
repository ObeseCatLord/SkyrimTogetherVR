
#include <Messages/RequestQuestUpdate.h>
#include <TiltedCore/Serialization.hpp>

void RequestQuestUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, OwnerPlayerId);
    Id.Serialize(aWriter);
    aWriter.WriteBits(Stage, 16);
    aWriter.WriteBits(Status, 8);
    aWriter.WriteBits(ClientQuestType, 8);
}

void RequestQuestUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    OwnerPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Id.Deserialize(aReader);

    uint64_t tmp;
    aReader.ReadBits(tmp, 16);
    Stage = tmp & 0xFFFF;

    aReader.ReadBits(tmp, 8);
    Status = tmp & 0xFF;

    aReader.ReadBits(tmp, 8);
    ClientQuestType = tmp & 0xFF;
}
