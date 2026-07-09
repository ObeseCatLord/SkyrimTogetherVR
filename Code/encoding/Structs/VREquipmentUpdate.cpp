#include <Structs/VREquipmentUpdate.h>

#include <TiltedCore/Serialization.hpp>

bool VREquipmentUpdate::operator==(const VREquipmentUpdate& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && WeaponDrawn == acRhs.WeaponDrawn &&
           WeaponFullyDrawn == acRhs.WeaponFullyDrawn && LeftWeapon == acRhs.LeftWeapon &&
           RightWeapon == acRhs.RightWeapon && Ammo == acRhs.Ammo && LeftSpell == acRhs.LeftSpell &&
           RightSpell == acRhs.RightSpell && PowerOrShout == acRhs.PowerOrShout;
}

bool VREquipmentUpdate::operator!=(const VREquipmentUpdate& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VREquipmentUpdate::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    TiltedPhoques::Serialization::WriteBool(aWriter, WeaponDrawn);
    TiltedPhoques::Serialization::WriteBool(aWriter, WeaponFullyDrawn);
    LeftWeapon.Serialize(aWriter);
    RightWeapon.Serialize(aWriter);
    Ammo.Serialize(aWriter);
    LeftSpell.Serialize(aWriter);
    RightSpell.Serialize(aWriter);
    PowerOrShout.Serialize(aWriter);
}

void VREquipmentUpdate::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    WeaponDrawn = TiltedPhoques::Serialization::ReadBool(aReader);
    WeaponFullyDrawn = TiltedPhoques::Serialization::ReadBool(aReader);
    LeftWeapon.Deserialize(aReader);
    RightWeapon.Deserialize(aReader);
    Ammo.Deserialize(aReader);
    LeftSpell.Deserialize(aReader);
    RightSpell.Deserialize(aReader);
    PowerOrShout.Deserialize(aReader);
}
