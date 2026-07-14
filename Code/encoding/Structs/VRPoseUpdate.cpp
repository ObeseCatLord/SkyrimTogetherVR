#include <Structs/VRPoseUpdate.h>
#include <TiltedCore/Serialization.hpp>

#include <cmath>

namespace
{
constexpr float kMaxPosePositionComponent = 1000000.0f;
constexpr float kMaxPoseBasisLength = 4.0f;
constexpr float kMinPoseBasisLength = 0.2f;
constexpr float kMaxPoseBasisDot = 0.35f;
constexpr float kMinPoseScale = 0.01f;
constexpr float kMaxPoseScale = 100.0f;
constexpr float kMaxVrikOffsetComponent = 1000.0f;
constexpr float kMaxFingerCurl = 1.0f;
constexpr uint32_t kBodyPoseFormatVersionCurrent = 1;
constexpr float kMaxBodyPelvisPositionComponent = 500.0f;
constexpr float kMaxBodyLimbPositionComponent = 0.05f;
constexpr float kBodyPoseMinScale = 0.95f;
constexpr float kBodyPoseMaxScale = 1.05f;
constexpr float kBodyPoseMinBasisLength = 0.95f;
constexpr float kBodyPoseMaxBasisLength = 1.05f;
constexpr float kBodyPoseMaxBasisDot = 0.05f;
constexpr float kBodyPoseMinDeterminant = 0.95f;

bool IsFiniteVector(const glm::vec3& acValue) noexcept
{
    return std::isfinite(acValue.x) && std::isfinite(acValue.y) && std::isfinite(acValue.z);
}

bool IsBoundedVector(const glm::vec3& acValue, float aMaxAbsComponent) noexcept
{
    return std::abs(acValue.x) <= aMaxAbsComponent && std::abs(acValue.y) <= aMaxAbsComponent &&
           std::abs(acValue.z) <= aMaxAbsComponent;
}

float Dot(const glm::vec3& acLeft, const glm::vec3& acRight) noexcept
{
    return acLeft.x * acRight.x + acLeft.y * acRight.y + acLeft.z * acRight.z;
}

glm::vec3 Cross(const glm::vec3& acLeft, const glm::vec3& acRight) noexcept
{
    return {
        acLeft.y * acRight.z - acLeft.z * acRight.y,
        acLeft.z * acRight.x - acLeft.x * acRight.z,
        acLeft.x * acRight.y - acLeft.y * acRight.x,
    };
}

bool IsPoseNodeSafe(const VRPoseNodeData& acPose) noexcept
{
    if (!acPose.Valid)
        return true;
    if (!IsFiniteVector(acPose.Position) || !IsFiniteVector(acPose.AxisX) || !IsFiniteVector(acPose.AxisY) ||
        !IsFiniteVector(acPose.AxisZ) || !std::isfinite(acPose.Scale))
        return false;
    if (!IsBoundedVector(acPose.Position, kMaxPosePositionComponent) ||
        !IsBoundedVector(acPose.AxisX, kMaxPoseBasisLength) || !IsBoundedVector(acPose.AxisY, kMaxPoseBasisLength) ||
        !IsBoundedVector(acPose.AxisZ, kMaxPoseBasisLength))
        return false;

    const auto minLengthSquared = kMinPoseBasisLength * kMinPoseBasisLength;
    const auto maxLengthSquared = kMaxPoseBasisLength * kMaxPoseBasisLength;
    const auto xLengthSquared = Dot(acPose.AxisX, acPose.AxisX);
    const auto yLengthSquared = Dot(acPose.AxisY, acPose.AxisY);
    const auto zLengthSquared = Dot(acPose.AxisZ, acPose.AxisZ);
    if (xLengthSquared < minLengthSquared || xLengthSquared > maxLengthSquared ||
        yLengthSquared < minLengthSquared || yLengthSquared > maxLengthSquared ||
        zLengthSquared < minLengthSquared || zLengthSquared > maxLengthSquared)
        return false;
    if (std::abs(Dot(acPose.AxisX, acPose.AxisY)) > kMaxPoseBasisDot ||
        std::abs(Dot(acPose.AxisX, acPose.AxisZ)) > kMaxPoseBasisDot ||
        std::abs(Dot(acPose.AxisY, acPose.AxisZ)) > kMaxPoseBasisDot)
        return false;
    return acPose.Scale >= kMinPoseScale && acPose.Scale <= kMaxPoseScale;
}

bool IsBodyNodeSafe(const VRPoseNodeData& acPose, float aMaxPositionComponent) noexcept
{
    if (!acPose.Valid || !IsFiniteVector(acPose.Position) || !IsFiniteVector(acPose.AxisX) ||
        !IsFiniteVector(acPose.AxisY) || !IsFiniteVector(acPose.AxisZ) || !std::isfinite(acPose.Scale))
        return false;
    if (!IsBoundedVector(acPose.Position, aMaxPositionComponent) ||
        acPose.Scale < kBodyPoseMinScale || acPose.Scale > kBodyPoseMaxScale)
        return false;

    const auto minLengthSquared = kBodyPoseMinBasisLength * kBodyPoseMinBasisLength;
    const auto maxLengthSquared = kBodyPoseMaxBasisLength * kBodyPoseMaxBasisLength;
    const auto xLengthSquared = Dot(acPose.AxisX, acPose.AxisX);
    const auto yLengthSquared = Dot(acPose.AxisY, acPose.AxisY);
    const auto zLengthSquared = Dot(acPose.AxisZ, acPose.AxisZ);
    if (xLengthSquared < minLengthSquared || xLengthSquared > maxLengthSquared ||
        yLengthSquared < minLengthSquared || yLengthSquared > maxLengthSquared ||
        zLengthSquared < minLengthSquared || zLengthSquared > maxLengthSquared)
        return false;
    if (std::abs(Dot(acPose.AxisX, acPose.AxisY)) > kBodyPoseMaxBasisDot ||
        std::abs(Dot(acPose.AxisX, acPose.AxisZ)) > kBodyPoseMaxBasisDot ||
        std::abs(Dot(acPose.AxisY, acPose.AxisZ)) > kBodyPoseMaxBasisDot)
        return false;
    return Dot(Cross(acPose.AxisX, acPose.AxisY), acPose.AxisZ) >= kBodyPoseMinDeterminant;
}

bool IsBodyPoseZeroState(const VRBodyPoseData& acBody) noexcept
{
    return !acBody.Valid && acBody.CaptureSequence == 0 && acBody.RootGeneration == 0 && !acBody.Pelvis.Valid &&
           !acBody.LeftThigh.Valid && !acBody.LeftCalf.Valid && !acBody.LeftFoot.Valid && !acBody.RightThigh.Valid &&
           !acBody.RightCalf.Valid && !acBody.RightFoot.Valid;
}

bool IsFingerCurlSafe(const VRFingerCurlData& acFingers) noexcept
{
    if (!acFingers.Valid)
        return true;
    return std::isfinite(acFingers.Thumb) && std::isfinite(acFingers.Index) && std::isfinite(acFingers.Middle) &&
           std::isfinite(acFingers.Ring) && std::isfinite(acFingers.Pinky) &&
           acFingers.Thumb >= 0.0f && acFingers.Thumb <= kMaxFingerCurl &&
           acFingers.Index >= 0.0f && acFingers.Index <= kMaxFingerCurl &&
           acFingers.Middle >= 0.0f && acFingers.Middle <= kMaxFingerCurl &&
           acFingers.Ring >= 0.0f && acFingers.Ring <= kMaxFingerCurl &&
           acFingers.Pinky >= 0.0f && acFingers.Pinky <= kMaxFingerCurl;
}

bool AreVrikOffsetsSafe(const VRVrikData& acVrik) noexcept
{
    if (!acVrik.CameraOffsetsValid)
        return true;
    return IsFiniteVector(acVrik.CameraOffset) && IsFiniteVector(acVrik.FinalCameraOffset) &&
           IsFiniteVector(acVrik.FinalSmoothingOffset) && IsBoundedVector(acVrik.CameraOffset, kMaxVrikOffsetComponent) &&
           IsBoundedVector(acVrik.FinalCameraOffset, kMaxVrikOffsetComponent) &&
           IsBoundedVector(acVrik.FinalSmoothingOffset, kMaxVrikOffsetComponent);
}

void SerializeVector3(TiltedPhoques::Buffer::Writer& aWriter, const glm::vec3& acValue) noexcept
{
    TiltedPhoques::Serialization::WriteFloat(aWriter, acValue.x);
    TiltedPhoques::Serialization::WriteFloat(aWriter, acValue.y);
    TiltedPhoques::Serialization::WriteFloat(aWriter, acValue.z);
}

void DeserializeVector3(TiltedPhoques::Buffer::Reader& aReader, glm::vec3& aValue) noexcept
{
    aValue.x = TiltedPhoques::Serialization::ReadFloat(aReader);
    aValue.y = TiltedPhoques::Serialization::ReadFloat(aReader);
    aValue.z = TiltedPhoques::Serialization::ReadFloat(aReader);
}
}

bool VRPoseNodeData::operator==(const VRPoseNodeData& acRhs) const noexcept
{
    if (Valid != acRhs.Valid)
        return false;

    if (!Valid)
        return true;

    return Position == acRhs.Position && AxisX == acRhs.AxisX && AxisY == acRhs.AxisY &&
           AxisZ == acRhs.AxisZ && Scale == acRhs.Scale;
}

bool VRPoseNodeData::operator!=(const VRPoseNodeData& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRPoseNodeData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteBool(aWriter, Valid);
    if (!Valid)
        return;

    SerializeVector3(aWriter, Position);
    SerializeVector3(aWriter, AxisX);
    SerializeVector3(aWriter, AxisY);
    SerializeVector3(aWriter, AxisZ);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Scale);
}

void VRPoseNodeData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Valid = TiltedPhoques::Serialization::ReadBool(aReader);
    if (!Valid)
    {
        Position.x = 0.0f;
        Position.y = 0.0f;
        Position.z = 0.0f;
        AxisX = {1.0f, 0.0f, 0.0f};
        AxisY = {0.0f, 1.0f, 0.0f};
        AxisZ = {0.0f, 0.0f, 1.0f};
        Scale = 1.0f;
        return;
    }

    DeserializeVector3(aReader, Position);
    DeserializeVector3(aReader, AxisX);
    DeserializeVector3(aReader, AxisY);
    DeserializeVector3(aReader, AxisZ);
    Scale = TiltedPhoques::Serialization::ReadFloat(aReader);
}

bool VRFingerCurlData::operator==(const VRFingerCurlData& acRhs) const noexcept
{
    if (Valid != acRhs.Valid)
        return false;

    if (!Valid)
        return true;

    return Thumb == acRhs.Thumb && Index == acRhs.Index && Middle == acRhs.Middle &&
           Ring == acRhs.Ring && Pinky == acRhs.Pinky;
}

bool VRFingerCurlData::operator!=(const VRFingerCurlData& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRFingerCurlData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteBool(aWriter, Valid);
    if (!Valid)
        return;

    TiltedPhoques::Serialization::WriteFloat(aWriter, Thumb);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Index);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Middle);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Ring);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Pinky);
}

void VRFingerCurlData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Valid = TiltedPhoques::Serialization::ReadBool(aReader);
    if (!Valid)
    {
        Thumb = 0.0f;
        Index = 0.0f;
        Middle = 0.0f;
        Ring = 0.0f;
        Pinky = 0.0f;
        return;
    }

    Thumb = TiltedPhoques::Serialization::ReadFloat(aReader);
    Index = TiltedPhoques::Serialization::ReadFloat(aReader);
    Middle = TiltedPhoques::Serialization::ReadFloat(aReader);
    Ring = TiltedPhoques::Serialization::ReadFloat(aReader);
    Pinky = TiltedPhoques::Serialization::ReadFloat(aReader);
}

bool VRVrikData::operator==(const VRVrikData& acRhs) const noexcept
{
    if (Detected != acRhs.Detected || InterfaceAvailable != acRhs.InterfaceAvailable ||
        LeftFingers != acRhs.LeftFingers || RightFingers != acRhs.RightFingers ||
        CameraOffsetsValid != acRhs.CameraOffsetsValid)
        return false;

    if (!CameraOffsetsValid)
        return true;

    return CameraOffset == acRhs.CameraOffset && FinalCameraOffset == acRhs.FinalCameraOffset &&
           FinalSmoothingOffset == acRhs.FinalSmoothingOffset;
}

bool VRVrikData::operator!=(const VRVrikData& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRVrikData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteBool(aWriter, Detected);
    TiltedPhoques::Serialization::WriteBool(aWriter, InterfaceAvailable);
    LeftFingers.Serialize(aWriter);
    RightFingers.Serialize(aWriter);

    TiltedPhoques::Serialization::WriteBool(aWriter, CameraOffsetsValid);
    if (!CameraOffsetsValid)
        return;

    SerializeVector3(aWriter, CameraOffset);
    SerializeVector3(aWriter, FinalCameraOffset);
    SerializeVector3(aWriter, FinalSmoothingOffset);
}

void VRVrikData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Detected = TiltedPhoques::Serialization::ReadBool(aReader);
    InterfaceAvailable = TiltedPhoques::Serialization::ReadBool(aReader);
    LeftFingers.Deserialize(aReader);
    RightFingers.Deserialize(aReader);

    CameraOffsetsValid = TiltedPhoques::Serialization::ReadBool(aReader);
    if (!CameraOffsetsValid)
    {
        CameraOffset = {};
        FinalCameraOffset = {};
        FinalSmoothingOffset = {};
        return;
    }

    DeserializeVector3(aReader, CameraOffset);
    DeserializeVector3(aReader, FinalCameraOffset);
    DeserializeVector3(aReader, FinalSmoothingOffset);
}

bool VRBodyPoseData::operator==(const VRBodyPoseData& acRhs) const noexcept
{
    return FormatVersion == acRhs.FormatVersion && Valid == acRhs.Valid && CaptureSequence == acRhs.CaptureSequence &&
           RootGeneration == acRhs.RootGeneration && Pelvis == acRhs.Pelvis && LeftThigh == acRhs.LeftThigh &&
           LeftCalf == acRhs.LeftCalf && LeftFoot == acRhs.LeftFoot && RightThigh == acRhs.RightThigh &&
           RightCalf == acRhs.RightCalf && RightFoot == acRhs.RightFoot;
}

bool VRBodyPoseData::operator!=(const VRBodyPoseData& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRBodyPoseData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, FormatVersion);
    if (FormatVersion != 1)
        return;

    TiltedPhoques::Serialization::WriteBool(aWriter, Valid);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, CaptureSequence);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, RootGeneration);
    Pelvis.Serialize(aWriter);
    LeftThigh.Serialize(aWriter);
    LeftCalf.Serialize(aWriter);
    LeftFoot.Serialize(aWriter);
    RightThigh.Serialize(aWriter);
    RightCalf.Serialize(aWriter);
    RightFoot.Serialize(aWriter);
}

void VRBodyPoseData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    FormatVersion = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    if (FormatVersion != 1)
    {
        Valid = false;
        CaptureSequence = 0;
        RootGeneration = 0;
        Pelvis = {};
        LeftThigh = {};
        LeftCalf = {};
        LeftFoot = {};
        RightThigh = {};
        RightCalf = {};
        RightFoot = {};
        return;
    }

    Valid = TiltedPhoques::Serialization::ReadBool(aReader);
    CaptureSequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RootGeneration = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Pelvis.Deserialize(aReader);
    LeftThigh.Deserialize(aReader);
    LeftCalf.Deserialize(aReader);
    LeftFoot.Deserialize(aReader);
    RightThigh.Deserialize(aReader);
    RightCalf.Deserialize(aReader);
    RightFoot.Deserialize(aReader);
}

bool VRPoseUpdate::operator==(const VRPoseUpdate& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && Hmd == acRhs.Hmd && LeftHand == acRhs.LeftHand &&
           RightHand == acRhs.RightHand && SpellOrigin == acRhs.SpellOrigin &&
           SpellDestination == acRhs.SpellDestination && ArrowOrigin == acRhs.ArrowOrigin &&
           ArrowDestination == acRhs.ArrowDestination && BowAim == acRhs.BowAim &&
           BowRotation == acRhs.BowRotation && LeftWeaponOffset == acRhs.LeftWeaponOffset &&
           RightWeaponOffset == acRhs.RightWeaponOffset && PrimaryMagicOffset == acRhs.PrimaryMagicOffset &&
           PrimaryMagicAim == acRhs.PrimaryMagicAim && SecondaryMagicOffset == acRhs.SecondaryMagicOffset &&
           SecondaryMagicAim == acRhs.SecondaryMagicAim && Body == acRhs.Body && Vrik == acRhs.Vrik;
}

bool VRPoseUpdate::operator!=(const VRPoseUpdate& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRPoseUpdate::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    Hmd.Serialize(aWriter);
    LeftHand.Serialize(aWriter);
    RightHand.Serialize(aWriter);
    SpellOrigin.Serialize(aWriter);
    SpellDestination.Serialize(aWriter);
    ArrowOrigin.Serialize(aWriter);
    ArrowDestination.Serialize(aWriter);
    BowAim.Serialize(aWriter);
    BowRotation.Serialize(aWriter);
    LeftWeaponOffset.Serialize(aWriter);
    RightWeaponOffset.Serialize(aWriter);
    PrimaryMagicOffset.Serialize(aWriter);
    PrimaryMagicAim.Serialize(aWriter);
    SecondaryMagicOffset.Serialize(aWriter);
    SecondaryMagicAim.Serialize(aWriter);
    Body.Serialize(aWriter);
    Vrik.Serialize(aWriter);
}

void VRPoseUpdate::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Hmd.Deserialize(aReader);
    LeftHand.Deserialize(aReader);
    RightHand.Deserialize(aReader);
    SpellOrigin.Deserialize(aReader);
    SpellDestination.Deserialize(aReader);
    ArrowOrigin.Deserialize(aReader);
    ArrowDestination.Deserialize(aReader);
    BowAim.Deserialize(aReader);
    BowRotation.Deserialize(aReader);
    LeftWeaponOffset.Deserialize(aReader);
    RightWeaponOffset.Deserialize(aReader);
    PrimaryMagicOffset.Deserialize(aReader);
    PrimaryMagicAim.Deserialize(aReader);
    SecondaryMagicOffset.Deserialize(aReader);
    SecondaryMagicAim.Deserialize(aReader);
    Body.Deserialize(aReader);
    Vrik.Deserialize(aReader);
}

bool IsVRBodyPoseDataSafe(const VRBodyPoseData& acBody) noexcept
{
    if (acBody.FormatVersion == 0)
        return IsBodyPoseZeroState(acBody);
    if (acBody.FormatVersion != kBodyPoseFormatVersionCurrent)
        return false;
    if (!acBody.Valid)
        return IsBodyPoseZeroState(acBody);
    if (acBody.CaptureSequence == 0 || acBody.RootGeneration == 0)
        return false;

    return IsBodyNodeSafe(acBody.Pelvis, kMaxBodyPelvisPositionComponent) &&
           IsBodyNodeSafe(acBody.LeftThigh, kMaxBodyLimbPositionComponent) &&
           IsBodyNodeSafe(acBody.LeftCalf, kMaxBodyLimbPositionComponent) &&
           IsBodyNodeSafe(acBody.LeftFoot, kMaxBodyLimbPositionComponent) &&
           IsBodyNodeSafe(acBody.RightThigh, kMaxBodyLimbPositionComponent) &&
           IsBodyNodeSafe(acBody.RightCalf, kMaxBodyLimbPositionComponent) &&
           IsBodyNodeSafe(acBody.RightFoot, kMaxBodyLimbPositionComponent);
}

bool IsVRPoseUpdateSafe(const VRPoseUpdate& acPose) noexcept
{
    if (!IsPoseNodeSafe(acPose.Hmd) || !IsPoseNodeSafe(acPose.LeftHand) || !IsPoseNodeSafe(acPose.RightHand) ||
        !IsPoseNodeSafe(acPose.SpellOrigin) || !IsPoseNodeSafe(acPose.SpellDestination) ||
        !IsPoseNodeSafe(acPose.ArrowOrigin) || !IsPoseNodeSafe(acPose.ArrowDestination) ||
        !IsPoseNodeSafe(acPose.BowAim) || !IsPoseNodeSafe(acPose.BowRotation) ||
        !IsPoseNodeSafe(acPose.LeftWeaponOffset) || !IsPoseNodeSafe(acPose.RightWeaponOffset) ||
        !IsPoseNodeSafe(acPose.PrimaryMagicOffset) || !IsPoseNodeSafe(acPose.PrimaryMagicAim) ||
        !IsPoseNodeSafe(acPose.SecondaryMagicOffset) || !IsPoseNodeSafe(acPose.SecondaryMagicAim))
        return false;
    if (!IsVRBodyPoseDataSafe(acPose.Body))
        return false;
    if (!IsFingerCurlSafe(acPose.Vrik.LeftFingers) || !IsFingerCurlSafe(acPose.Vrik.RightFingers))
        return false;
    return AreVrikOffsetsSafe(acPose.Vrik);
}

bool HasAnyVRPosePayload(const VRPoseUpdate& acPose) noexcept
{
    return acPose.Hmd.Valid || acPose.LeftHand.Valid || acPose.RightHand.Valid || acPose.SpellOrigin.Valid ||
           acPose.SpellDestination.Valid || acPose.ArrowOrigin.Valid || acPose.ArrowDestination.Valid ||
           acPose.BowAim.Valid || acPose.BowRotation.Valid || acPose.LeftWeaponOffset.Valid ||
           acPose.RightWeaponOffset.Valid || acPose.PrimaryMagicOffset.Valid || acPose.PrimaryMagicAim.Valid ||
           acPose.SecondaryMagicOffset.Valid || acPose.SecondaryMagicAim.Valid || acPose.Body.Valid ||
           acPose.Vrik.Detected || acPose.Vrik.InterfaceAvailable || acPose.Vrik.LeftFingers.Valid ||
           acPose.Vrik.RightFingers.Valid || acPose.Vrik.CameraOffsetsValid;
}
