#include <Structs/ObjectData.h>
#include <TiltedCore/Serialization.hpp>

using TiltedPhoques::Serialization;

bool ObjectData::operator==(const ObjectData& acRhs) const noexcept
{
    return ServerId == acRhs.ServerId && Id == acRhs.Id && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && CurrentCoords == acRhs.CurrentCoords && CurrentLockData == acRhs.CurrentLockData && CurrentInventory == acRhs.CurrentInventory && HasCurrentOpenState == acRhs.HasCurrentOpenState && CurrentOpenState == acRhs.CurrentOpenState && IsSenderFirst == acRhs.IsSenderFirst;
}

bool ObjectData::operator!=(const ObjectData& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ObjectData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Id.Serialize(aWriter);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    CurrentCoords.Serialize(aWriter);
    CurrentLockData.Serialize(aWriter);
    CurrentInventory.Serialize(aWriter);
    Serialization::WriteBool(aWriter, HasCurrentOpenState);
    aWriter.WriteBits(CurrentOpenState, 8);
    Serialization::WriteBool(aWriter, IsSenderFirst);
}

void ObjectData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Id.Deserialize(aReader);
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    CurrentCoords.Deserialize(aReader);
    CurrentLockData.Deserialize(aReader);
    CurrentInventory.Deserialize(aReader);
    if (!CurrentInventory.IsDecodedValid)
    {
        IsDecodedValid = false;
        return;
    }
    HasCurrentOpenState = Serialization::ReadBool(aReader);
    uint64_t currentOpenState{};
    if (!aReader.ReadBits(currentOpenState, 8))
    {
        IsDecodedValid = false;
        return;
    }
    CurrentOpenState = currentOpenState & 0xFF;
    IsSenderFirst = Serialization::ReadBool(aReader);
    IsDecodedValid = HasValidCurrentOpenState();
}
