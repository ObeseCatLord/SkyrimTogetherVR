#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace SkyrimTogetherVR::AnimationGraphProtocol
{
inline constexpr std::uint16_t kDescriptorVersion = 2;
inline constexpr std::uint16_t kMaximumBooleanCount = 64;
inline constexpr std::uint16_t kMaximumFloatCount = 64;
inline constexpr std::uint16_t kMaximumIntegerCount = 64;
inline constexpr std::uint16_t kValuesPerChunk = 7;

struct DescriptorShape
{
    std::uint16_t BooleanCount{};
    std::uint16_t FloatCount{};
    std::uint16_t IntegerCount{};
    std::uint16_t DirectionFloatIndex{};

    [[nodiscard]] constexpr bool Matches(const std::size_t a_booleanCount, const std::size_t a_floatCount,
                                         const std::size_t a_integerCount) const noexcept
    {
        return BooleanCount == a_booleanCount && FloatCount == a_floatCount && IntegerCount == a_integerCount;
    }
};

#include "VRAnimationGraphShapes.generated.inc"

[[nodiscard]] constexpr const DescriptorShape* FindKnownShape(
    const std::size_t a_booleanCount, const std::size_t a_floatCount,
    const std::size_t a_integerCount) noexcept
{
    for (const auto& shape : kKnownDescriptorShapes)
        if (shape.Matches(a_booleanCount, a_floatCount, a_integerCount))
            return &shape;
    return nullptr;
}

[[nodiscard]] constexpr bool IsKnownShape(const std::size_t a_booleanCount, const std::size_t a_floatCount,
                                          const std::size_t a_integerCount) noexcept
{
    return FindKnownShape(a_booleanCount, a_floatCount, a_integerCount) != nullptr;
}

enum class ValueType : std::uint16_t { BooleanBits = 1, Float = 2, Integer = 3 };
enum ChunkFlag : std::uint32_t { FullSnapshot = 1u << 0 };
enum class ChunkAcceptResult : std::uint8_t { Accepted, Complete, Stale, Malformed };

[[nodiscard]] constexpr std::uint16_t MaximumCount(const ValueType a_type) noexcept
{
    switch (a_type) {
    case ValueType::BooleanBits: return kMaximumBooleanCount;
    case ValueType::Float: return kMaximumFloatCount;
    case ValueType::Integer: return kMaximumIntegerCount;
    default: return 0;
    }
}

[[nodiscard]] constexpr bool IsValidCount(const ValueType a_type, const std::size_t a_count) noexcept
{
    return a_count != 0 && a_count <= MaximumCount(a_type);
}

[[nodiscard]] constexpr std::uint32_t ExpectedChunkMask(const ValueType a_type, const std::uint16_t a_totalCount) noexcept
{
    if (!IsValidCount(a_type, a_totalCount)) return 0;
    if (a_type == ValueType::BooleanBits) return 1;
    const auto chunks = (a_totalCount + kValuesPerChunk - 1) / kValuesPerChunk;
    return (1u << chunks) - 1u;
}

[[nodiscard]] constexpr bool IsValidChunk(const ValueType a_type, const std::uint16_t a_startIndex,
                                          const std::uint16_t a_valueCount, const std::uint16_t a_totalCount) noexcept
{
    if (!IsValidCount(a_type, a_totalCount) || a_valueCount == 0 || a_startIndex >= a_totalCount) return false;
    if (a_type == ValueType::BooleanBits) return a_startIndex == 0 && a_valueCount == a_totalCount;
    return a_startIndex % kValuesPerChunk == 0 && a_valueCount <= kValuesPerChunk &&
           static_cast<std::uint32_t>(a_startIndex) + a_valueCount <= a_totalCount &&
           (a_valueCount == kValuesPerChunk || a_startIndex + a_valueCount == a_totalCount);
}

[[nodiscard]] inline bool AreChunkValuesValid(const ValueType a_type, const std::uint16_t a_valueCount,
                                               const std::uint16_t a_totalCount,
                                               const std::uint32_t (&a_values)[kValuesPerChunk]) noexcept
{
    if (a_type == ValueType::BooleanBits) {
        const auto words = (a_totalCount + 31u) / 32u;
        if (words == 0 || words > kValuesPerChunk) return false;
        if (a_totalCount % 32u != 0 && (a_values[words - 1] >> (a_totalCount % 32u)) != 0) return false;
        for (std::uint16_t i = words; i < kValuesPerChunk; ++i) if (a_values[i] != 0) return false;
        return true;
    }
    for (std::uint16_t i = 0; i < kValuesPerChunk; ++i) {
        if (i >= a_valueCount && a_values[i] != 0) return false;
        if (a_type == ValueType::Float && i < a_valueCount && !std::isfinite(std::bit_cast<float>(a_values[i]))) return false;
    }
    return true;
}

struct SnapshotBuffer
{
    std::uint64_t SnapshotId{};
    float Direction{};
    std::uint16_t BooleanCount{};
    std::uint16_t FloatCount{};
    std::uint16_t IntegerCount{};
    std::array<bool, kMaximumBooleanCount> Booleans{};
    std::array<float, kMaximumFloatCount> Floats{};
    std::array<std::int32_t, kMaximumIntegerCount> Integers{};
    std::uint32_t BooleanChunkMask{};
    std::uint32_t FloatChunkMask{};
    std::uint32_t IntegerChunkMask{};

    [[nodiscard]] constexpr std::uint16_t Count(const ValueType a_type) const noexcept
    {
        switch (a_type) {
        case ValueType::BooleanBits: return BooleanCount;
        case ValueType::Float: return FloatCount;
        case ValueType::Integer: return IntegerCount;
        default: return 0;
        }
    }
    [[nodiscard]] constexpr bool IsComplete() const noexcept
    {
        return SnapshotId != 0 && IsValidCount(ValueType::BooleanBits, BooleanCount) &&
               IsValidCount(ValueType::Float, FloatCount) && IsValidCount(ValueType::Integer, IntegerCount) &&
               IsKnownShape(BooleanCount, FloatCount, IntegerCount) &&
               BooleanChunkMask == ExpectedChunkMask(ValueType::BooleanBits, BooleanCount) &&
               FloatChunkMask == ExpectedChunkMask(ValueType::Float, FloatCount) &&
               IntegerChunkMask == ExpectedChunkMask(ValueType::Integer, IntegerCount);
    }
};

[[nodiscard]] inline ChunkAcceptResult AcceptChunk(SnapshotBuffer& a_snapshot, const std::uint64_t a_snapshotId,
    const ValueType a_type, const std::uint16_t a_startIndex, const std::uint16_t a_valueCount,
    const std::uint16_t a_totalCount, const float a_direction,
    const std::uint32_t (&a_values)[kValuesPerChunk]) noexcept
{
    if (a_snapshotId == 0) return ChunkAcceptResult::Malformed;
    if (a_snapshotId < a_snapshot.SnapshotId) return ChunkAcceptResult::Stale;
    const bool newer = a_snapshotId > a_snapshot.SnapshotId;
    if (!std::isfinite(a_direction) || !IsValidChunk(a_type, a_startIndex, a_valueCount, a_totalCount) ||
        !AreChunkValuesValid(a_type, a_valueCount, a_totalCount, a_values)) return ChunkAcceptResult::Malformed;
    const auto existingCount = newer ? 0 :
        (a_type == ValueType::BooleanBits ? a_snapshot.BooleanCount :
         a_type == ValueType::Float ? a_snapshot.FloatCount : a_snapshot.IntegerCount);
    if (existingCount != 0 && existingCount != a_totalCount) return ChunkAcceptResult::Malformed;
    const auto booleanCount = a_type == ValueType::BooleanBits ? a_totalCount :
                              (newer ? 0 : a_snapshot.BooleanCount);
    const auto floatCount = a_type == ValueType::Float ? a_totalCount :
                            (newer ? 0 : a_snapshot.FloatCount);
    const auto integerCount = a_type == ValueType::Integer ? a_totalCount :
                              (newer ? 0 : a_snapshot.IntegerCount);
    if (booleanCount != 0 && floatCount != 0 && integerCount != 0 &&
        !IsKnownShape(booleanCount, floatCount, integerCount))
        return ChunkAcceptResult::Malformed;
    if (newer) a_snapshot = {};
    if (newer) { a_snapshot.SnapshotId = a_snapshotId; a_snapshot.Direction = a_direction; }
    if (std::bit_cast<std::uint32_t>(a_snapshot.Direction) != std::bit_cast<std::uint32_t>(a_direction))
        return ChunkAcceptResult::Malformed;
    auto& count = a_type == ValueType::BooleanBits ? a_snapshot.BooleanCount :
                  a_type == ValueType::Float ? a_snapshot.FloatCount : a_snapshot.IntegerCount;
    count = a_totalCount;
    auto& mask = a_type == ValueType::BooleanBits ? a_snapshot.BooleanChunkMask :
                 a_type == ValueType::Float ? a_snapshot.FloatChunkMask : a_snapshot.IntegerChunkMask;
    const auto bit = a_type == ValueType::BooleanBits ? 1u : 1u << (a_startIndex / kValuesPerChunk);
    if ((mask & bit) != 0) return ChunkAcceptResult::Malformed;
    if (a_type == ValueType::BooleanBits) {
        for (std::uint16_t i = 0; i < a_valueCount; ++i) a_snapshot.Booleans[i] = (a_values[i / 32] & (1u << (i % 32))) != 0;
    } else if (a_type == ValueType::Float) {
        for (std::uint16_t i = 0; i < a_valueCount; ++i) a_snapshot.Floats[a_startIndex + i] = std::bit_cast<float>(a_values[i]);
    } else if (a_type == ValueType::Integer) {
        for (std::uint16_t i = 0; i < a_valueCount; ++i) a_snapshot.Integers[a_startIndex + i] = std::bit_cast<std::int32_t>(a_values[i]);
    } else return ChunkAcceptResult::Malformed;
    mask |= bit;
    return a_snapshot.IsComplete() ? ChunkAcceptResult::Complete : ChunkAcceptResult::Accepted;
}
} // namespace SkyrimTogetherVR::AnimationGraphProtocol
