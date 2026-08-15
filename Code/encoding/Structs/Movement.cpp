#include <Structs/Movement.h>
#include <TiltedCore/Serialization.hpp>
#include <bit>
#include <cmath>

using TiltedPhoques::Serialization;

bool Movement::operator==(const Movement& acRhs) const noexcept
{
    return CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && Rotation == acRhs.Rotation && Variables == acRhs.Variables && Direction == acRhs.Direction;
}

bool Movement::operator!=(const Movement& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Movement::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Rotation.Serialize(aWriter);
    Variables.GenerateDiff(AnimationVariables{}, aWriter);
    aWriter.WriteBits(std::bit_cast<std::uint32_t>(Direction), 32);
}

void Movement::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Rotation.Deserialize(aReader);
    Variables = AnimationVariables{};
    if (!Variables.ApplyDiff(aReader))
    {
        IsDecodedValid = false;
        return;
    }

    uint64_t tmp = 0;
    if (!aReader.ReadBits(tmp, 32))
    {
        IsDecodedValid = false;
        return;
    }
    const uint32_t tmp32 = tmp & 0xFFFFFFFF;
    Direction = std::bit_cast<float>(tmp32);
    IsDecodedValid = std::isfinite(Direction);
}
