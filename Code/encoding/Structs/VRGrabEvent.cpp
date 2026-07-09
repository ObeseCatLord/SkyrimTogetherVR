#include <Structs/VRGrabEvent.h>

#include <TiltedCore/Serialization.hpp>

bool VRGrabEvent::operator==(const VRGrabEvent& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && ObjectId == acRhs.ObjectId && CellId == acRhs.CellId &&
           WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && FormType == acRhs.FormType &&
           Grabbed == acRhs.Grabbed;
}

bool VRGrabEvent::operator!=(const VRGrabEvent& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRGrabEvent::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    ObjectId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    aWriter.WriteBits(FormType, 8);
    TiltedPhoques::Serialization::WriteBool(aWriter, Grabbed);
}

void VRGrabEvent::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ObjectId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);

    uint64_t formType{};
    aReader.ReadBits(formType, 8);
    FormType = formType & 0xFF;

    Grabbed = TiltedPhoques::Serialization::ReadBool(aReader);
}
