#include <Messages/AuthenticationResponse.h>

void AuthenticationResponse::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, static_cast<uint32_t>(Type));
    Serialization::WriteBool(aWriter, SKSEActive);
    Serialization::WriteBool(aWriter, MO2Active);
    Serialization::WriteString(aWriter, Version);
    UserMods.Serialize(aWriter);
    Settings.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteVarInt(aWriter, GameplayProtocolRevision);
    Serialization::WriteVarInt(aWriter, ServerCapabilities);
    Serialization::WriteVarInt(aWriter, NegotiatedCapabilities);
    Serialization::WriteVarInt(aWriter, ServerInstanceNonce);
    Serialization::WriteVarInt(aWriter, ConnectionGeneration);
    Serialization::WriteVarInt(aWriter, ClientSessionNonce);
    Serialization::WriteVarInt(aWriter, ConnectionAttempt);
}

void AuthenticationResponse::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Type = static_cast<ResponseType>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);
    SKSEActive = Serialization::ReadBool(aReader);
    MO2Active = Serialization::ReadBool(aReader);
    Version = Serialization::ReadString(aReader);
    UserMods.Deserialize(aReader);
    Settings.Deserialize(aReader);
    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    GameplayProtocolRevision = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ServerCapabilities = Serialization::ReadVarInt(aReader);
    NegotiatedCapabilities = Serialization::ReadVarInt(aReader);
    ServerInstanceNonce = Serialization::ReadVarInt(aReader);
    ConnectionGeneration = Serialization::ReadVarInt(aReader);
    ClientSessionNonce = Serialization::ReadVarInt(aReader);
    ConnectionAttempt = Serialization::ReadVarInt(aReader);
}
