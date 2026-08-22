#include <catch2/catch.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>
#include <glm/glm.hpp>

#include <Structs/GameplayCapabilities.h>
#include <Structs/VRPlanckPhysicsEvent.h>
#include <vr_common/VRPlanckPhysicsBridge.h>

namespace
{
void SetNode(VRPlanckPhysicsEvent& arEvent, const char* apNode) noexcept
{
    std::size_t length = 0;
    while (apNode[length] != '\0')
    {
        arEvent.NodeName[length] = apNode[length];
        ++length;
    }
    arEvent.NodeNameLength = static_cast<uint8_t>(length);
}

VRPlanckPhysicsEvent ValidEvent(const VRPlanckPhysicsEvent::Kind aKind)
{
    VRPlanckPhysicsEvent event{};
    event.EventKind = aKind;
    event.ProducerEpoch = 1;
    event.EventId = 1;
    event.TargetActorId = GameId(1, 1);

    switch (aKind)
    {
    case VRPlanckPhysicsEvent::Kind::HitImpulse:
        SetNode(event, "Node");
        event.Position = {1.0F, 2.0F, 3.0F};
        event.Velocity = {4.0F, 5.0F, 6.0F};
        event.ImpulseMultiplier = 1.0F;
        break;
    case VRPlanckPhysicsEvent::Kind::RagdollEnter:
        event.SourcePosition = {1.0F, 2.0F, 3.0F};
        break;
    case VRPlanckPhysicsEvent::Kind::RagdollExit:
        break;
    case VRPlanckPhysicsEvent::Kind::GripBegin:
        SetNode(event, "GripNode");
        [[fallthrough]];
    case VRPlanckPhysicsEvent::Kind::GripUpdate:
        event.GripId = 1;
        event.Position = {1.0F, 2.0F, 3.0F};
        event.LinearVelocity = {4.0F, 5.0F, 6.0F};
        event.AngularVelocity = {7.0F, 8.0F, 9.0F};
        event.TtlSeconds = 1.0F;
        break;
    case VRPlanckPhysicsEvent::Kind::GripEnd:
        event.GripId = 1;
        break;
    }

    return event;
}
} // namespace

TEST_CASE("PLANCK physics accepts canonical payloads for every interface002 kind")
{
    for (const auto kind : {
             VRPlanckPhysicsEvent::Kind::HitImpulse,
             VRPlanckPhysicsEvent::Kind::RagdollEnter,
             VRPlanckPhysicsEvent::Kind::RagdollExit,
             VRPlanckPhysicsEvent::Kind::GripBegin,
             VRPlanckPhysicsEvent::Kind::GripUpdate,
             VRPlanckPhysicsEvent::Kind::GripEnd,
         })
    {
        const auto event = ValidEvent(kind);
        CHECK(event.IsValid());

        TiltedPhoques::Buffer buffer(512);
        TiltedPhoques::Buffer::Writer writer(&buffer);
        event.Serialize(writer);
        VRPlanckPhysicsEvent decoded{};
        TiltedPhoques::Buffer::Reader reader(&buffer);
        decoded.Deserialize(reader);
        CHECK(decoded == event);
    }
}

TEST_CASE("PLANCK physics rejects malformed node and irrelevant fields")
{
    auto event = ValidEvent(VRPlanckPhysicsEvent::Kind::HitImpulse);
    event.NodeNameLength = static_cast<uint8_t>(VRPlanckPhysicsEvent::kMaximumNodeNameBytes + 1);
    CHECK_FALSE(event.IsValid());

    event = ValidEvent(VRPlanckPhysicsEvent::Kind::HitImpulse);
    event.NodeName[0] = '\0';
    CHECK_FALSE(event.IsValid());

    event = ValidEvent(VRPlanckPhysicsEvent::Kind::HitImpulse);
    event.NodeName[event.NodeNameLength] = 'x';
    CHECK_FALSE(event.IsValid());

    event = ValidEvent(VRPlanckPhysicsEvent::Kind::HitImpulse);
    event.SourcePosition.x = 1.0F;
    CHECK_FALSE(event.IsValid());

    event = ValidEvent(VRPlanckPhysicsEvent::Kind::RagdollEnter);
    event.Position.x = 1.0F;
    CHECK_FALSE(event.IsValid());

    event = ValidEvent(VRPlanckPhysicsEvent::Kind::GripUpdate);
    event.NodeName[0] = 'x';
    CHECK_FALSE(event.IsValid());

    event = ValidEvent(VRPlanckPhysicsEvent::Kind::GripEnd);
    event.TtlSeconds = 1.0F;
    CHECK_FALSE(event.IsValid());
}

TEST_CASE("PLANCK physics rejects unsafe quantized vectors and grip rotations")
{
    auto event = ValidEvent(VRPlanckPhysicsEvent::Kind::HitImpulse);
    event.Position.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(event.IsValid());

    event = ValidEvent(VRPlanckPhysicsEvent::Kind::HitImpulse);
    event.Velocity.z = VRPlanckPhysicsEvent::kMaximumVectorMagnitude + 1.0F;
    CHECK_FALSE(event.IsValid());

    event = ValidEvent(VRPlanckPhysicsEvent::Kind::GripBegin);
    event.WorldRotation.w = 0.5F;
    CHECK_FALSE(event.IsValid());

    event = ValidEvent(VRPlanckPhysicsEvent::Kind::GripUpdate);
    event.WorldRotation.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(event.IsValid());
}

TEST_CASE("PLANCK physics serializes invalid in-memory events without unsafe packing")
{
    auto event = ValidEvent(VRPlanckPhysicsEvent::Kind::HitImpulse);
    event.Position.x = std::numeric_limits<float>::infinity();
    event.NodeNameLength = static_cast<uint8_t>(VRPlanckPhysicsEvent::kMaximumNodeNameBytes + 1);

    TiltedPhoques::Buffer buffer(512);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    event.Serialize(writer);
    VRPlanckPhysicsEvent decoded{};
    TiltedPhoques::Buffer::Reader reader(&buffer);
    decoded.Deserialize(reader);
    CHECK_FALSE(decoded.IsValid());
}

TEST_CASE("PLANCK physics remains an optional negotiated extension")
{
    using namespace SkyrimTogether::Protocol;
    const auto base = BuildVRProductionCapabilities(VRProductionProfile::AvatarSync);
    CHECK_FALSE(HasCapability(base, GameplayCapability::PlanckPhysicsInterface002));

    const auto planck = BuildVRProductionCapabilities(
        VRProductionProfile::AvatarSync, ToMask(GameplayCapability::PlanckPhysicsInterface002));
    CHECK(HasCapability(planck, GameplayCapability::PlanckPhysicsInterface002));
    CHECK(CanAdmitGameplayClient(planck));
}

TEST_CASE("PLANCK operational bridge admission requires every rich interface002 feature")
{
    using namespace SkyrimTogetherVR::PlanckBridge;

    static_assert(std::is_same_v<DiscardLocalEventsFn, Result (*)(std::uint64_t) noexcept>);
    CHECK(kAbiRevision == 3);
    Capabilities capabilities{};
    capabilities.AbiRevision = kAbiRevision;
    capabilities.InterfaceRevision = kPlanckInterfaceRevision;
    capabilities.Features = kRequiredFeatures;
    capabilities.BridgeEpoch = 1;
    CHECK(IsCompatibleCapabilities(capabilities));

    for (std::uint64_t feature = 1; feature != 0; feature <<= 1)
    {
        if ((kRequiredFeatures & feature) == 0)
            continue;
        capabilities.Features = kRequiredFeatures & ~feature;
        CHECK_FALSE(IsCompatibleCapabilities(capabilities));
    }

    capabilities.Features = kRequiredFeatures;
    capabilities.BridgeEpoch = 0;
    CHECK_FALSE(IsCompatibleCapabilities(capabilities));
}

TEST_CASE("PLANCK relay policy deduplicates events and follows the server PVP decision")
{
    using namespace SkyrimTogether::PlanckPhysicsPolicy;
    CHECK(IsStrictlyNewEvent(10, 9, true));
    CHECK_FALSE(IsStrictlyNewEvent(9, 9, true));
    CHECK_FALSE(IsStrictlyNewEvent(0, 0, false));
    CHECK_FALSE(CanRoutePlayerTarget(false));
    CHECK(CanRoutePlayerTarget(true));
}

TEST_CASE("PLANCK remote retry policy preserves FIFO order and eventually terminalizes")
{
    using namespace SkyrimTogether::PlanckPhysicsPolicy;
    CHECK(ShouldAppendRemoteRetry(true, 10, 11));
    CHECK_FALSE(ShouldAppendRemoteRetry(true, 10, 10));
    CHECK_FALSE(ShouldAppendRemoteRetry(true, 10, 9));
    CHECK_FALSE(ShouldAppendRemoteRetry(false, 0, 1));

    CHECK_FALSE(IsRemoteRetryExpired(19, 20, 4.9, 5.0));
    CHECK(IsRemoteRetryExpired(20, 20, 0.0, 5.0));
    CHECK(IsRemoteRetryExpired(1, 20, 5.0, 5.0));
}

TEST_CASE("PLANCK bridge queue pressure retries while invalid submissions remain terminal")
{
    using namespace SkyrimTogetherVR::PlanckBridge;

    CHECK(MapPlanckSubmitResult(kPlanckResultQueueFull) == Result::Busy);
    CHECK(MapPlanckSubmitResult(kPlanckResultInvalidRequest) == Result::Rejected);
    CHECK(MapPlanckSubmitResult(kPlanckResultDuplicate) == Result::Rejected);
    CHECK(MapPlanckSubmitResult(kPlanckResultUnsupported) == Result::Rejected);
    CHECK(MapPlanckSubmitResult(kPlanckResultAccepted) == Result::Accepted);

    CHECK(IsPlanckQueuePressure(kPlanckResultQueueFull));
    CHECK_FALSE(IsPlanckQueuePressure(kPlanckResultInvalidRequest));
    CHECK(IsTerminalPlanckSubmission(kPlanckResultInvalidRequest));
    CHECK(IsTerminalPlanckSubmission(kPlanckResultDuplicate));
    CHECK_FALSE(IsTerminalPlanckSubmission(kPlanckResultQueueFull));
}

TEST_CASE("PLANCK lifecycle fences do not rotate the authenticated wire producer")
{
    using namespace SkyrimTogetherVR::PlanckBridge;

    CHECK_FALSE(ShouldResetWireProducerIdentity(10, 20, 10, 20));
    CHECK(ShouldResetWireProducerIdentity(10, 20, 11, 20));
    CHECK(ShouldResetWireProducerIdentity(10, 20, 10, 21));

    CHECK(CanTransmitLifecycleFencedEvent(false, 7, 7, 42));
    CHECK_FALSE(CanTransmitLifecycleFencedEvent(false, 6, 7, 42));
    CHECK_FALSE(CanTransmitLifecycleFencedEvent(false, 7, 7, 0));
    CHECK_FALSE(CanTransmitLifecycleFencedEvent(true, 7, 7, 42));
}

TEST_CASE("PLANCK cancellation retention fails closed at its explicit bound")
{
    using namespace SkyrimTogetherVR::PlanckBridge;

    CHECK(CanRetainPendingClear(255, 256));
    CHECK_FALSE(CanRetainPendingClear(256, 256));
}

TEST_CASE("PLANCK replacement producer waits for the old token clear")
{
    using namespace SkyrimTogetherVR::PlanckBridge;

    CHECK_FALSE(CanAdmitReplacementProducer(true, 1, 2, 3, 4));
    CHECK(CanAdmitReplacementProducer(false, 1, 2, 3, 4));
    CHECK_FALSE(CanAdmitReplacementProducer(false, 0, 2, 3, 4));
    CHECK_FALSE(CanAdmitReplacementProducer(false, 1, 0, 3, 4));
}
