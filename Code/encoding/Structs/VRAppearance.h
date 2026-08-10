#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Structs/GameId.h>
#include <TiltedCore/Buffer.hpp>

struct VRAppearanceHeadPart
{
    std::uint8_t Slot{0};
    GameId FormId{};

    bool operator==(const VRAppearanceHeadPart& acRhs) const noexcept;
    bool operator!=(const VRAppearanceHeadPart& acRhs) const noexcept;
};

struct VRAppearanceTint
{
    static constexpr std::size_t kMaximumTexturePathBytes = 255;

    std::uint8_t Type{0};
    std::uint32_t Color{0};
    float Alpha{0.0F};
    std::uint8_t TexturePathLength{0};
    std::array<char, kMaximumTexturePathBytes> TexturePath{};

    bool operator==(const VRAppearanceTint& acRhs) const noexcept;
    bool operator!=(const VRAppearanceTint& acRhs) const noexcept;
};

// Semantic, ABI-independent appearance data. All variable content is stored
// in fixed arrays so a malformed peer cannot cause an unbounded allocation.
struct VRAppearance
{
    using ValidationMask = std::uint32_t;

    // Stable semantic-validation diagnostics. New failures must use an unused bit.
    enum ValidationFailure : ValidationMask
    {
        kValidationFailureNone = 0x00000000u,
        kValidationFailureCoreSchema = 0x00000001u,
        kValidationFailureNameLength = 0x00000002u,
        kValidationFailureNameUtf8 = 0x00000004u,
        kValidationFailureNameZeroTail = 0x00000008u,
        kValidationFailureFaceMorphs = 0x00000010u,
        kValidationFailureFaceParts = 0x00000020u,
        kValidationFailureHeadPartCount = 0x00000040u,
        kValidationFailureHeadPartEntry = 0x00000080u,
        kValidationFailureDuplicateHeadPartSlot = 0x00000100u,
        kValidationFailureUnusedHeadPartTail = 0x00000200u,
        kValidationFailureTintCount = 0x00000400u,
        kValidationFailureTintEntry = 0x00000800u,
        kValidationFailureTintPathUtf8 = 0x00001000u,
        kValidationFailureTintPathStructuralSafety = 0x00002000u,
        kValidationFailureTintPathZeroTail = 0x00004000u,
        kValidationFailureUnusedTintTail = 0x00008000u,
    };

    static constexpr std::uint8_t kSchemaVersion = 2;
    static constexpr std::uint8_t kMaximumNameBytes = 127;
    static constexpr std::uint8_t kMaximumHeadParts = 7;
    static constexpr std::uint8_t kMaximumTints = 32;
    static constexpr std::uint8_t kFaceMorphCount = 19;
    static constexpr std::uint8_t kFacePartCount = 4;
    static constexpr std::int32_t kFacePartDefault = 0x7F7FFFFF;
    static constexpr float kMaximumFaceMorphMagnitude = 10.0F;
    static constexpr std::int32_t kMaximumFacePartPreset = 255;

    bool operator==(const VRAppearance& acRhs) const noexcept;
    bool operator!=(const VRAppearance& acRhs) const noexcept;

    [[nodiscard]] ValidationMask GetValidationFailureMask() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    std::uint8_t SchemaVersion{kSchemaVersion};
    std::uint32_t Sequence{0};
    GameId RaceId{};
    std::uint8_t Sex{0};
    float Weight{0.0F};
    std::uint16_t Level{1};
    bool Essential{false};
    GameId HairColorId{};
    GameId FaceTextureId{};
    bool HasFaceData{false};
    std::array<float, kFaceMorphCount> FaceMorphs{};
    std::array<std::int32_t, kFacePartCount> FaceParts{};
    std::uint8_t NameLength{0};
    std::array<char, kMaximumNameBytes> Name{};
    std::uint8_t HeadPartCount{0};
    std::array<VRAppearanceHeadPart, kMaximumHeadParts> HeadParts{};
    std::uint8_t TintCount{0};
    std::array<VRAppearanceTint, kMaximumTints> Tints{};
};

static_assert(sizeof(VRAppearance) < 16 * 1024, "VRAppearance must remain bounded below 16 KiB");
