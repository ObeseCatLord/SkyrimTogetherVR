#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <TiltedCore/Buffer.hpp>
#include <glm/vec4.hpp>

struct VRPlanckPhysicsEvent
{
    enum class Kind : uint8_t
    {
        HitImpulse = 1,
        RagdollEnter,
        RagdollExit,
        GripBegin,
        GripUpdate,
        GripEnd,
    };

    static constexpr std::size_t kMaximumNodeNameBytes = 63;
    static constexpr float kMaximumVectorMagnitude = 1'048'575.0F;
    static constexpr float kMaximumImpulseMultiplier = 10.0F;
    static constexpr float kMaximumGripTtlSeconds = 5.0F;

    bool operator==(const VRPlanckPhysicsEvent& acRhs) const noexcept;
    bool operator!=(const VRPlanckPhysicsEvent& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
    [[nodiscard]] bool IsValid() const noexcept;

    Kind EventKind{};
    uint64_t ProducerEpoch{0};
    uint64_t EventId{0};
    GameId TargetActorId{};
    uint64_t GripId{0};
    std::array<char, kMaximumNodeNameBytes> NodeName{};
    uint8_t NodeNameLength{0};
    Vector3_NetQuantize Position{};
    Vector3_NetQuantize Velocity{};
    Vector3_NetQuantize SourcePosition{};
    Vector3_NetQuantize LinearVelocity{};
    Vector3_NetQuantize AngularVelocity{};
    glm::vec4 WorldRotation{0.0F, 0.0F, 0.0F, 1.0F};
    float ImpulseMultiplier{0.0F};
    float TtlSeconds{0.0F};
    bool IsDecodedValid{true};
};

namespace SkyrimTogether::PlanckPhysicsPolicy
{
[[nodiscard]] constexpr bool IsStrictlyNewEvent(const uint64_t aCandidate, const uint64_t aLast,
                                                const bool aHasLast) noexcept
{
    return aCandidate != 0 && (!aHasLast || aCandidate > aLast);
}

[[nodiscard]] constexpr bool CanRoutePlayerTarget(const bool aPvpEnabled) noexcept
{
    return aPvpEnabled;
}

[[nodiscard]] constexpr bool ShouldAppendRemoteRetry(
    const bool aHasPendingEvent, const uint64_t aLastPendingEventId,
    const uint64_t aCandidateEventId) noexcept
{
    return aHasPendingEvent && aCandidateEventId > aLastPendingEventId;
}

[[nodiscard]] constexpr bool IsRemoteRetryExpired(
    const uint8_t aAttempts, const uint8_t aMaximumAttempts,
    const double aElapsedSeconds, const double aMaximumElapsedSeconds) noexcept
{
    return aAttempts >= aMaximumAttempts || aElapsedSeconds >= aMaximumElapsedSeconds;
}
} // namespace SkyrimTogether::PlanckPhysicsPolicy
