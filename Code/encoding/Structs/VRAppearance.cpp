#include <Structs/VRAppearance.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <TiltedCore/Serialization.hpp>

namespace
{
constexpr std::uint8_t kTintMaskTypeCount = 15;

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

template <std::size_t Size>
bool IsValidUtf8(const std::array<char, Size>& acBytes, const std::size_t aLength) noexcept
{
    if (aLength > acBytes.size())
        return false;
    for (std::size_t index = 0; index < aLength;)
    {
        const auto byte = static_cast<std::uint8_t>(acBytes[index]);
        if (byte == 0)
            return false;
        if (byte < 0x80)
        {
            ++index;
            continue;
        }

        std::size_t continuationCount{};
        std::uint32_t codePoint{};
        if (byte >= 0xC2 && byte <= 0xDF) { continuationCount = 1; codePoint = byte & 0x1Fu; }
        else if (byte >= 0xE0 && byte <= 0xEF) { continuationCount = 2; codePoint = byte & 0x0Fu; }
        else if (byte >= 0xF0 && byte <= 0xF4) { continuationCount = 3; codePoint = byte & 0x07u; }
        else return false;
        if (index + continuationCount >= aLength)
            return false;
        for (std::size_t continuation = 1; continuation <= continuationCount; ++continuation)
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
        index += continuationCount + 1;
    }
    return true;
}

bool IsStructurallySafeRelativeResourcePath(
    const std::array<char, VRAppearanceTint::kMaximumTexturePathBytes>& acBytes,
    const std::uint8_t aLength) noexcept
{
    if (aLength == 0)
        return true;
    if (acBytes[0] == '/' || acBytes[0] == '\\')
        return false;

    std::size_t segmentBegin{};
    for (std::size_t index = 0; index <= aLength; ++index)
    {
        if (index != aLength && acBytes[index] != '/' && acBytes[index] != '\\')
        {
            if (acBytes[index] == ':')
                return false;
            continue;
        }

        const auto segmentLength = index - segmentBegin;
        if (segmentLength == 0 || (segmentLength == 1 && acBytes[segmentBegin] == '.') ||
            (segmentLength == 2 && acBytes[segmentBegin] == '.' && acBytes[segmentBegin + 1] == '.'))
            return false;
        segmentBegin = index + 1;
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
    return Type == acRhs.Type && Color == acRhs.Color && Alpha == acRhs.Alpha &&
           TexturePathLength == acRhs.TexturePathLength && TexturePath == acRhs.TexturePath;
}

bool VRAppearanceTint::operator!=(const VRAppearanceTint& acRhs) const noexcept
{
    return !(*this == acRhs);
}

bool VRAppearance::operator==(const VRAppearance& acRhs) const noexcept
{
    return SchemaVersion == acRhs.SchemaVersion && Sequence == acRhs.Sequence && RaceId == acRhs.RaceId &&
           Sex == acRhs.Sex && Weight == acRhs.Weight &&
           Level == acRhs.Level && Essential == acRhs.Essential && HairColorId == acRhs.HairColorId &&
           FaceTextureId == acRhs.FaceTextureId && HasFaceData == acRhs.HasFaceData &&
           FaceMorphs == acRhs.FaceMorphs && FaceParts == acRhs.FaceParts &&
           NameLength == acRhs.NameLength && Name == acRhs.Name && HeadPartCount == acRhs.HeadPartCount &&
           HeadParts == acRhs.HeadParts && TintCount == acRhs.TintCount && Tints == acRhs.Tints;
}

bool VRAppearance::operator!=(const VRAppearance& acRhs) const noexcept
{
    return !(*this == acRhs);
}

VRAppearance::ValidationMask VRAppearance::GetValidationFailureMask() const noexcept
{
    ValidationMask failures = kValidationFailureNone;

    if (SchemaVersion != kSchemaVersion || Sequence == 0 || !RaceId || Sex > 1 || !std::isfinite(Weight) ||
        Weight < 0.0F || Weight > 100.0F || Level == 0)
        failures |= kValidationFailureCoreSchema;

    const bool validNameLength = NameLength != 0 && NameLength <= kMaximumNameBytes;
    if (!validNameLength)
        failures |= kValidationFailureNameLength;
    else
    {
        if (!IsValidUtf8(Name, NameLength))
            failures |= kValidationFailureNameUtf8;
        for (std::size_t index = NameLength; index < Name.size(); ++index)
            if (Name[index] != '\0')
            {
                failures |= kValidationFailureNameZeroTail;
                break;
            }
    }

    for (const auto morph : FaceMorphs) {
        if (!std::isfinite(morph) || std::abs(morph) > kMaximumFaceMorphMagnitude || (!HasFaceData && morph != 0.0F))
        {
            failures |= kValidationFailureFaceMorphs;
            break;
        }
    }
    for (const auto part : FaceParts) {
        if ((!HasFaceData && part != 0) ||
            (HasFaceData && part != kFacePartDefault && (part < 0 || part > kMaximumFacePartPreset)))
        {
            failures |= kValidationFailureFaceParts;
            break;
        }
    }

    if (HeadPartCount > kMaximumHeadParts)
        failures |= kValidationFailureHeadPartCount;
    else
    {
        std::array<bool, kMaximumHeadParts> occupiedSlots{};
        for (std::size_t index = 0; index < HeadPartCount; ++index)
        {
            const auto& headPart = HeadParts[index];
            if (headPart.Slot >= kMaximumHeadParts || !headPart.FormId)
            {
                failures |= kValidationFailureHeadPartEntry;
                continue;
            }
            if (occupiedSlots[headPart.Slot])
                failures |= kValidationFailureDuplicateHeadPartSlot;
            occupiedSlots[headPart.Slot] = true;
        }
        for (std::size_t index = HeadPartCount; index < HeadParts.size(); ++index)
            if (HeadParts[index].Slot != 0 || HeadParts[index].FormId)
            {
                failures |= kValidationFailureUnusedHeadPartTail;
                break;
            }
    }

    if (TintCount > kMaximumTints)
        failures |= kValidationFailureTintCount;
    else
    {
        for (std::size_t index = 0; index < TintCount; ++index)
        {
            const auto& tint = Tints[index];
            if (tint.Type >= kTintMaskTypeCount || !std::isfinite(tint.Alpha) || tint.Alpha < 0.0F ||
                tint.Alpha > 1.0F)
                failures |= kValidationFailureTintEntry;

            if (!IsValidUtf8(tint.TexturePath, tint.TexturePathLength))
                failures |= kValidationFailureTintPathUtf8;
            else if (!IsStructurallySafeRelativeResourcePath(tint.TexturePath, tint.TexturePathLength))
                failures |= kValidationFailureTintPathStructuralSafety;

            for (std::size_t pathIndex = tint.TexturePathLength; pathIndex < tint.TexturePath.size(); ++pathIndex)
                if (tint.TexturePath[pathIndex] != '\0')
                {
                    failures |= kValidationFailureTintPathZeroTail;
                    break;
                }
        }
        for (std::size_t index = TintCount; index < Tints.size(); ++index)
            if (Tints[index].Type != 0 || Tints[index].Color != 0 || Tints[index].Alpha != 0.0F ||
                Tints[index].TexturePathLength != 0 ||
                std::any_of(Tints[index].TexturePath.begin(), Tints[index].TexturePath.end(),
                            [](const char value) { return value != '\0'; }))
            {
                failures |= kValidationFailureUnusedTintTail;
                break;
            }
    }

    return failures;
}

bool VRAppearance::IsValid() const noexcept
{
    return GetValidationFailureMask() == kValidationFailureNone;
}

void VRAppearance::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(SchemaVersion, 8);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    RaceId.Serialize(aWriter);
    aWriter.WriteBits(Sex, 8);
    WriteFloat(aWriter, Weight);
    aWriter.WriteBits(Level, 16);
    aWriter.WriteBits(Essential ? 1u : 0u, 1);
    HairColorId.Serialize(aWriter);
    FaceTextureId.Serialize(aWriter);
    aWriter.WriteBits(HasFaceData ? 1u : 0u, 1);
    for (const auto morph : FaceMorphs)
        WriteFloat(aWriter, morph);
    for (const auto part : FaceParts)
        aWriter.WriteBits(static_cast<std::uint32_t>(part), 32);
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
        aWriter.WriteBits(Tints[index].TexturePathLength, 8);
        for (std::uint16_t pathIndex = 0; pathIndex < Tints[index].TexturePathLength; ++pathIndex)
            aWriter.WriteBits(static_cast<std::uint8_t>(Tints[index].TexturePath[pathIndex]), 8);
    }
}

void VRAppearance::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    std::uint64_t value{};
    aReader.ReadBits(value, 8);
    SchemaVersion = static_cast<std::uint8_t>(value);
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RaceId.Deserialize(aReader);
    aReader.ReadBits(value, 8);
    Sex = static_cast<std::uint8_t>(value);
    Weight = ReadFloat(aReader);
    aReader.ReadBits(value, 16);
    Level = static_cast<std::uint16_t>(value);
    aReader.ReadBits(value, 1);
    Essential = value != 0;
    HairColorId.Deserialize(aReader);
    FaceTextureId.Deserialize(aReader);
    aReader.ReadBits(value, 1);
    HasFaceData = value != 0;
    for (auto& morph : FaceMorphs)
        morph = ReadFloat(aReader);
    for (auto& part : FaceParts)
    {
        aReader.ReadBits(value, 32);
        part = static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
    }
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
        aReader.ReadBits(value, 8);
        const auto texturePathLength = static_cast<std::uint8_t>(value);
        VRAppearanceTint tint{type, color, alpha};
        tint.TexturePathLength = texturePathLength;
        for (std::uint16_t pathIndex = 0; pathIndex < texturePathLength; ++pathIndex)
        {
            aReader.ReadBits(value, 8);
            tint.TexturePath[pathIndex] = static_cast<char>(value);
        }
        if (index < kMaximumTints)
            Tints[index] = tint;
    }
    if (invalidNameLength || invalidHeadPartCount || invalidTintCount)
        SchemaVersion = 0;
}
