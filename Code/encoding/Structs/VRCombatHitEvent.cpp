#include <Structs/VRCombatHitEvent.h>

#include <TiltedCore/Serialization.hpp>

bool VRCombatHitEvent::operator==(const VRCombatHitEvent& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && HitterId == acRhs.HitterId && HitteeId == acRhs.HitteeId &&
           SourceId == acRhs.SourceId && ProjectileId == acRhs.ProjectileId &&
           HitterPosition == acRhs.HitterPosition && HitteePosition == acRhs.HitteePosition &&
           RawHitFlags == acRhs.RawHitFlags && HitterFormType == acRhs.HitterFormType &&
           HitteeFormType == acRhs.HitteeFormType && PlanckHit == acRhs.PlanckHit &&
           HitterIsPlayer == acRhs.HitterIsPlayer && HitteeIsPlayer == acRhs.HitteeIsPlayer;
}

bool VRCombatHitEvent::operator!=(const VRCombatHitEvent& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRCombatHitEvent::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    HitterId.Serialize(aWriter);
    HitteeId.Serialize(aWriter);
    SourceId.Serialize(aWriter);
    ProjectileId.Serialize(aWriter);
    HitterPosition.Serialize(aWriter);
    HitteePosition.Serialize(aWriter);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, RawHitFlags);
    aWriter.WriteBits(HitterFormType, 8);
    aWriter.WriteBits(HitteeFormType, 8);
    TiltedPhoques::Serialization::WriteBool(aWriter, PlanckHit);
    TiltedPhoques::Serialization::WriteBool(aWriter, HitterIsPlayer);
    TiltedPhoques::Serialization::WriteBool(aWriter, HitteeIsPlayer);
}

void VRCombatHitEvent::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    HitterId.Deserialize(aReader);
    HitteeId.Deserialize(aReader);
    SourceId.Deserialize(aReader);
    ProjectileId.Deserialize(aReader);
    HitterPosition.Deserialize(aReader);
    HitteePosition.Deserialize(aReader);
    RawHitFlags = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    uint64_t hitterFormType{};
    aReader.ReadBits(hitterFormType, 8);
    HitterFormType = hitterFormType & 0xFF;

    uint64_t hitteeFormType{};
    aReader.ReadBits(hitteeFormType, 8);
    HitteeFormType = hitteeFormType & 0xFF;

    PlanckHit = TiltedPhoques::Serialization::ReadBool(aReader);
    HitterIsPlayer = TiltedPhoques::Serialization::ReadBool(aReader);
    HitteeIsPlayer = TiltedPhoques::Serialization::ReadBool(aReader);
}
