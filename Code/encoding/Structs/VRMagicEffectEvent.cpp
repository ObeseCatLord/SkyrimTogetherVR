#include <Structs/VRMagicEffectEvent.h>

#include <TiltedCore/Serialization.hpp>

bool VRMagicEffectEvent::operator==(const VRMagicEffectEvent& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && EffectId == acRhs.EffectId && CasterId == acRhs.CasterId &&
           TargetId == acRhs.TargetId && CasterPosition == acRhs.CasterPosition &&
           TargetPosition == acRhs.TargetPosition && CasterFormType == acRhs.CasterFormType &&
           TargetFormType == acRhs.TargetFormType && CasterIsPlayer == acRhs.CasterIsPlayer &&
           TargetIsPlayer == acRhs.TargetIsPlayer;
}

bool VRMagicEffectEvent::operator!=(const VRMagicEffectEvent& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRMagicEffectEvent::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    EffectId.Serialize(aWriter);
    CasterId.Serialize(aWriter);
    TargetId.Serialize(aWriter);
    CasterPosition.Serialize(aWriter);
    TargetPosition.Serialize(aWriter);
    aWriter.WriteBits(CasterFormType, 8);
    aWriter.WriteBits(TargetFormType, 8);
    TiltedPhoques::Serialization::WriteBool(aWriter, CasterIsPlayer);
    TiltedPhoques::Serialization::WriteBool(aWriter, TargetIsPlayer);
}

void VRMagicEffectEvent::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    EffectId.Deserialize(aReader);
    CasterId.Deserialize(aReader);
    TargetId.Deserialize(aReader);
    CasterPosition.Deserialize(aReader);
    TargetPosition.Deserialize(aReader);

    uint64_t casterFormType{};
    aReader.ReadBits(casterFormType, 8);
    CasterFormType = casterFormType & 0xFF;

    uint64_t targetFormType{};
    aReader.ReadBits(targetFormType, 8);
    TargetFormType = targetFormType & 0xFF;

    CasterIsPlayer = TiltedPhoques::Serialization::ReadBool(aReader);
    TargetIsPlayer = TiltedPhoques::Serialization::ReadBool(aReader);
}
