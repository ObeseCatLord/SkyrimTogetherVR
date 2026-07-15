#include <Messages/AuthenticationRequest.h>

void AuthenticationRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, DiscordId);
    Serialization::WriteBool(aWriter, SKSEActive);
    Serialization::WriteBool(aWriter, MO2Active);
    Serialization::WriteString(aWriter, Token);
    Serialization::WriteString(aWriter, Version);
    UserMods.Serialize(aWriter);
    Serialization::WriteString(aWriter, Username);
    WorldSpaceId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Level);
    PlayerTime.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, GameplayProtocolRevision);
    Serialization::WriteVarInt(aWriter, GameplayCapabilities);
    Serialization::WriteVarInt(aWriter, ClientSessionNonce);
    Serialization::WriteVarInt(aWriter, ConnectionAttempt);
}

void AuthenticationRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    DiscordId = Serialization::ReadVarInt(aReader);
    SKSEActive = Serialization::ReadBool(aReader);
    MO2Active = Serialization::ReadBool(aReader);
    Token = Serialization::ReadString(aReader);
    Version = Serialization::ReadString(aReader);
    UserMods.Deserialize(aReader);
    Username = Serialization::ReadString(aReader);
    WorldSpaceId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    Level = Serialization::ReadVarInt(aReader) & 0xFFFF;
    PlayerTime.Deserialize(aReader);
    GameplayProtocolRevision = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    GameplayCapabilities = Serialization::ReadVarInt(aReader);
    ClientSessionNonce = Serialization::ReadVarInt(aReader);
    ConnectionAttempt = Serialization::ReadVarInt(aReader);
}
