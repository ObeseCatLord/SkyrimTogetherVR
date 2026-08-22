#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace SkyrimTogetherVR::HiggsSpatialReplayPolicy
{
constexpr float kMaximumTransformMagnitude = 1000000.0F;
constexpr float kMinimumTransformScale = 0.001F;
constexpr float kMaximumTransformScale = 1000.0F;
constexpr std::size_t kChunkCount = 4;
constexpr std::uint8_t kCompleteChunkMask = (1u << kChunkCount) - 1u;
constexpr std::uint32_t kHasHand = 1u << 0;
constexpr std::uint32_t kTwoHanding = 1u << 1;
constexpr std::uint32_t kLeftHand = 1u << 4;
constexpr std::uint32_t kSpatialBegin = 1u << 5;
constexpr std::uint32_t kSpatialChunk = 1u << 6;
constexpr std::uint32_t kSpatialNode = 1u << 9;
constexpr std::uint32_t kSpatialRebase = 1u << 10;
constexpr std::uint32_t kChunkIndexShift = 7;
constexpr std::uint32_t kChunkIndexMask = 0x3u << kChunkIndexShift;
constexpr std::uint32_t kKnownFlags =
    kHasHand | kTwoHanding | kLeftHand | kSpatialBegin | kSpatialChunk | kSpatialNode | kSpatialRebase | kChunkIndexMask;
constexpr std::size_t kNodeBytesPerChunk = 16;
constexpr std::size_t kMaximumNodeBytes = 48;

struct Vector3
{
    float X{};
    float Y{};
    float Z{};
};

struct Matrix3
{
    std::array<Vector3, 3> Rows{{
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
    }};
};

struct Transform
{
    Vector3 Translate{};
    Matrix3 Rotate{};
    float Scale{1.0F};
};

struct Chunk
{
    std::array<float, 4> Lanes{};
};

struct Transaction
{
    Transform Relative{};
    std::uint32_t Sequence{};
    std::uint8_t NextChunk{};
    bool IsLeft{};
    bool Active{};
    std::array<char, kMaximumNodeBytes> NodeName{};
    std::uint8_t NodeNameLength{};
    std::uint8_t NextNodeChunk{};
};

enum class AppendResult
{
    Rejected,
    Accepted,
    Complete,
};

[[nodiscard]] constexpr bool HasOnlyKnownFlags(const std::uint32_t aFlags) noexcept
{
    return (aFlags & ~kKnownFlags) == 0;
}

[[nodiscard]] constexpr bool IsSpatialBegin(const std::uint32_t aFlags) noexcept
{
    return HasOnlyKnownFlags(aFlags) && (aFlags & (kHasHand | kSpatialBegin)) == (kHasHand | kSpatialBegin) &&
           (aFlags & (kSpatialChunk | kSpatialNode | kChunkIndexMask)) == 0;
}

[[nodiscard]] constexpr bool IsSpatialChunk(const std::uint32_t aFlags) noexcept
{
    return HasOnlyKnownFlags(aFlags) && (aFlags & (kHasHand | kSpatialChunk)) == (kHasHand | kSpatialChunk) &&
           (aFlags & (kSpatialBegin | kSpatialNode)) == 0;
}

[[nodiscard]] constexpr bool IsSpatialNode(const std::uint32_t aFlags) noexcept
{
    return HasOnlyKnownFlags(aFlags) && (aFlags & (kHasHand | kSpatialNode)) == (kHasHand | kSpatialNode) &&
           (aFlags & (kSpatialBegin | kSpatialChunk)) == 0;
}

[[nodiscard]] constexpr std::uint32_t ChunkIndex(const std::uint32_t aFlags) noexcept
{
    return (aFlags & kChunkIndexMask) >> kChunkIndexShift;
}

[[nodiscard]] inline bool IsFinite(const float aValue) noexcept
{
    return std::isfinite(aValue);
}

[[nodiscard]] inline bool IsFinite(const Vector3& acValue) noexcept
{
    return IsFinite(acValue.X) && IsFinite(acValue.Y) && IsFinite(acValue.Z);
}

[[nodiscard]] inline float Dot(const Vector3& acLeft, const Vector3& acRight) noexcept
{
    return acLeft.X * acRight.X + acLeft.Y * acRight.Y + acLeft.Z * acRight.Z;
}

[[nodiscard]] inline Vector3 Multiply(const Matrix3& acMatrix, const Vector3& acValue) noexcept
{
    return {Dot(acMatrix.Rows[0], acValue), Dot(acMatrix.Rows[1], acValue), Dot(acMatrix.Rows[2], acValue)};
}

[[nodiscard]] inline Matrix3 Multiply(const Matrix3& acLeft, const Matrix3& acRight) noexcept
{
    Matrix3 result{};
    for (std::size_t row = 0; row < result.Rows.size(); ++row) {
        result.Rows[row] = {
            acLeft.Rows[row].X * acRight.Rows[0].X + acLeft.Rows[row].Y * acRight.Rows[1].X +
                acLeft.Rows[row].Z * acRight.Rows[2].X,
            acLeft.Rows[row].X * acRight.Rows[0].Y + acLeft.Rows[row].Y * acRight.Rows[1].Y +
                acLeft.Rows[row].Z * acRight.Rows[2].Y,
            acLeft.Rows[row].X * acRight.Rows[0].Z + acLeft.Rows[row].Y * acRight.Rows[1].Z +
                acLeft.Rows[row].Z * acRight.Rows[2].Z,
        };
    }
    return result;
}

[[nodiscard]] inline Matrix3 Transpose(const Matrix3& acMatrix) noexcept
{
    Matrix3 result{};
    result.Rows[0] = {acMatrix.Rows[0].X, acMatrix.Rows[1].X, acMatrix.Rows[2].X};
    result.Rows[1] = {acMatrix.Rows[0].Y, acMatrix.Rows[1].Y, acMatrix.Rows[2].Y};
    result.Rows[2] = {acMatrix.Rows[0].Z, acMatrix.Rows[1].Z, acMatrix.Rows[2].Z};
    return result;
}

[[nodiscard]] inline bool IsOrthonormal(const Matrix3& acMatrix) noexcept
{
    constexpr float epsilon = 0.01F;
    const auto& x = acMatrix.Rows[0];
    const auto& y = acMatrix.Rows[1];
    const auto& z = acMatrix.Rows[2];
    return IsFinite(x) && IsFinite(y) && IsFinite(z) && std::abs(Dot(x, x) - 1.0F) <= epsilon &&
           std::abs(Dot(y, y) - 1.0F) <= epsilon && std::abs(Dot(z, z) - 1.0F) <= epsilon &&
           std::abs(Dot(x, y)) <= epsilon && std::abs(Dot(x, z)) <= epsilon && std::abs(Dot(y, z)) <= epsilon;
}

[[nodiscard]] inline bool IsSafeTransform(const Transform& acTransform) noexcept
{
    return IsFinite(acTransform.Translate) && std::abs(acTransform.Translate.X) <= kMaximumTransformMagnitude &&
           std::abs(acTransform.Translate.Y) <= kMaximumTransformMagnitude &&
           std::abs(acTransform.Translate.Z) <= kMaximumTransformMagnitude && IsFinite(acTransform.Scale) &&
           acTransform.Scale >= kMinimumTransformScale && acTransform.Scale <= kMaximumTransformScale &&
           IsOrthonormal(acTransform.Rotate);
}

[[nodiscard]] inline bool IsFinite(const Chunk& acChunk) noexcept
{
    for (const auto lane : acChunk.Lanes) {
        if (!IsFinite(lane))
            return false;
    }
    return true;
}

[[nodiscard]] inline std::array<Chunk, kChunkCount> MakeChunks(const Transform& acTransform) noexcept
{
    std::array<Chunk, kChunkCount> result{};
    result[0].Lanes = {acTransform.Translate.X, acTransform.Translate.Y, acTransform.Translate.Z, acTransform.Scale};
    for (std::size_t index = 1; index < result.size(); ++index)
        result[index].Lanes = {acTransform.Rotate.Rows[index - 1].X, acTransform.Rotate.Rows[index - 1].Y,
                               acTransform.Rotate.Rows[index - 1].Z, 0.0F};
    return result;
}

inline void Begin(Transaction& arTransaction, const std::uint32_t aSequence, const bool aIsLeft,
                  const std::uint8_t aNodeNameLength = 0) noexcept
{
    arTransaction = {};
    arTransaction.Sequence = aSequence;
    arTransaction.IsLeft = aIsLeft;
    arTransaction.NodeNameLength = aNodeNameLength < kMaximumNodeBytes ? aNodeNameLength : 0;
    arTransaction.Active = aSequence != 0;
}

[[nodiscard]] inline bool AppendNode(Transaction& arTransaction, const std::uint32_t aSequence,
                                     const bool aIsLeft, const std::uint32_t aIndex,
                                     const std::array<char, kNodeBytesPerChunk>& acBytes) noexcept
{
    const auto required = (arTransaction.NodeNameLength + kNodeBytesPerChunk - 1) / kNodeBytesPerChunk;
    if (!arTransaction.Active || arTransaction.Sequence != aSequence || arTransaction.IsLeft != aIsLeft ||
        aIndex != arTransaction.NextNodeChunk || aIndex >= required)
        return false;
    const auto offset = static_cast<std::size_t>(aIndex) * kNodeBytesPerChunk;
    const auto count = std::min<std::size_t>(kNodeBytesPerChunk, arTransaction.NodeNameLength - offset);
    for (std::size_t index = 0; index < count; ++index)
        arTransaction.NodeName[offset + index] = acBytes[index];
    ++arTransaction.NextNodeChunk;
    return true;
}

inline void Cancel(Transaction& arTransaction) noexcept
{
    arTransaction = {};
}

inline void ClearForDrop(Transaction& arTransaction) noexcept
{
    Cancel(arTransaction);
}

[[nodiscard]] inline AppendResult Append(Transaction& arTransaction, const std::uint32_t aSequence,
                                          const bool aIsLeft, const std::uint32_t aIndex,
                                          const Chunk& acChunk) noexcept
{
    if (!arTransaction.Active || arTransaction.Sequence != aSequence || arTransaction.IsLeft != aIsLeft ||
        aIndex >= kChunkCount || aIndex != arTransaction.NextChunk || !IsFinite(acChunk) ||
        arTransaction.NextNodeChunk != (arTransaction.NodeNameLength + kNodeBytesPerChunk - 1) / kNodeBytesPerChunk)
        return AppendResult::Rejected;

    switch (aIndex) {
    case 0:
        arTransaction.Relative.Translate = {acChunk.Lanes[0], acChunk.Lanes[1], acChunk.Lanes[2]};
        arTransaction.Relative.Scale = acChunk.Lanes[3];
        break;
    case 1:
    case 2:
    case 3:
        arTransaction.Relative.Rotate.Rows[aIndex - 1] = {acChunk.Lanes[0], acChunk.Lanes[1], acChunk.Lanes[2]};
        break;
    default: return AppendResult::Rejected;
    }
    ++arTransaction.NextChunk;
    return arTransaction.NextChunk == kChunkCount ? AppendResult::Complete : AppendResult::Accepted;
}

[[nodiscard]] inline Transform ComposeHandRelative(const Transform& acHandWorld,
                                                    const Transform& acRelative) noexcept
{
    Transform world{};
    world.Rotate = Multiply(acHandWorld.Rotate, acRelative.Rotate);
    world.Scale = acHandWorld.Scale * acRelative.Scale;
    const auto relativeTranslate = Vector3{acRelative.Translate.X * acHandWorld.Scale,
                                           acRelative.Translate.Y * acHandWorld.Scale,
                                           acRelative.Translate.Z * acHandWorld.Scale};
    const auto rotated = Multiply(acHandWorld.Rotate, relativeTranslate);
    world.Translate = {rotated.X + acHandWorld.Translate.X, rotated.Y + acHandWorld.Translate.Y,
                       rotated.Z + acHandWorld.Translate.Z};
    return world;
}

[[nodiscard]] inline Transform ToParentLocal(const Transform& acParentWorld, const Transform& acWorld) noexcept
{
    Transform local{};
    const auto inverseRotate = Transpose(acParentWorld.Rotate);
    const auto inverseScale = 1.0F / acParentWorld.Scale;
    const auto inverseTranslate = Multiply(
        inverseRotate, Vector3{-acParentWorld.Translate.X, -acParentWorld.Translate.Y, -acParentWorld.Translate.Z});
    const auto rotatedWorld = Multiply(inverseRotate, acWorld.Translate);
    local.Translate = {rotatedWorld.X * inverseScale + inverseTranslate.X * inverseScale,
                       rotatedWorld.Y * inverseScale + inverseTranslate.Y * inverseScale,
                       rotatedWorld.Z * inverseScale + inverseTranslate.Z * inverseScale};
    local.Rotate = Multiply(inverseRotate, acWorld.Rotate);
    local.Scale = acWorld.Scale * inverseScale;
    return local;
}

[[nodiscard]] inline Transform SolveObjectRootWorld(const Transform& acCurrentGrabbedNodeWorld,
                                                    const Transform& acCurrentObjectRootWorld,
                                                    const Transform& acDesiredGrabbedNodeWorld) noexcept
{
    const auto rootFromGrabbedNode = ToParentLocal(acCurrentGrabbedNodeWorld, acCurrentObjectRootWorld);
    return ComposeHandRelative(acDesiredGrabbedNodeWorld, rootFromGrabbedNode);
}
} // namespace SkyrimTogetherVR::HiggsSpatialReplayPolicy
