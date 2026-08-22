#include <Structs/VRHiggsState.h>

#include <algorithm>
#include <cmath>

#include <TiltedCore/Serialization.hpp>

#include <Structs/VRInteractionValidation.h>

namespace
{
bool IsNewerMutationSequence(const uint32_t aCandidate, const uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

template <std::size_t N>
[[nodiscard]] bool IsValidNodeName(
    const std::array<char, N>& acName, const std::uint8_t aLength) noexcept
{
    // Replay passes this fixed buffer to BSFixedString, so one byte must
    // remain zero-terminated even for a packet constructed by a peer.
    if (aLength >= acName.size())
        return false;
    return std::none_of(acName.begin(), acName.begin() + aLength,
                        [](const char aCharacter) noexcept { return aCharacter == '\0'; });
}

[[nodiscard]] bool IsFiniteVector(const glm::vec3& acValue) noexcept
{
    return std::isfinite(acValue.x) && std::isfinite(acValue.y) && std::isfinite(acValue.z);
}

[[nodiscard]] float Dot(const glm::vec3& acLeft, const glm::vec3& acRight) noexcept
{
    return acLeft.x * acRight.x + acLeft.y * acRight.y + acLeft.z * acRight.z;
}

[[nodiscard]] bool IsValidGrabTransform(const VRHiggsGrabTransform& acTransform) noexcept
{
    if (!acTransform.Valid)
        return true;
    constexpr float kAxisTolerance = 0.02F;
    return IsFiniteVector(acTransform.Translate) && IsFiniteVector(acTransform.AxisX) &&
           IsFiniteVector(acTransform.AxisY) && IsFiniteVector(acTransform.AxisZ) &&
           std::isfinite(acTransform.Scale) && acTransform.Scale >= 0.001F && acTransform.Scale <= 1000.0F &&
           std::abs(Dot(acTransform.AxisX, acTransform.AxisX) - 1.0F) <= kAxisTolerance &&
           std::abs(Dot(acTransform.AxisY, acTransform.AxisY) - 1.0F) <= kAxisTolerance &&
           std::abs(Dot(acTransform.AxisZ, acTransform.AxisZ) - 1.0F) <= kAxisTolerance &&
           std::abs(Dot(acTransform.AxisX, acTransform.AxisY)) <= kAxisTolerance &&
           std::abs(Dot(acTransform.AxisX, acTransform.AxisZ)) <= kAxisTolerance &&
           std::abs(Dot(acTransform.AxisY, acTransform.AxisZ)) <= kAxisTolerance;
}

[[nodiscard]] bool IsValidFingerState(const VRHiggsFingerState& acFingers) noexcept
{
    if (!acFingers.Valid)
        return true;
    const auto valid = [](const float aValue) noexcept {
        return std::isfinite(aValue) && aValue >= 0.0F && aValue <= 1.0F;
    };
    return valid(acFingers.Thumb) && valid(acFingers.Index) && valid(acFingers.Middle) &&
           valid(acFingers.Ring) && valid(acFingers.Pinky);
}

[[nodiscard]] bool IsValidHandState(const VRHiggsHandState& acHand) noexcept
{
    if (!acHand.IsDecodedValid)
        return false;
    if (!acHand.Valid)
        return true;
    if (!IsValidNodeName(acHand.GrabbedNodeName, acHand.GrabbedNodeNameLength) ||
        !IsValidFingerState(acHand.Fingers) || !IsValidGrabTransform(acHand.GrabTransform))
        return false;
    if (acHand.HoldingObject)
        return static_cast<bool>(acHand.GrabbedObject) && acHand.GrabTransform.Valid;
    return !acHand.GrabTransform.Valid && acHand.GrabbedNodeNameLength == 0;
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
    if (IsDecodedValid != acRhs.IsDecodedValid || Valid != acRhs.Valid)
        return false;

    if (!Valid)
        return true;

    return HoldingObject == acRhs.HoldingObject && CanGrabObject == acRhs.CanGrabObject &&
           HandInGrabbableState == acRhs.HandInGrabbableState && Disabled == acRhs.Disabled &&
           WeaponCollisionDisabled == acRhs.WeaponCollisionDisabled &&
           GrabbedObject == acRhs.GrabbedObject && GrabbedNodeName == acRhs.GrabbedNodeName &&
           GrabbedNodeNameLength == acRhs.GrabbedNodeNameLength && Fingers == acRhs.Fingers &&
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
    const auto nodeNameLength = IsValidNodeName(GrabbedNodeName, GrabbedNodeNameLength) ?
        GrabbedNodeNameLength : 0;
    aWriter.WriteBits(nodeNameLength, 8);
    for (std::size_t index = 0; index < nodeNameLength; ++index)
        aWriter.WriteBits(static_cast<uint8_t>(GrabbedNodeName[index]), 8);
    Fingers.Serialize(aWriter);
    GrabTransform.Serialize(aWriter);
}

void VRHiggsHandState::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    IsDecodedValid = true;
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
    uint64_t nodeNameLength{};
    aReader.ReadBits(nodeNameLength, 8);
    if (nodeNameLength > GrabbedNodeName.size())
    {
        Valid = false;
        IsDecodedValid = false;
        GrabbedNodeName = {};
        GrabbedNodeNameLength = 0;
        return;
    }
    GrabbedNodeName = {};
    GrabbedNodeNameLength = static_cast<uint8_t>(nodeNameLength);
    for (std::size_t index = 0; index < GrabbedNodeNameLength; ++index) {
        uint64_t character{};
        aReader.ReadBits(character, 8);
        GrabbedNodeName[index] = static_cast<char>(character);
    }
    if (!IsValidNodeName(GrabbedNodeName, GrabbedNodeNameLength)) {
        Valid = false;
        IsDecodedValid = false;
        return;
    }
    Fingers.Deserialize(aReader);
    GrabTransform.Deserialize(aReader);
}

bool VRHiggsEventSnapshot::operator==(const VRHiggsEventSnapshot& acRhs) const noexcept
{
    return IsDecodedValid == acRhs.IsDecodedValid && Sequence == acRhs.Sequence && EventKind == acRhs.EventKind &&
           HasHand == acRhs.HasHand && IsLeft == acRhs.IsLeft && ObjectId == acRhs.ObjectId &&
           InventoryBaseForm == acRhs.InventoryBaseForm && Mass == acRhs.Mass &&
           SeparatingVelocity == acRhs.SeparatingVelocity && GrabTransform == acRhs.GrabTransform &&
           GrabbedNodeName == acRhs.GrabbedNodeName && GrabbedNodeNameLength == acRhs.GrabbedNodeNameLength;
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
    InventoryBaseForm.Serialize(aWriter);
    TiltedPhoques::Serialization::WriteFloat(aWriter, Mass);
    TiltedPhoques::Serialization::WriteFloat(aWriter, SeparatingVelocity);
    GrabTransform.Serialize(aWriter);
    const auto nodeNameLength = IsValidNodeName(GrabbedNodeName, GrabbedNodeNameLength) ?
        GrabbedNodeNameLength : 0;
    aWriter.WriteBits(nodeNameLength, 8);
    for (std::size_t index = 0; index < nodeNameLength; ++index)
        aWriter.WriteBits(static_cast<uint8_t>(GrabbedNodeName[index]), 8);
}

void VRHiggsEventSnapshot::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    IsDecodedValid = true;
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    uint64_t kind{};
    aReader.ReadBits(kind, 8);
    EventKind = static_cast<Kind>(kind & 0xFF);

    HasHand = TiltedPhoques::Serialization::ReadBool(aReader);
    IsLeft = TiltedPhoques::Serialization::ReadBool(aReader);
    ObjectId.Deserialize(aReader);
    InventoryBaseForm.Deserialize(aReader);
    Mass = TiltedPhoques::Serialization::ReadFloat(aReader);
    SeparatingVelocity = TiltedPhoques::Serialization::ReadFloat(aReader);
    GrabTransform.Deserialize(aReader);
    uint64_t nodeNameLength{};
    aReader.ReadBits(nodeNameLength, 8);
    GrabbedNodeName = {};
    if (nodeNameLength <= GrabbedNodeName.size()) {
        GrabbedNodeNameLength = static_cast<uint8_t>(nodeNameLength);
        for (std::size_t index = 0; index < GrabbedNodeNameLength; ++index) {
            uint64_t character{};
            aReader.ReadBits(character, 8);
            GrabbedNodeName[index] = static_cast<char>(character);
        }
        if (!IsValidNodeName(GrabbedNodeName, GrabbedNodeNameLength)) {
            GrabbedNodeNameLength = 0;
            IsDecodedValid = false;
        }
    } else {
        GrabbedNodeNameLength = 0;
        IsDecodedValid = false;
    }
}

bool VRHiggsState::operator==(const VRHiggsState& acRhs) const noexcept
{
    const auto eventCount = std::min<std::size_t>(MutationEventCount, MutationEvents.size());
    const auto rhsEventCount = std::min<std::size_t>(acRhs.MutationEventCount, acRhs.MutationEvents.size());
    return IsDecodedValid == acRhs.IsDecodedValid &&
           Sequence == acRhs.Sequence && ProducerEpoch == acRhs.ProducerEpoch &&
           MutationSequence == acRhs.MutationSequence && MutationReplayRebased == acRhs.MutationReplayRebased &&
           BridgeLoaded == acRhs.BridgeLoaded &&
           Detected == acRhs.Detected && InterfaceAvailable == acRhs.InterfaceAvailable &&
           CallbacksRegistered == acRhs.CallbacksRegistered &&
           SnapshotAvailable == acRhs.SnapshotAvailable &&
           SnapshotSequence == acRhs.SnapshotSequence && TwoHanding == acRhs.TwoHanding &&
           Left == acRhs.Left && Right == acRhs.Right &&
           eventCount == rhsEventCount &&
           std::equal(MutationEvents.begin(), MutationEvents.begin() + eventCount, acRhs.MutationEvents.begin());
}

bool VRHiggsState::operator!=(const VRHiggsState& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void VRHiggsState::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto eventCount = std::min<std::size_t>(MutationEventCount, MutationEvents.size());
    // Never emit a count/terminal-sequence mismatch when an in-memory caller
    // supplied more than the bounded wire window. The excess events are
    // intentionally clamped rather than becoming an invalid packet.
    const auto mutationSequence = eventCount != 0 ? MutationEvents[eventCount - 1].Sequence : 0;
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Sequence);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, ProducerEpoch);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, mutationSequence);
    TiltedPhoques::Serialization::WriteBool(aWriter, MutationReplayRebased);
    TiltedPhoques::Serialization::WriteBool(aWriter, BridgeLoaded);
    TiltedPhoques::Serialization::WriteBool(aWriter, Detected);
    TiltedPhoques::Serialization::WriteBool(aWriter, InterfaceAvailable);
    TiltedPhoques::Serialization::WriteBool(aWriter, CallbacksRegistered);
    TiltedPhoques::Serialization::WriteBool(aWriter, SnapshotAvailable);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, SnapshotSequence);
    TiltedPhoques::Serialization::WriteBool(aWriter, TwoHanding);
    Left.Serialize(aWriter);
    Right.Serialize(aWriter);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, eventCount);
    for (std::size_t index = 0; index < eventCount; ++index)
        MutationEvents[index].Serialize(aWriter);
}

void VRHiggsState::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    IsDecodedValid = true;
    Sequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ProducerEpoch = TiltedPhoques::Serialization::ReadVarInt(aReader);
    MutationSequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    MutationReplayRebased = TiltedPhoques::Serialization::ReadBool(aReader);
    BridgeLoaded = TiltedPhoques::Serialization::ReadBool(aReader);
    Detected = TiltedPhoques::Serialization::ReadBool(aReader);
    InterfaceAvailable = TiltedPhoques::Serialization::ReadBool(aReader);
    CallbacksRegistered = TiltedPhoques::Serialization::ReadBool(aReader);
    SnapshotAvailable = TiltedPhoques::Serialization::ReadBool(aReader);
    SnapshotSequence = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    TwoHanding = TiltedPhoques::Serialization::ReadBool(aReader);
    Left.Deserialize(aReader);
    if (!Left.IsDecodedValid)
    {
        IsDecodedValid = false;
        return;
    }
    Right.Deserialize(aReader);
    if (!Right.IsDecodedValid)
    {
        IsDecodedValid = false;
        return;
    }

    MutationEvents = {};
    const auto eventCount = TiltedPhoques::Serialization::ReadVarInt(aReader);
    if (eventCount > MutationEvents.size())
    {
        MutationEventCount = 0;
        IsDecodedValid = false;
        return;
    }
    MutationEventCount = static_cast<uint8_t>(eventCount);
    for (std::size_t index = 0; index < MutationEventCount; ++index) {
        MutationEvents[index].Deserialize(aReader);
        if (!MutationEvents[index].IsDecodedValid)
        {
            IsDecodedValid = false;
            return;
        }
    }
}

bool VRHiggsState::IsMutationReplayValid() const noexcept
{
    if (!IsDecodedValid || ProducerEpoch == 0 || MutationEventCount > MutationEvents.size() ||
        !IsValidHandState(Left) || !IsValidHandState(Right))
        return false;
    if (MutationEventCount == 0)
        return MutationSequence == 0;

    uint32_t previousSequence{};
    for (std::size_t index = 0; index < MutationEventCount; ++index)
    {
        const auto& event = MutationEvents[index];
        const auto sequence = event.Sequence;
        if (sequence == 0 || !event.IsDecodedValid ||
            !IsValidNodeName(event.GrabbedNodeName, event.GrabbedNodeNameLength) ||
            !IsValidGrabTransform(event.GrabTransform) ||
            !SkyrimTogether::VR::IsHiggsMutationPayloadValid(
                                 event.Mass, event.SeparatingVelocity) ||
            (index != 0 && !IsNewerMutationSequence(sequence, previousSequence)))
            return false;
        previousSequence = sequence;
    }
    return MutationSequence == previousSequence;
}
