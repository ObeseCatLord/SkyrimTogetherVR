#include <Messages/NotifyActivate.h>

void NotifyActivate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, ActivatorId);
    aWriter.WriteBits(PreActivationOpenState, 8);
    Serialization::WriteBool(aWriter, HasPostActivationOpenState);
    aWriter.WriteBits(PostActivationOpenState, 8);
}

void NotifyActivate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    IsDecodedValid = true;
    ServerMessage::DeserializeRaw(aReader);

    Id.Deserialize(aReader);
    ActivatorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    uint64_t preActivationOpenState = 0;
    if (!aReader.ReadBits(preActivationOpenState, 8))
    {
        IsDecodedValid = false;
        return;
    }
    PreActivationOpenState = preActivationOpenState & 0xFF;
    HasPostActivationOpenState = Serialization::ReadBool(aReader);
    uint64_t postActivationOpenState{};
    if (!aReader.ReadBits(postActivationOpenState, 8))
    {
        IsDecodedValid = false;
        return;
    }
    PostActivationOpenState = postActivationOpenState & 0xFF;
    IsDecodedValid = IsValid();
}
