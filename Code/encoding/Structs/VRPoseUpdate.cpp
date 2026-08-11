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
constexpr uint32_t kBodyPoseFormatVersionLegacy = 1;
constexpr uint32_t kBodyPoseFormatVersionJoints = 2;
constexpr uint32_t kBodyPoseFormatVersionCurrent = 3;
constexpr uint32_t kBodyJointPoseFormatVersionCurrent = 1;
constexpr uint32_t kBodyJointPoseKnownMask = (1u << kVRBodyJointRotationCount) - 1u;
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

bool IsBodyJointRotationSafe(const VRBodyJointRotationData& acRotation) noexcept
{
    if (!std::isfinite(acRotation.X) || !std::isfinite(acRotation.Y) || !std::isfinite(acRotation.Z) ||
        !std::isfinite(acRotation.W))
        return false;
    const auto lengthSquared = acRotation.X * acRotation.X + acRotation.Y * acRotation.Y +
                               acRotation.Z * acRotation.Z + acRotation.W * acRotation.W;
    if (lengthSquared < 0.95f || lengthSquared > 1.05f)
        return false;

    const auto xx = acRotation.X * acRotation.X;
    const auto yy = acRotation.Y * acRotation.Y;
    const auto zz = acRotation.Z * acRotation.Z;
    const auto xy = acRotation.X * acRotation.Y;
    const auto xz = acRotation.X * acRotation.Z;
    const auto yz = acRotation.Y * acRotation.Z;
    const auto xw = acRotation.X * acRotation.W;
    const auto yw = acRotation.Y * acRotation.W;
    const auto zw = acRotation.Z * acRotation.W;
    const VRPoseNodeData probe{
        true,
        {},
        {1.0f - 2.0f * (yy + zz), 2.0f * (xy - zw), 2.0f * (xz + yw)},
        {2.0f * (xy + zw), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - xw)},
        {2.0f * (xz - yw), 2.0f * (yz + xw), 1.0f - 2.0f * (xx + yy)},
        1.0f,
    };
    return IsBodyNodeSafe(probe, 0.0f);
}

bool IsBodyPoseZeroState(const VRBodyPoseData& acBody) noexcept
{
    return !acBody.Valid && acBody.CaptureSequence == 0 && acBody.RootGeneration == 0 && !acBody.Pelvis.Valid &&
           !acBody.Spine0.Valid && !acBody.Spine1.Valid && !acBody.Spine2.Valid && !acBody.Neck.Valid &&
           !acBody.LeftClavicle.Valid && !acBody.LeftUpperArm.Valid && !acBody.LeftForearm.Valid &&
           !acBody.RightClavicle.Valid && !acBody.RightUpperArm.Valid && !acBody.RightForearm.Valid &&
           !acBody.LeftThigh.Valid && !acBody.LeftCalf.Valid && !acBody.LeftFoot.Valid && !acBody.RightThigh.Valid &&
           !acBody.RightCalf.Valid && !acBody.RightFoot.Valid && acBody.Joints.FormatVersion == 0 &&
           !acBody.Joints.Valid && acBody.Joints.CaptureSequence == 0 && acBody.Joints.RootGeneration == 0 &&
           acBody.Joints.NodeMask == 0;
}

bool HasNoUpperBodyExtension(const VRBodyPoseData& acBody) noexcept
{
    return !acBody.Spine0.Valid && !acBody.Spine1.Valid && !acBody.Spine2.Valid && !acBody.Neck.Valid &&
           !acBody.LeftClavicle.Valid && !acBody.LeftUpperArm.Valid && !acBody.LeftForearm.Valid &&
           !acBody.RightClavicle.Valid && !acBody.RightUpperArm.Valid && !acBody.RightForearm.Valid;
}

bool IsBodyJointPoseZeroState(const VRBodyJointPoseData& acJoints) noexcept
{
    return acJoints.FormatVersion == 0 && !acJoints.Valid && acJoints.CaptureSequence == 0 &&
           acJoints.RootGeneration == 0 && acJoints.NodeMask == 0;
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

bool VRBodyJointRotationData::operator==(const VRBodyJointRotationData& acRhs) const noexcept
{
    return X == acRhs.X && Y == acRhs.Y && Z == acRhs.Z && W == acRhs.W;
}

bool VRBodyJointRotationData::operator!=(const VRBodyJointRotationData& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRBodyJointRotationData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteFloat(aWriter, X);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Y);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Z);
    TiltedPhoques::Serialization::WriteFloat(aWriter, W);
}

void VRBodyJointRotationData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    X = TiltedPhoques::Serialization::ReadFloat(aReader);
    Y = TiltedPhoques::Serialization::ReadFloat(aReader);
    Z = TiltedPhoques::Serialization::ReadFloat(aReader);
    W = TiltedPhoques::Serialization::ReadFloat(aReader);
}

bool VRBodyJointPoseData::operator==(const VRBodyJointPoseData& acRhs) const noexcept
{
    return FormatVersion == acRhs.FormatVersion && Valid == acRhs.Valid && CaptureSequence == acRhs.CaptureSequence &&
           RootGeneration == acRhs.RootGeneration && NodeMask == acRhs.NodeMask && Rotations == acRhs.Rotations;
}

bool VRBodyJointPoseData::operator!=(const VRBodyJointPoseData& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRBodyJointPoseData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, FormatVersion);
    if (FormatVersion != kBodyJointPoseFormatVersionCurrent)
        return;

    TiltedPhoques::Serialization::WriteBool(aWriter, Valid);
    if (!Valid)
        return;

    TiltedPhoques::Serialization::WriteVarInt(aWriter, CaptureSequence);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, RootGeneration);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, NodeMask);
    for (std::size_t index = 0; index < Rotations.size(); ++index) {
        if ((NodeMask & (1u << index)) != 0)
            Rotations[index].Serialize(aWriter);
    }
}

void VRBodyJointPoseData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    FormatVersion = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    if (FormatVersion != kBodyJointPoseFormatVersionCurrent) {
        Valid = false;
        CaptureSequence = 0;
        RootGeneration = 0;
        NodeMask = 0;
        Rotations = {};
        return;
    }

    Valid = TiltedPhoques::Serialization::ReadBool(aReader);
    if (!Valid) {
        CaptureSequence = 0;
        RootGeneration = 0;
        NodeMask = 0;
        Rotations = {};
        return;
    }

    CaptureSequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RootGeneration = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    NodeMask = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Rotations = {};
    // A set bit declares a four-float quaternion on the fixed wire. Consume
    // unknown sparse bits too, then leave validation to reject the frame; this
    // preserves the following field boundary for malformed packets that still
    // carry their declared payload.
    for (std::uint32_t index = 0; index < 32; ++index) {
        if ((NodeMask & (1u << index)) == 0)
            continue;
        if (index < Rotations.size())
            Rotations[index].Deserialize(aReader);
        else {
            VRBodyJointRotationData ignored{};
            ignored.Deserialize(aReader);
        }
    }
}

bool VRBodyPoseData::operator==(const VRBodyPoseData& acRhs) const noexcept
{
    return FormatVersion == acRhs.FormatVersion && Valid == acRhs.Valid && CaptureSequence == acRhs.CaptureSequence &&
           RootGeneration == acRhs.RootGeneration && Pelvis == acRhs.Pelvis && Spine0 == acRhs.Spine0 &&
           Spine1 == acRhs.Spine1 && Spine2 == acRhs.Spine2 && Neck == acRhs.Neck &&
           LeftClavicle == acRhs.LeftClavicle && LeftUpperArm == acRhs.LeftUpperArm &&
           LeftForearm == acRhs.LeftForearm && RightClavicle == acRhs.RightClavicle &&
           RightUpperArm == acRhs.RightUpperArm && RightForearm == acRhs.RightForearm &&
           LeftThigh == acRhs.LeftThigh &&
           LeftCalf == acRhs.LeftCalf && LeftFoot == acRhs.LeftFoot && RightThigh == acRhs.RightThigh &&
           RightCalf == acRhs.RightCalf && RightFoot == acRhs.RightFoot && Joints == acRhs.Joints;
}

bool VRBodyPoseData::operator!=(const VRBodyPoseData& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRBodyPoseData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, FormatVersion);
    if (!IsSupportedVRBodyPoseFormatVersion(FormatVersion))
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
    if (FormatVersion == kBodyPoseFormatVersionCurrent) {
        Spine0.Serialize(aWriter);
        Spine1.Serialize(aWriter);
        Spine2.Serialize(aWriter);
        Neck.Serialize(aWriter);
        LeftClavicle.Serialize(aWriter);
        LeftUpperArm.Serialize(aWriter);
        LeftForearm.Serialize(aWriter);
        RightClavicle.Serialize(aWriter);
        RightUpperArm.Serialize(aWriter);
        RightForearm.Serialize(aWriter);
    }
    if (FormatVersion == kBodyPoseFormatVersionJoints || FormatVersion == kBodyPoseFormatVersionCurrent)
        Joints.Serialize(aWriter);
}

void VRBodyPoseData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    FormatVersion = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    if (!IsSupportedVRBodyPoseFormatVersion(FormatVersion))
    {
        Valid = false;
        CaptureSequence = 0;
        RootGeneration = 0;
        Pelvis = {};
        Spine0 = {};
        Spine1 = {};
        Spine2 = {};
        Neck = {};
        LeftClavicle = {};
        LeftUpperArm = {};
        LeftForearm = {};
        RightClavicle = {};
        RightUpperArm = {};
        RightForearm = {};
        LeftThigh = {};
        LeftCalf = {};
        LeftFoot = {};
        RightThigh = {};
        RightCalf = {};
        RightFoot = {};
        Joints = {};
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
    if (FormatVersion == kBodyPoseFormatVersionCurrent) {
        Spine0.Deserialize(aReader);
        Spine1.Deserialize(aReader);
        Spine2.Deserialize(aReader);
        Neck.Deserialize(aReader);
        LeftClavicle.Deserialize(aReader);
        LeftUpperArm.Deserialize(aReader);
        LeftForearm.Deserialize(aReader);
        RightClavicle.Deserialize(aReader);
        RightUpperArm.Deserialize(aReader);
        RightForearm.Deserialize(aReader);
    } else {
        Spine0 = {};
        Spine1 = {};
        Spine2 = {};
        Neck = {};
        LeftClavicle = {};
        LeftUpperArm = {};
        LeftForearm = {};
        RightClavicle = {};
        RightUpperArm = {};
        RightForearm = {};
    }
    if (FormatVersion == kBodyPoseFormatVersionJoints || FormatVersion == kBodyPoseFormatVersionCurrent)
        Joints.Deserialize(aReader);
    else
        Joints = {};
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

bool IsSupportedVRBodyPoseFormatVersion(const uint32_t aFormatVersion) noexcept
{
    return aFormatVersion == kBodyPoseFormatVersionLegacy || aFormatVersion == kBodyPoseFormatVersionJoints ||
           aFormatVersion == kBodyPoseFormatVersionCurrent;
}

bool IsVRBodyPoseDataSafe(const VRBodyPoseData& acBody) noexcept
{
    if (acBody.FormatVersion == 0)
        return IsBodyPoseZeroState(acBody);
    if (!IsSupportedVRBodyPoseFormatVersion(acBody.FormatVersion))
        return false;
    if (!acBody.Valid)
        return IsBodyPoseZeroState(acBody);
    if (acBody.CaptureSequence == 0 || acBody.RootGeneration == 0)
        return false;

    const auto bodySafe = IsBodyNodeSafe(acBody.Pelvis, kMaxBodyPelvisPositionComponent) &&
                          IsBodyNodeSafe(acBody.LeftThigh, kMaxBodyLimbPositionComponent) &&
                          IsBodyNodeSafe(acBody.LeftCalf, kMaxBodyLimbPositionComponent) &&
                          IsBodyNodeSafe(acBody.LeftFoot, kMaxBodyLimbPositionComponent) &&
                          IsBodyNodeSafe(acBody.RightThigh, kMaxBodyLimbPositionComponent) &&
                          IsBodyNodeSafe(acBody.RightCalf, kMaxBodyLimbPositionComponent) &&
                          IsBodyNodeSafe(acBody.RightFoot, kMaxBodyLimbPositionComponent);
    if (!bodySafe)
        return false;
    if (acBody.FormatVersion == kBodyPoseFormatVersionLegacy)
        return HasNoUpperBodyExtension(acBody) && IsBodyJointPoseZeroState(acBody.Joints);
    if (!IsVRBodyJointPoseDataSafe(acBody.Joints))
        return false;
    if (acBody.FormatVersion == kBodyPoseFormatVersionJoints)
        return HasNoUpperBodyExtension(acBody) &&
               (!acBody.Joints.Valid ||
                (acBody.Joints.CaptureSequence == acBody.CaptureSequence &&
                 acBody.Joints.RootGeneration == acBody.RootGeneration));

    const auto upperBodySafe = IsBodyNodeSafe(acBody.Spine0, kMaxBodyLimbPositionComponent) &&
                               IsBodyNodeSafe(acBody.Spine1, kMaxBodyLimbPositionComponent) &&
                               IsBodyNodeSafe(acBody.Spine2, kMaxBodyLimbPositionComponent) &&
                               IsBodyNodeSafe(acBody.Neck, kMaxBodyLimbPositionComponent) &&
                               IsBodyNodeSafe(acBody.LeftClavicle, kMaxBodyLimbPositionComponent) &&
                               IsBodyNodeSafe(acBody.LeftUpperArm, kMaxBodyLimbPositionComponent) &&
                               IsBodyNodeSafe(acBody.LeftForearm, kMaxBodyLimbPositionComponent) &&
                               IsBodyNodeSafe(acBody.RightClavicle, kMaxBodyLimbPositionComponent) &&
                               IsBodyNodeSafe(acBody.RightUpperArm, kMaxBodyLimbPositionComponent) &&
                               IsBodyNodeSafe(acBody.RightForearm, kMaxBodyLimbPositionComponent);
    if (!upperBodySafe)
        return false;
    return !acBody.Joints.Valid ||
           (acBody.Joints.CaptureSequence == acBody.CaptureSequence &&
            acBody.Joints.RootGeneration == acBody.RootGeneration);
}

bool IsVRBodyJointPoseDataSafe(const VRBodyJointPoseData& acJoints) noexcept
{
    if (acJoints.FormatVersion == 0)
        return IsBodyJointPoseZeroState(acJoints);
    if (acJoints.FormatVersion != kBodyJointPoseFormatVersionCurrent)
        return false;
    if (!acJoints.Valid)
        return acJoints.CaptureSequence == 0 && acJoints.RootGeneration == 0 && acJoints.NodeMask == 0;
    if (acJoints.CaptureSequence == 0 || acJoints.RootGeneration == 0 || acJoints.NodeMask == 0 ||
        (acJoints.NodeMask & ~kBodyJointPoseKnownMask) != 0)
        return false;

    for (std::size_t index = 0; index < acJoints.Rotations.size(); ++index) {
        if ((acJoints.NodeMask & (1u << index)) == 0)
            continue;
        if (!IsBodyJointRotationSafe(acJoints.Rotations[index]))
            return false;
    }
    return true;
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
           acPose.Body.Joints.Valid ||
           acPose.Vrik.Detected || acPose.Vrik.InterfaceAvailable || acPose.Vrik.LeftFingers.Valid ||
           acPose.Vrik.RightFingers.Valid || acPose.Vrik.CameraOffsetsValid;
}
