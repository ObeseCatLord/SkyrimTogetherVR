#include <Structs/VRActivationEvent.h>

#include <TiltedCore/Serialization.hpp>

bool VRActivationEvent::operator==(const VRActivationEvent& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && ObjectId == acRhs.ObjectId && CellId == acRhs.CellId &&
           WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && FormType == acRhs.FormType &&
           OpenState == acRhs.OpenState;
}

bool VRActivationEvent::operator!=(const VRActivationEvent& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRActivationEvent::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    ObjectId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    aWriter.WriteBits(FormType, 8);
    aWriter.WriteBits(OpenState, 8);
}

void VRActivationEvent::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ObjectId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);

    uint64_t formType{};
    aReader.ReadBits(formType, 8);
    FormType = formType & 0xFF;

    uint64_t openState{};
    aReader.ReadBits(openState, 8);
    OpenState = openState & 0xFF;
}
