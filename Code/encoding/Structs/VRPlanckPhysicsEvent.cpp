#include <Structs/VRPlanckPhysicsEvent.h>

#include <algorithm>
#include <cmath>

#include <TiltedCore/Serialization.hpp>

namespace
{
using Kind = VRPlanckPhysicsEvent::Kind;

[[nodiscard]] bool IsKnownKind(const Kind aKind) noexcept
{
    return aKind >= Kind::HitImpulse && aKind <= Kind::GripEnd;
}

[[nodiscard]] bool IsNodeRequired(const Kind aKind) noexcept
{
    return aKind == Kind::HitImpulse || aKind == Kind::GripBegin;
}

[[nodiscard]] bool IsFiniteVector(const Vector3_NetQuantize& acValue) noexcept
{
    return std::isfinite(acValue.x) && std::isfinite(acValue.y) && std::isfinite(acValue.z) &&
           std::abs(acValue.x) <= VRPlanckPhysicsEvent::kMaximumVectorMagnitude &&
           std::abs(acValue.y) <= VRPlanckPhysicsEvent::kMaximumVectorMagnitude &&
           std::abs(acValue.z) <= VRPlanckPhysicsEvent::kMaximumVectorMagnitude;
}

[[nodiscard]] bool IsZeroVector(const Vector3_NetQuantize& acValue) noexcept
{
    return acValue.x == 0.0F && acValue.y == 0.0F && acValue.z == 0.0F;
}

[[nodiscard]] bool IsIdentityQuaternion(const glm::vec4& acValue) noexcept
{
    return acValue.x == 0.0F && acValue.y == 0.0F && acValue.z == 0.0F && acValue.w == 1.0F;
}

[[nodiscard]] bool IsNearUnitQuaternion(const glm::vec4& acValue) noexcept
{
    if (!std::isfinite(acValue.x) || !std::isfinite(acValue.y) || !std::isfinite(acValue.z) ||
        !std::isfinite(acValue.w))
        return false;

    const auto magnitudeSquared = acValue.x * acValue.x + acValue.y * acValue.y +
                                  acValue.z * acValue.z + acValue.w * acValue.w;
    return std::isfinite(magnitudeSquared) && magnitudeSquared >= 0.98F && magnitudeSquared <= 1.02F;
}

[[nodiscard]] bool HasCanonicalNode(const VRPlanckPhysicsEvent& acEvent, const bool aRequired) noexcept
{
    if (aRequired != (acEvent.NodeNameLength != 0) || acEvent.NodeNameLength > acEvent.NodeName.size())
        return false;

    for (std::size_t index = 0; index < acEvent.NodeName.size(); ++index)
    {
        if (index < acEvent.NodeNameLength)
        {
            if (acEvent.NodeName[index] == '\0')
                return false;
        }
        else if (acEvent.NodeName[index] != '\0')
            return false;
    }

    return true;
}

[[nodiscard]] bool HasCanonicalCommonFields(const VRPlanckPhysicsEvent& acEvent) noexcept
{
    return acEvent.GripId == 0 && IsZeroVector(acEvent.Position) && IsZeroVector(acEvent.Velocity) &&
           IsZeroVector(acEvent.SourcePosition) && IsZeroVector(acEvent.LinearVelocity) &&
           IsZeroVector(acEvent.AngularVelocity) && IsIdentityQuaternion(acEvent.WorldRotation) &&
           acEvent.ImpulseMultiplier == 0.0F && acEvent.TtlSeconds == 0.0F;
}

void SerializeEvent(const VRPlanckPhysicsEvent& acEvent, TiltedPhoques::Buffer::Writer& arWriter) noexcept
{
    arWriter.WriteBits(static_cast<uint8_t>(acEvent.EventKind), 8);
    TiltedPhoques::Serialization::WriteVarInt(arWriter, acEvent.ProducerEpoch);
    TiltedPhoques::Serialization::WriteVarInt(arWriter, acEvent.EventId);
    acEvent.TargetActorId.Serialize(arWriter);
    TiltedPhoques::Serialization::WriteVarInt(arWriter, acEvent.GripId);
    arWriter.WriteBits(acEvent.NodeNameLength, 8);
    for (std::size_t index = 0; index < acEvent.NodeNameLength; ++index)
        arWriter.WriteBits(static_cast<uint8_t>(acEvent.NodeName[index]), 8);
    acEvent.Position.Serialize(arWriter);
    acEvent.Velocity.Serialize(arWriter);
    acEvent.SourcePosition.Serialize(arWriter);
    acEvent.LinearVelocity.Serialize(arWriter);
    acEvent.AngularVelocity.Serialize(arWriter);
    TiltedPhoques::Serialization::WriteFloat(arWriter, acEvent.WorldRotation.x);
    TiltedPhoques::Serialization::WriteFloat(arWriter, acEvent.WorldRotation.y);
    TiltedPhoques::Serialization::WriteFloat(arWriter, acEvent.WorldRotation.z);
    TiltedPhoques::Serialization::WriteFloat(arWriter, acEvent.WorldRotation.w);
    TiltedPhoques::Serialization::WriteFloat(arWriter, acEvent.ImpulseMultiplier);
    TiltedPhoques::Serialization::WriteFloat(arWriter, acEvent.TtlSeconds);
}
} // namespace

bool VRPlanckPhysicsEvent::operator==(const VRPlanckPhysicsEvent& acRhs) const noexcept
{
    return EventKind == acRhs.EventKind && ProducerEpoch == acRhs.ProducerEpoch && EventId == acRhs.EventId &&
           TargetActorId == acRhs.TargetActorId && GripId == acRhs.GripId && NodeName == acRhs.NodeName &&
           NodeNameLength == acRhs.NodeNameLength && Position == acRhs.Position && Velocity == acRhs.Velocity &&
           SourcePosition == acRhs.SourcePosition && LinearVelocity == acRhs.LinearVelocity &&
           AngularVelocity == acRhs.AngularVelocity && WorldRotation == acRhs.WorldRotation &&
           ImpulseMultiplier == acRhs.ImpulseMultiplier && TtlSeconds == acRhs.TtlSeconds &&
           IsDecodedValid == acRhs.IsDecodedValid;
}

bool VRPlanckPhysicsEvent::operator!=(const VRPlanckPhysicsEvent& acRhs) const noexcept
{
    return !operator==(acRhs);
}

bool VRPlanckPhysicsEvent::IsValid() const noexcept
{
    if (!IsDecodedValid || !IsKnownKind(EventKind) || ProducerEpoch == 0 || EventId == 0 || !TargetActorId ||
        !IsFiniteVector(Position) || !IsFiniteVector(Velocity) || !IsFiniteVector(SourcePosition) ||
        !IsFiniteVector(LinearVelocity) || !IsFiniteVector(AngularVelocity) || !std::isfinite(ImpulseMultiplier) ||
        !std::isfinite(TtlSeconds))
        return false;

    switch (EventKind)
    {
    case Kind::HitImpulse:
        return GripId == 0 && HasCanonicalNode(*this, true) && IsFiniteVector(Position) && IsFiniteVector(Velocity) &&
               IsZeroVector(SourcePosition) && IsZeroVector(LinearVelocity) && IsZeroVector(AngularVelocity) &&
               IsIdentityQuaternion(WorldRotation) && ImpulseMultiplier > 0.0F &&
               ImpulseMultiplier <= kMaximumImpulseMultiplier && TtlSeconds == 0.0F;
    case Kind::RagdollEnter:
        return GripId == 0 && HasCanonicalNode(*this, false) && IsZeroVector(Position) && IsZeroVector(Velocity) &&
               IsFiniteVector(SourcePosition) && IsZeroVector(LinearVelocity) && IsZeroVector(AngularVelocity) &&
               IsIdentityQuaternion(WorldRotation) && ImpulseMultiplier == 0.0F && TtlSeconds == 0.0F;
    case Kind::RagdollExit:
        return HasCanonicalCommonFields(*this) && HasCanonicalNode(*this, false);
    case Kind::GripBegin:
    case Kind::GripUpdate:
        return GripId != 0 && HasCanonicalNode(*this, EventKind == Kind::GripBegin) && IsFiniteVector(Position) &&
               IsZeroVector(Velocity) && IsZeroVector(SourcePosition) && IsFiniteVector(LinearVelocity) &&
               IsFiniteVector(AngularVelocity) && IsNearUnitQuaternion(WorldRotation) && ImpulseMultiplier == 0.0F &&
               TtlSeconds > 0.0F && TtlSeconds <= kMaximumGripTtlSeconds;
    case Kind::GripEnd:
        return GripId != 0 && HasCanonicalNode(*this, false) && IsZeroVector(Position) && IsZeroVector(Velocity) &&
               IsZeroVector(SourcePosition) && IsZeroVector(LinearVelocity) && IsZeroVector(AngularVelocity) &&
               IsIdentityQuaternion(WorldRotation) && ImpulseMultiplier == 0.0F && TtlSeconds == 0.0F;
    default:
        return false;
    }
}

void VRPlanckPhysicsEvent::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    // Never send an invalid in-memory payload into Vector3_NetQuantize::Pack:
    // its cast is intentionally unchecked for historical protocol reasons.
    const VRPlanckPhysicsEvent invalidEvent{};
    SerializeEvent(IsValid() ? *this : invalidEvent, aWriter);
}

void VRPlanckPhysicsEvent::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};

    uint64_t eventKind{};
    uint64_t nodeNameLength{};
    aReader.ReadBits(eventKind, 8);
    EventKind = static_cast<Kind>(eventKind);
    ProducerEpoch = TiltedPhoques::Serialization::ReadVarInt(aReader);
    EventId = TiltedPhoques::Serialization::ReadVarInt(aReader);
    TargetActorId.Deserialize(aReader);
    GripId = TiltedPhoques::Serialization::ReadVarInt(aReader);
    aReader.ReadBits(nodeNameLength, 8);

    const auto boundedNodeNameLength = std::min<std::size_t>(nodeNameLength, NodeName.size());
    for (std::size_t index = 0; index < nodeNameLength; ++index)
    {
        uint64_t character{};
        aReader.ReadBits(character, 8);
        if (index < boundedNodeNameLength)
            NodeName[index] = static_cast<char>(character);
    }
    if (nodeNameLength <= NodeName.size())
        NodeNameLength = static_cast<uint8_t>(nodeNameLength);

    Position.Deserialize(aReader);
    Velocity.Deserialize(aReader);
    SourcePosition.Deserialize(aReader);
    LinearVelocity.Deserialize(aReader);
    AngularVelocity.Deserialize(aReader);
    WorldRotation.x = TiltedPhoques::Serialization::ReadFloat(aReader);
    WorldRotation.y = TiltedPhoques::Serialization::ReadFloat(aReader);
    WorldRotation.z = TiltedPhoques::Serialization::ReadFloat(aReader);
    WorldRotation.w = TiltedPhoques::Serialization::ReadFloat(aReader);
    ImpulseMultiplier = TiltedPhoques::Serialization::ReadFloat(aReader);
    TtlSeconds = TiltedPhoques::Serialization::ReadFloat(aReader);
    IsDecodedValid = nodeNameLength <= NodeName.size() && IsValid();
}
