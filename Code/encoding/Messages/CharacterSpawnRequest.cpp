#include <Messages/CharacterSpawnRequest.h>

void CharacterSpawnRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    FormId.Serialize(aWriter);
    BaseId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Rotation.Serialize(aWriter);
    aWriter.WriteBits(ChangeFlags, 32);
    Serialization::WriteString(aWriter, AppearanceBuffer);
    InventoryContent.Serialize(aWriter);
    FactionsContent.Serialize(aWriter);
    ActionsToReplay.Serialize(aWriter);
    FaceTints.Serialize(aWriter);
    InitialActorValues.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteBool(aWriter, IsDead);
    Serialization::WriteBool(aWriter, IsPlayer);
    Serialization::WriteBool(aWriter, IsWeaponDrawn);
    Serialization::WriteBool(aWriter, IsPlayerSummon);
    Serialization::WriteBool(aWriter, HasVRAppearance);
    if (HasVRAppearance)
        InitialVRAppearance.Serialize(aWriter);
}

void CharacterSpawnRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    IsDecodedValid = true;
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    FormId.Deserialize(aReader);
    BaseId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Rotation.Deserialize(aReader);

    uint64_t dest = 0;
    if (!aReader.ReadBits(dest, 32))
    {
        IsDecodedValid = false;
        return;
    }
    ChangeFlags = dest & 0xFFFFFFFF;

    try
    {
        AppearanceBuffer = Serialization::ReadString(aReader);
    }
    catch (...)
    {
        IsDecodedValid = false;
        return;
    }
    InventoryContent = {};
    InventoryContent.Deserialize(aReader);
    if (!InventoryContent.IsDecodedValid)
    {
        IsDecodedValid = false;
        return;
    }

    FactionsContent = {};
    FactionsContent.Deserialize(aReader);
    if (!FactionsContent.IsDecodedValid)
    {
        IsDecodedValid = false;
        return;
    }

    ActionsToReplay = {};
    ActionsToReplay.Deserialize(aReader);
    if (!ActionsToReplay.IsDecodedValid)
    {
        IsDecodedValid = false;
        return;
    }

    FaceTints.Deserialize(aReader);
    if (!FaceTints.IsDecodedValid)
    {
        IsDecodedValid = false;
        return;
    }
    InitialActorValues.Deserialize(aReader);
    if (!InitialActorValues.IsDecodedValid)
    {
        IsDecodedValid = false;
        return;
    }
    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    IsDead = Serialization::ReadBool(aReader);
    IsPlayer = Serialization::ReadBool(aReader);
    IsWeaponDrawn = Serialization::ReadBool(aReader);
    IsPlayerSummon = Serialization::ReadBool(aReader);

    // Transport decoding has no message-local version or payload-length boundary
    // after the legacy fields, so a missing extension bit is truncation.
    std::uint64_t hasVRAppearance{};
    if (!aReader.ReadBits(hasVRAppearance, 1))
    {
        IsDecodedValid = false;
        return;
    }
    HasVRAppearance = hasVRAppearance != 0;
    InitialVRAppearance = {};
    if (HasVRAppearance) {
        InitialVRAppearance.Deserialize(aReader);
        if (!InitialVRAppearance.IsValid())
            IsDecodedValid = false;
    }
}
