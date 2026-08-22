#include <Structs/Movement.h>
#include <TiltedCore/Serialization.hpp>
#include <vr_common/VRAnimationGraphProtocol.h>
#include <bit>
#include <cmath>

using TiltedPhoques::Serialization;

bool Movement::operator==(const Movement& acRhs) const noexcept
{
    return CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && Rotation == acRhs.Rotation && Variables == acRhs.Variables &&
           std::bit_cast<std::uint32_t>(Direction) == std::bit_cast<std::uint32_t>(acRhs.Direction);
}

bool Movement::operator!=(const Movement& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

bool Movement::TryGetCanonicalDirection(float& arDirection) const noexcept
{
    const bool hasDescriptorMetadata = !Variables.Booleans.empty() || !Variables.Floats.empty() ||
                                       !Variables.Integers.empty() || Variables.DescriptorDigest != 0 ||
                                       Variables.DirectionFloatIndex != 0;
    if (!hasDescriptorMetadata)
    {
        arDirection = Direction;
        return std::isfinite(arDirection);
    }

    if (!SkyrimTogetherVR::AnimationGraphProtocol::IsValidDescriptorContract(
            Variables.Booleans.size(), Variables.Floats.size(), Variables.Integers.size(),
            Variables.DescriptorDigest, Variables.DirectionFloatIndex))
        return false;

    arDirection = Variables.Floats[Variables.DirectionFloatIndex];
    return std::isfinite(arDirection);
}

void Movement::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    float canonicalDirection = Direction;
    // Production producers reject invalid metadata before constructing the
    // enclosing request. Preserve record framing for defensive/direct callers;
    // ApplyDiff and the server will reject the invalid descriptor contract.
    static_cast<void>(TryGetCanonicalDirection(canonicalDirection));

    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Rotation.Serialize(aWriter);
    Variables.GenerateDiff(AnimationVariables{}, aWriter);
    aWriter.WriteBits(std::bit_cast<std::uint32_t>(canonicalDirection), 32);
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
