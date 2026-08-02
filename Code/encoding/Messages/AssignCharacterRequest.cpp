#include <Messages/AssignCharacterRequest.h>

void AssignCharacterRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Cookie);
    ReferenceId.Serialize(aWriter);
    FormId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Rotation.Serialize(aWriter);
    aWriter.WriteBits(ChangeFlags, 32);
    Serialization::WriteString(aWriter, AppearanceBuffer);
    FactionsContent.Serialize(aWriter);
    LatestAction.GenerateDifferential(ActionEvent{}, aWriter);
    QuestContent.Serialize(aWriter);
    FaceTints.Serialize(aWriter);
    Serialization::WriteBool(aWriter, HasQuestContent);
    Serialization::WriteBool(aWriter, HasFaceTints);
    Serialization::WriteBool(aWriter, IsDragon);
    Serialization::WriteBool(aWriter, IsMount);
    Serialization::WriteBool(aWriter, IsPlayerSummon);
    Serialization::WriteBool(aWriter, HasVRAppearance);
    if (HasVRAppearance)
        InitialVRAppearance.Serialize(aWriter);
    CurrentActorData.Serialize(aWriter);
}

void AssignCharacterRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    m_isDecodedValid = true;
    ClientMessage::DeserializeRaw(aReader);

    Cookie = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ReferenceId.Deserialize(aReader);
    FormId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Rotation.Deserialize(aReader);

    uint64_t dest = 0;
    if (!aReader.ReadBits(dest, 32))
    {
        m_isDecodedValid = false;
        return;
    }
    ChangeFlags = dest & 0xFFFFFFFF;

    try
    {
        AppearanceBuffer = Serialization::ReadString(aReader);
    }
    catch (...)
    {
        m_isDecodedValid = false;
        return;
    }

    FactionsContent = {};
    FactionsContent.Deserialize(aReader);
    if (!FactionsContent.IsDecodedValid)
    {
        m_isDecodedValid = false;
        return;
    }

    LatestAction = ActionEvent{};
    LatestAction.ApplyDifferential(aReader);
    if (!LatestAction.IsDecodedValid)
    {
        m_isDecodedValid = false;
        return;
    }

    QuestContent.Deserialize(aReader);
    if (!QuestContent.IsDecodedValid)
    {
        m_isDecodedValid = false;
        return;
    }
    FaceTints.Deserialize(aReader);
    if (!FaceTints.IsDecodedValid)
    {
        m_isDecodedValid = false;
        return;
    }

    HasQuestContent = Serialization::ReadBool(aReader);
    HasFaceTints = Serialization::ReadBool(aReader);
    IsDragon = Serialization::ReadBool(aReader);
    IsMount = Serialization::ReadBool(aReader);
    IsPlayerSummon = Serialization::ReadBool(aReader);
    HasVRAppearance = Serialization::ReadBool(aReader);
    InitialVRAppearance = {};
    if (HasVRAppearance)
        InitialVRAppearance.Deserialize(aReader);

    CurrentActorData.Deserialize(aReader);
    if (!CurrentActorData.IsDecodedValid())
        m_isDecodedValid = false;
}
