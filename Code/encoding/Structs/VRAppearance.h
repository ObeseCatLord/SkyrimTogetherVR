#pragma once

#include <array>
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
    std::uint8_t Type{0};
    std::uint32_t Color{0};
    float Alpha{0.0F};

    bool operator==(const VRAppearanceTint& acRhs) const noexcept;
    bool operator!=(const VRAppearanceTint& acRhs) const noexcept;
};

// Semantic, ABI-independent appearance data. All variable content is stored
// in fixed arrays so a malformed peer cannot cause an unbounded allocation.
struct VRAppearance
{
    static constexpr std::uint8_t kMaximumNameBytes = 127;
    static constexpr std::uint8_t kMaximumHeadParts = 7;
    static constexpr std::uint8_t kMaximumTints = 16;

    bool operator==(const VRAppearance& acRhs) const noexcept;
    bool operator!=(const VRAppearance& acRhs) const noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    std::uint32_t Sequence{0};
    GameId RaceId{};
    std::uint8_t Sex{0};
    float Weight{0.0F};
    std::uint16_t Level{1};
    bool Essential{false};
    std::uint8_t NameLength{0};
    std::array<char, kMaximumNameBytes> Name{};
    std::uint8_t HeadPartCount{0};
    std::array<VRAppearanceHeadPart, kMaximumHeadParts> HeadParts{};
    std::uint8_t TintCount{0};
    std::array<VRAppearanceTint, kMaximumTints> Tints{};
};
