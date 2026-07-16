#include <Structs/VRAppearance.h>

#include <cmath>
#include <cstring>

#include <TiltedCore/Serialization.hpp>

namespace
{
void WriteFloat(TiltedPhoques::Buffer::Writer& aWriter, const float aValue) noexcept
{
    std::uint32_t bits{};
    std::memcpy(&bits, &aValue, sizeof(bits));
    aWriter.WriteBits(bits, 32);
}

float ReadFloat(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    std::uint64_t bits{};
    aReader.ReadBits(bits, 32);
    const auto value = static_cast<std::uint32_t>(bits);
    float result{};
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

bool IsValidUtf8(const std::array<char, VRAppearance::kMaximumNameBytes>& acBytes, const std::uint8_t aLength) noexcept
{
    for (std::uint8_t index = 0; index < aLength;)
    {
        const auto byte = static_cast<std::uint8_t>(acBytes[index]);
        if (byte == 0)
            return false;
        if (byte < 0x80)
        {
            ++index;
            continue;
        }

        std::uint8_t continuationCount{};
        std::uint32_t codePoint{};
        if (byte >= 0xC2 && byte <= 0xDF) { continuationCount = 1; codePoint = byte & 0x1Fu; }
        else if (byte >= 0xE0 && byte <= 0xEF) { continuationCount = 2; codePoint = byte & 0x0Fu; }
        else if (byte >= 0xF0 && byte <= 0xF4) { continuationCount = 3; codePoint = byte & 0x07u; }
        else return false;
        if (index + continuationCount >= aLength)
            return false;
        for (std::uint8_t continuation = 1; continuation <= continuationCount; ++continuation)
        {
            const auto continuationByte = static_cast<std::uint8_t>(acBytes[index + continuation]);
            if ((continuationByte & 0xC0) != 0x80)
                return false;
            codePoint = (codePoint << 6) | (continuationByte & 0x3Fu);
        }
        if ((continuationCount == 2 && codePoint < 0x800) ||
            (continuationCount == 3 && codePoint < 0x10000) || codePoint > 0x10FFFF ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF))
            return false;
        index = static_cast<std::uint8_t>(index + continuationCount + 1);
    }
    return true;
}
} // namespace

bool VRAppearanceHeadPart::operator==(const VRAppearanceHeadPart& acRhs) const noexcept
{
    return Slot == acRhs.Slot && FormId == acRhs.FormId;
}

bool VRAppearanceHeadPart::operator!=(const VRAppearanceHeadPart& acRhs) const noexcept
{
    return !(*this == acRhs);
}

bool VRAppearanceTint::operator==(const VRAppearanceTint& acRhs) const noexcept
{
    return Type == acRhs.Type && Color == acRhs.Color && Alpha == acRhs.Alpha;
}

bool VRAppearanceTint::operator!=(const VRAppearanceTint& acRhs) const noexcept
{
    return !(*this == acRhs);
}

bool VRAppearance::operator==(const VRAppearance& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && RaceId == acRhs.RaceId && Sex == acRhs.Sex && Weight == acRhs.Weight &&
           Level == acRhs.Level && Essential == acRhs.Essential &&
           NameLength == acRhs.NameLength && Name == acRhs.Name && HeadPartCount == acRhs.HeadPartCount &&
           HeadParts == acRhs.HeadParts && TintCount == acRhs.TintCount && Tints == acRhs.Tints;
}

bool VRAppearance::operator!=(const VRAppearance& acRhs) const noexcept
{
    return !(*this == acRhs);
}

bool VRAppearance::IsValid() const noexcept
{
    if (Sequence == 0 || !RaceId || Sex > 1 || !std::isfinite(Weight) || Weight < 0.0F || Weight > 100.0F || Level == 0 ||
        NameLength > kMaximumNameBytes || HeadPartCount > kMaximumHeadParts || TintCount > kMaximumTints ||
        !IsValidUtf8(Name, NameLength))
        return false;

    for (std::uint8_t index = NameLength; index < kMaximumNameBytes; ++index)
        if (Name[index] != '\0')
            return false;
    for (std::uint8_t index = 0; index < HeadPartCount; ++index)
    {
        if (HeadParts[index].Slot >= kMaximumHeadParts || !HeadParts[index].FormId)
            return false;
        for (std::uint8_t prior = 0; prior < index; ++prior)
            if (HeadParts[prior].Slot == HeadParts[index].Slot)
                return false;
    }
    for (std::uint8_t index = HeadPartCount; index < kMaximumHeadParts; ++index)
        if (HeadParts[index].Slot != 0 || HeadParts[index].FormId)
            return false;
    for (std::uint8_t index = 0; index < TintCount; ++index)
    {
        if (Tints[index].Type >= kMaximumTints || !std::isfinite(Tints[index].Alpha) ||
            Tints[index].Alpha < 0.0F || Tints[index].Alpha > 1.0F)
            return false;
        for (std::uint8_t prior = 0; prior < index; ++prior)
            if (Tints[prior].Type == Tints[index].Type)
                return false;
    }
    for (std::uint8_t index = TintCount; index < kMaximumTints; ++index)
        if (Tints[index].Type != 0 || Tints[index].Color != 0 || Tints[index].Alpha != 0.0F)
            return false;
    return true;
}

void VRAppearance::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    RaceId.Serialize(aWriter);
    aWriter.WriteBits(Sex, 8);
    WriteFloat(aWriter, Weight);
    aWriter.WriteBits(Level, 16);
    aWriter.WriteBits(Essential ? 1u : 0u, 1);
    aWriter.WriteBits(NameLength, 8);
    for (std::uint8_t index = 0; index < NameLength; ++index)
        aWriter.WriteBits(static_cast<std::uint8_t>(Name[index]), 8);
    aWriter.WriteBits(HeadPartCount, 8);
    for (std::uint8_t index = 0; index < HeadPartCount; ++index)
    {
        aWriter.WriteBits(HeadParts[index].Slot, 8);
        HeadParts[index].FormId.Serialize(aWriter);
    }
    aWriter.WriteBits(TintCount, 8);
    for (std::uint8_t index = 0; index < TintCount; ++index)
    {
        aWriter.WriteBits(Tints[index].Type, 8);
        aWriter.WriteBits(Tints[index].Color, 32);
        WriteFloat(aWriter, Tints[index].Alpha);
    }
}

void VRAppearance::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RaceId.Deserialize(aReader);
    std::uint64_t value{};
    aReader.ReadBits(value, 8);
    Sex = static_cast<std::uint8_t>(value);
    Weight = ReadFloat(aReader);
    aReader.ReadBits(value, 16);
    Level = static_cast<std::uint16_t>(value);
    aReader.ReadBits(value, 1);
    Essential = value != 0;
    aReader.ReadBits(value, 8);
    const auto encodedNameLength = static_cast<std::uint8_t>(value);
    const bool invalidNameLength = encodedNameLength > kMaximumNameBytes;
    NameLength = invalidNameLength ? 0 : encodedNameLength;
    for (std::size_t index = 0; index < encodedNameLength; ++index)
    {
        aReader.ReadBits(value, 8);
        if (index < kMaximumNameBytes)
            Name[index] = static_cast<char>(value);
    }
    aReader.ReadBits(value, 8);
    const auto encodedHeadPartCount = static_cast<std::uint8_t>(value);
    const bool invalidHeadPartCount = encodedHeadPartCount > kMaximumHeadParts;
    HeadPartCount = invalidHeadPartCount ? 0 : encodedHeadPartCount;
    for (std::size_t index = 0; index < encodedHeadPartCount; ++index)
    {
        aReader.ReadBits(value, 8);
        GameId formId{};
        formId.Deserialize(aReader);
        if (index < kMaximumHeadParts)
            HeadParts[index] = {static_cast<std::uint8_t>(value), formId};
    }
    aReader.ReadBits(value, 8);
    const auto encodedTintCount = static_cast<std::uint8_t>(value);
    const bool invalidTintCount = encodedTintCount > kMaximumTints;
    TintCount = invalidTintCount ? 0 : encodedTintCount;
    for (std::size_t index = 0; index < encodedTintCount; ++index)
    {
        aReader.ReadBits(value, 8);
        const auto type = static_cast<std::uint8_t>(value);
        aReader.ReadBits(value, 32);
        const auto color = static_cast<std::uint32_t>(value);
        const auto alpha = ReadFloat(aReader);
        if (index < kMaximumTints)
            Tints[index] = {type, color, alpha};
    }
    if (invalidNameLength || invalidHeadPartCount || invalidTintCount)
        Sex = 2;
}
