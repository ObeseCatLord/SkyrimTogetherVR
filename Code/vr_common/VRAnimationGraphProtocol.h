#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace SkyrimTogetherVR::AnimationGraphProtocol
{
inline constexpr std::uint16_t kDescriptorVersion = 1;
inline constexpr std::uint16_t kBooleanCount = 60;
inline constexpr std::uint16_t kFloatCount = 13;
inline constexpr std::uint16_t kIntegerCount = 14;
inline constexpr std::uint16_t kValuesPerChunk = 7;

enum class ValueType : std::uint16_t
{
    BooleanBits = 1,
    Float = 2,
    Integer = 3,
};

enum ChunkFlag : std::uint32_t
{
    FullSnapshot = 1u << 0,
};

enum class ChunkAcceptResult : std::uint8_t
{
    Accepted,
    Complete,
    Stale,
    Malformed,
};

struct SnapshotBuffer
{
    std::uint64_t SnapshotId{};
    float Direction{};
    std::array<bool, kBooleanCount> Booleans{};
    std::array<float, kFloatCount> Floats{};
    std::array<std::int32_t, kIntegerCount> Integers{};
    std::uint32_t BooleanChunkMask{};
    std::uint32_t FloatChunkMask{};
    std::uint32_t IntegerChunkMask{};

    [[nodiscard]] constexpr bool IsComplete() const noexcept
    {
        return SnapshotId != 0 && BooleanChunkMask == 1 && FloatChunkMask == 3 && IntegerChunkMask == 3;
    }
};

[[nodiscard]] constexpr std::uint16_t ExpectedCount(const ValueType a_type) noexcept
{
    switch (a_type)
    {
    case ValueType::BooleanBits:
        return kBooleanCount;
    case ValueType::Float:
        return kFloatCount;
    case ValueType::Integer:
        return kIntegerCount;
    default:
        return 0;
    }
}

[[nodiscard]] constexpr bool IsValidChunk(
    const ValueType a_type,
    const std::uint16_t a_startIndex,
    const std::uint16_t a_valueCount,
    const std::uint16_t a_totalCount) noexcept
{
    const auto expected = ExpectedCount(a_type);
    if (expected == 0 || a_totalCount != expected || a_valueCount == 0 || a_startIndex >= expected)
        return false;

    if (a_type == ValueType::BooleanBits)
        return a_startIndex == 0 && a_valueCount == expected;

    if (a_startIndex % kValuesPerChunk != 0 || a_valueCount > kValuesPerChunk ||
        static_cast<std::uint32_t>(a_startIndex) + a_valueCount > expected)
        return false;
    return a_valueCount == kValuesPerChunk || a_startIndex + a_valueCount == expected;
}

[[nodiscard]] constexpr std::uint32_t ExpectedChunkMask(const ValueType a_type) noexcept
{
    if (a_type == ValueType::BooleanBits)
        return 1;
    const auto count = ExpectedCount(a_type);
    const auto chunks = (count + kValuesPerChunk - 1) / kValuesPerChunk;
    return (1u << chunks) - 1u;
}

[[nodiscard]] inline bool AreChunkValuesValid(
    const ValueType a_type,
    const std::uint16_t a_valueCount,
    const std::uint32_t (&a_values)[kValuesPerChunk]) noexcept
{
    if (a_type != ValueType::Float)
        return true;
    for (std::uint16_t index = 0; index < a_valueCount; ++index) {
        if (!std::isfinite(std::bit_cast<float>(a_values[index])))
            return false;
    }
    return true;
}

[[nodiscard]] inline ChunkAcceptResult AcceptChunk(
    SnapshotBuffer& a_snapshot,
    const std::uint64_t a_snapshotId,
    const ValueType a_type,
    const std::uint16_t a_startIndex,
    const std::uint16_t a_valueCount,
    const std::uint16_t a_totalCount,
    const float a_direction,
    const std::uint32_t (&a_values)[kValuesPerChunk]) noexcept
{
    if (a_snapshotId == 0 || !std::isfinite(a_direction) ||
        !IsValidChunk(a_type, a_startIndex, a_valueCount, a_totalCount) ||
        !AreChunkValuesValid(a_type, a_valueCount, a_values))
        return ChunkAcceptResult::Malformed;
    if (a_snapshotId < a_snapshot.SnapshotId)
        return ChunkAcceptResult::Stale;
    if (a_snapshotId > a_snapshot.SnapshotId) {
        a_snapshot = {};
        a_snapshot.SnapshotId = a_snapshotId;
        a_snapshot.Direction = a_direction;
    }
    if (std::bit_cast<std::uint32_t>(a_snapshot.Direction) != std::bit_cast<std::uint32_t>(a_direction))
        return ChunkAcceptResult::Malformed;

    if (a_type == ValueType::BooleanBits) {
        for (std::uint16_t index = 0; index < a_valueCount; ++index)
            a_snapshot.Booleans[index] = (a_values[index / 32] & (1u << (index % 32))) != 0;
        a_snapshot.BooleanChunkMask = 1;
    } else if (a_type == ValueType::Float) {
        for (std::uint16_t index = 0; index < a_valueCount; ++index) {
            const auto value = std::bit_cast<float>(a_values[index]);
            if (!std::isfinite(value))
                return ChunkAcceptResult::Malformed;
            a_snapshot.Floats[a_startIndex + index] = value;
        }
        a_snapshot.FloatChunkMask |= 1u << (a_startIndex / kValuesPerChunk);
    } else if (a_type == ValueType::Integer) {
        for (std::uint16_t index = 0; index < a_valueCount; ++index)
            a_snapshot.Integers[a_startIndex + index] = std::bit_cast<std::int32_t>(a_values[index]);
        a_snapshot.IntegerChunkMask |= 1u << (a_startIndex / kValuesPerChunk);
    } else {
        return ChunkAcceptResult::Malformed;
    }
    return a_snapshot.IsComplete() ? ChunkAcceptResult::Complete : ChunkAcceptResult::Accepted;
}
} // namespace SkyrimTogetherVR::AnimationGraphProtocol
