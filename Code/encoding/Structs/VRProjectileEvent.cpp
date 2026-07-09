#include <Structs/VRProjectileEvent.h>

#include <TiltedCore/Serialization.hpp>

bool VRProjectileEvent::operator==(const VRProjectileEvent& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && EventKind == acRhs.EventKind && SourceId == acRhs.SourceId &&
           WeaponId == acRhs.WeaponId && AmmoId == acRhs.AmmoId && SpellId == acRhs.SpellId &&
           Origin == acRhs.Origin && Destination == acRhs.Destination && Power == acRhs.Power &&
           OriginValid == acRhs.OriginValid && DestinationValid == acRhs.DestinationValid &&
           IsSunGazing == acRhs.IsSunGazing;
}

bool VRProjectileEvent::operator!=(const VRProjectileEvent& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRProjectileEvent::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    aWriter.WriteBits(static_cast<uint8_t>(EventKind), 8);
    SourceId.Serialize(aWriter);
    WeaponId.Serialize(aWriter);
    AmmoId.Serialize(aWriter);
    SpellId.Serialize(aWriter);
    Origin.Serialize(aWriter);
    Destination.Serialize(aWriter);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Power);
    TiltedPhoques::Serialization::WriteBool(aWriter, OriginValid);
    TiltedPhoques::Serialization::WriteBool(aWriter, DestinationValid);
    TiltedPhoques::Serialization::WriteBool(aWriter, IsSunGazing);
}

void VRProjectileEvent::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    uint64_t kind{};
    aReader.ReadBits(kind, 8);
    EventKind = static_cast<Kind>(kind & 0xFF);

    SourceId.Deserialize(aReader);
    WeaponId.Deserialize(aReader);
    AmmoId.Deserialize(aReader);
    SpellId.Deserialize(aReader);
    Origin.Deserialize(aReader);
    Destination.Deserialize(aReader);
    Power = TiltedPhoques::Serialization::ReadFloat(aReader);
    OriginValid = TiltedPhoques::Serialization::ReadBool(aReader);
    DestinationValid = TiltedPhoques::Serialization::ReadBool(aReader);
    IsSunGazing = TiltedPhoques::Serialization::ReadBool(aReader);
}
