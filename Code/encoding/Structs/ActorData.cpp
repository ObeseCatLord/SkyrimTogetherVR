#include <Structs/ActorData.h>
#include <TiltedCore/Serialization.hpp>

using TiltedPhoques::Serialization;

void ActorData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    InitialActorValues.Serialize(aWriter);
    InitialInventory.Serialize(aWriter);
    Serialization::WriteBool(aWriter, IsDead);
    Serialization::WriteBool(aWriter, IsWeaponDrawn);
}

void ActorData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    InitialActorValues.Deserialize(aReader);
    if (!InitialActorValues.IsDecodedValid)
        return;
    InitialInventory.Deserialize(aReader);
    if (!InitialInventory.IsDecodedValid)
        return;
    IsDead = Serialization::ReadBool(aReader);
    IsWeaponDrawn = Serialization::ReadBool(aReader);
}
