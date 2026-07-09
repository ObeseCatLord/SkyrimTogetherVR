#include <Structs/VRMovementUpdate.h>

#include <cstring>

#include <TiltedCore/Serialization.hpp>

bool VRMovementUpdate::operator==(const VRMovementUpdate& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId &&
           Position == acRhs.Position && Rotation == acRhs.Rotation && Direction == acRhs.Direction;
}

bool VRMovementUpdate::operator!=(const VRMovementUpdate& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRMovementUpdate::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Rotation.Serialize(aWriter);

    uint32_t direction{};
    std::memcpy(&direction, &Direction, sizeof(direction));
    aWriter.WriteBits(direction, 32);
}

void VRMovementUpdate::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Rotation.Deserialize(aReader);

    uint64_t direction{};
    aReader.ReadBits(direction, 32);
    const uint32_t direction32 = direction & 0xFFFFFFFF;
    std::memcpy(&Direction, &direction32, sizeof(Direction));
}
