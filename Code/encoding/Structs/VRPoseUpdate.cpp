#include <Structs/VRPoseUpdate.h>
#include <TiltedCore/Serialization.hpp>

namespace
{
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

bool VRPoseUpdate::operator==(const VRPoseUpdate& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && Hmd == acRhs.Hmd && LeftHand == acRhs.LeftHand &&
           RightHand == acRhs.RightHand && SpellOrigin == acRhs.SpellOrigin &&
           SpellDestination == acRhs.SpellDestination && ArrowOrigin == acRhs.ArrowOrigin &&
           ArrowDestination == acRhs.ArrowDestination && BowAim == acRhs.BowAim &&
           BowRotation == acRhs.BowRotation && LeftWeaponOffset == acRhs.LeftWeaponOffset &&
           RightWeaponOffset == acRhs.RightWeaponOffset && PrimaryMagicOffset == acRhs.PrimaryMagicOffset &&
           PrimaryMagicAim == acRhs.PrimaryMagicAim && SecondaryMagicOffset == acRhs.SecondaryMagicOffset &&
           SecondaryMagicAim == acRhs.SecondaryMagicAim && Vrik == acRhs.Vrik;
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
    Vrik.Deserialize(aReader);
}
