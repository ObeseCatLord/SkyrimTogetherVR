#pragma once

#include <cstddef>
#include <cstdint>

using TiltedPhoques::Vector;

struct AnimationVariables
{
    static constexpr std::size_t kMaximumBooleanCount = 256;
    static constexpr std::size_t kMaximumIntegerCount = 256;
    static constexpr std::size_t kMaximumFloatCount = 256;
    static constexpr std::size_t kMaximumTotalCount = 512;

    Vector<bool> Booleans{};
    Vector<uint32_t> Integers{};
    Vector<float> Floats{};
    // Identifies the exact ordered variable lists, not merely their counts.
    std::uint64_t DescriptorDigest{};
    std::uint16_t DirectionFloatIndex{};

    bool operator==(const AnimationVariables& acRhs) const noexcept;
    bool operator!=(const AnimationVariables& acRhs) const noexcept;

    void Load(std::istream&);
    void Save(std::ostream&) const;

    void GenerateDiff(const AnimationVariables& aPrevious, TiltedPhoques::Buffer::Writer& aWriter) const;
    [[nodiscard]] bool ApplyDiff(TiltedPhoques::Buffer::Reader& aReader) noexcept;
    void VectorBool_to_String(const Vector<bool>& bools, TiltedPhoques::String& chars) const;
    void String_to_VectorBool(const TiltedPhoques::String& chars, Vector<bool>& bools);

    bool IsDecodedValid{true};
};
