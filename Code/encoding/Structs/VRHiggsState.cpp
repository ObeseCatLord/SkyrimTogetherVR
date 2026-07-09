#include <Structs/VRHiggsState.h>

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

bool VRHiggsFingerState::operator==(const VRHiggsFingerState& acRhs) const noexcept
{
    if (Valid != acRhs.Valid)
        return false;

    if (!Valid)
        return true;

    return Thumb == acRhs.Thumb && Index == acRhs.Index && Middle == acRhs.Middle &&
           Ring == acRhs.Ring && Pinky == acRhs.Pinky;
}

bool VRHiggsFingerState::operator!=(const VRHiggsFingerState& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRHiggsFingerState::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
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

void VRHiggsFingerState::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
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

bool VRHiggsGrabTransform::operator==(const VRHiggsGrabTransform& acRhs) const noexcept
{
    if (Valid != acRhs.Valid)
        return false;

    if (!Valid)
        return true;

    return Translate == acRhs.Translate && AxisX == acRhs.AxisX && AxisY == acRhs.AxisY &&
           AxisZ == acRhs.AxisZ && Scale == acRhs.Scale;
}

bool VRHiggsGrabTransform::operator!=(const VRHiggsGrabTransform& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRHiggsGrabTransform::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteBool(aWriter, Valid);
    if (!Valid)
        return;

    Translate.Serialize(aWriter);
    SerializeVector3(aWriter, AxisX);
    SerializeVector3(aWriter, AxisY);
    SerializeVector3(aWriter, AxisZ);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Scale);
}

void VRHiggsGrabTransform::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Valid = TiltedPhoques::Serialization::ReadBool(aReader);
    if (!Valid)
    {
        Translate = glm::vec3{};
        AxisX = {1.0f, 0.0f, 0.0f};
        AxisY = {0.0f, 1.0f, 0.0f};
        AxisZ = {0.0f, 0.0f, 1.0f};
        Scale = 1.0f;
        return;
    }

    Translate.Deserialize(aReader);
    DeserializeVector3(aReader, AxisX);
    DeserializeVector3(aReader, AxisY);
    DeserializeVector3(aReader, AxisZ);
    Scale = TiltedPhoques::Serialization::ReadFloat(aReader);
}

bool VRHiggsHandState::operator==(const VRHiggsHandState& acRhs) const noexcept
{
    if (Valid != acRhs.Valid)
        return false;

    if (!Valid)
        return true;

    return HoldingObject == acRhs.HoldingObject && CanGrabObject == acRhs.CanGrabObject &&
           HandInGrabbableState == acRhs.HandInGrabbableState && Disabled == acRhs.Disabled &&
           WeaponCollisionDisabled == acRhs.WeaponCollisionDisabled &&
           GrabbedObject == acRhs.GrabbedObject && Fingers == acRhs.Fingers &&
           GrabTransform == acRhs.GrabTransform;
}

bool VRHiggsHandState::operator!=(const VRHiggsHandState& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRHiggsHandState::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteBool(aWriter, Valid);
    if (!Valid)
        return;

    TiltedPhoques::Serialization::WriteBool(aWriter, HoldingObject);
    TiltedPhoques::Serialization::WriteBool(aWriter, CanGrabObject);
    TiltedPhoques::Serialization::WriteBool(aWriter, HandInGrabbableState);
    TiltedPhoques::Serialization::WriteBool(aWriter, Disabled);
    TiltedPhoques::Serialization::WriteBool(aWriter, WeaponCollisionDisabled);
    GrabbedObject.Serialize(aWriter);
    Fingers.Serialize(aWriter);
    GrabTransform.Serialize(aWriter);
}

void VRHiggsHandState::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Valid = TiltedPhoques::Serialization::ReadBool(aReader);
    if (!Valid)
    {
        HoldingObject = false;
        CanGrabObject = false;
        HandInGrabbableState = false;
        Disabled = false;
        WeaponCollisionDisabled = false;
        GrabbedObject = {};
        Fingers = {};
        GrabTransform = {};
        return;
    }

    HoldingObject = TiltedPhoques::Serialization::ReadBool(aReader);
    CanGrabObject = TiltedPhoques::Serialization::ReadBool(aReader);
    HandInGrabbableState = TiltedPhoques::Serialization::ReadBool(aReader);
    Disabled = TiltedPhoques::Serialization::ReadBool(aReader);
    WeaponCollisionDisabled = TiltedPhoques::Serialization::ReadBool(aReader);
    GrabbedObject.Deserialize(aReader);
    Fingers.Deserialize(aReader);
    GrabTransform.Deserialize(aReader);
}

bool VRHiggsEventSnapshot::operator==(const VRHiggsEventSnapshot& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && EventKind == acRhs.EventKind &&
           HasHand == acRhs.HasHand && IsLeft == acRhs.IsLeft && ObjectId == acRhs.ObjectId &&
           Mass == acRhs.Mass && SeparatingVelocity == acRhs.SeparatingVelocity;
}

bool VRHiggsEventSnapshot::operator!=(const VRHiggsEventSnapshot& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRHiggsEventSnapshot::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    aWriter.WriteBits(static_cast<uint8_t>(EventKind), 8);
    TiltedPhoques::Serialization::WriteBool(aWriter, HasHand);
    TiltedPhoques::Serialization::WriteBool(aWriter, IsLeft);
    ObjectId.Serialize(aWriter);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Mass);
    TiltedPhoques::Serialization::WriteFloat(aWriter, SeparatingVelocity);
}

void VRHiggsEventSnapshot::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    uint64_t kind{};
    aReader.ReadBits(kind, 8);
    EventKind = static_cast<Kind>(kind & 0xFF);

    HasHand = TiltedPhoques::Serialization::ReadBool(aReader);
    IsLeft = TiltedPhoques::Serialization::ReadBool(aReader);
    ObjectId.Deserialize(aReader);
    Mass = TiltedPhoques::Serialization::ReadFloat(aReader);
    SeparatingVelocity = TiltedPhoques::Serialization::ReadFloat(aReader);
}

bool VRHiggsState::operator==(const VRHiggsState& acRhs) const noexcept
{
    return Sequence == acRhs.Sequence && BridgeLoaded == acRhs.BridgeLoaded &&
           Detected == acRhs.Detected && InterfaceAvailable == acRhs.InterfaceAvailable &&
           CallbacksRegistered == acRhs.CallbacksRegistered &&
           SnapshotAvailable == acRhs.SnapshotAvailable &&
           SnapshotSequence == acRhs.SnapshotSequence && TwoHanding == acRhs.TwoHanding &&
           Left == acRhs.Left && Right == acRhs.Right &&
           LastEventValid == acRhs.LastEventValid && LastEvent == acRhs.LastEvent;
}

bool VRHiggsState::operator!=(const VRHiggsState& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRHiggsState::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    TiltedPhoques::Serialization::WriteBool(aWriter, BridgeLoaded);
    TiltedPhoques::Serialization::WriteBool(aWriter, Detected);
    TiltedPhoques::Serialization::WriteBool(aWriter, InterfaceAvailable);
    TiltedPhoques::Serialization::WriteBool(aWriter, CallbacksRegistered);
    TiltedPhoques::Serialization::WriteBool(aWriter, SnapshotAvailable);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, SnapshotSequence);
    TiltedPhoques::Serialization::WriteBool(aWriter, TwoHanding);
    Left.Serialize(aWriter);
    Right.Serialize(aWriter);
    TiltedPhoques::Serialization::WriteBool(aWriter, LastEventValid);
    if (LastEventValid)
        LastEvent.Serialize(aWriter);
}

void VRHiggsState::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    BridgeLoaded = TiltedPhoques::Serialization::ReadBool(aReader);
    Detected = TiltedPhoques::Serialization::ReadBool(aReader);
    InterfaceAvailable = TiltedPhoques::Serialization::ReadBool(aReader);
    CallbacksRegistered = TiltedPhoques::Serialization::ReadBool(aReader);
    SnapshotAvailable = TiltedPhoques::Serialization::ReadBool(aReader);
    SnapshotSequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    TwoHanding = TiltedPhoques::Serialization::ReadBool(aReader);
    Left.Deserialize(aReader);
    Right.Deserialize(aReader);

    LastEventValid = TiltedPhoques::Serialization::ReadBool(aReader);
    if (LastEventValid)
    {
        LastEvent.Deserialize(aReader);
    }
    else
    {
        LastEvent = {};
    }
}
