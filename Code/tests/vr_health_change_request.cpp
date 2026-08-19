#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <Messages/RequestHealthChangeBroadcast.h>
#include <Structs/ActorValues.h>
#include <server/Services/HealthChangePolicy.h>
#include <server/Services/ServerAuthorityPolicy.h>

#include <catch2/catch.hpp>

using namespace TiltedPhoques;

TEST_CASE("Actor health ledger updates mapped health through a writable hopscotch iterator", "[server][vr-health-change]")
{
    constexpr std::uint32_t healthActorValue = 24;
    ActorValues values{};
    values.ActorValuesList.emplace(healthActorValue, 100.0F);

    const auto healthIt = values.ActorValuesList.find(healthActorValue);
    REQUIRE(healthIt != values.ActorValuesList.end());

    healthIt.value() = 75.0F;
    REQUIRE(healthIt.value() == 75.0F);
}

TEST_CASE("VR health-change request preserves attacker authentication fields", "[encoding][vr-health-change]")
{
    Buffer buffer(256);
    RequestHealthChangeBroadcast sent{};
    sent.Id = 0x1234;
    sent.DeltaHealth = -37.5F;
    sent.AttackerId = 0x5678;
    sent.ActionNonce = 0x1122334455667788ull;

    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Buffer::Reader reader(&buffer);
    std::uint64_t opcode{};
    reader.ReadBits(opcode, 8);

    RequestHealthChangeBroadcast received{};
    received.DeserializeRaw(reader);

    REQUIRE(sent == received);
    REQUIRE(opcode == static_cast<std::uint64_t>(RequestHealthChangeBroadcast::Opcode));

    ++received.AttackerId;
    REQUIRE_FALSE(sent == received);
    received = sent;
    ++received.ActionNonce;
    REQUIRE_FALSE(sent == received);
}

TEST_CASE("VR health-change request lanes separate owner state from physical damage", "[server][vr-health-change]")
{
    using SkyrimTogether::HealthChangePolicy::ClassifyVrRequest;
    using SkyrimTogether::HealthChangePolicy::VrRequestLane;

    REQUIRE(ClassifyVrRequest(true, true, 0, 0) == VrRequestLane::OwnerState);
    REQUIRE(ClassifyVrRequest(true, false, 7, 9) == VrRequestLane::PhysicalNpcDamage);
    REQUIRE(ClassifyVrRequest(true, true, 7, 9) == VrRequestLane::PhysicalNpcDamage);

    REQUIRE(ClassifyVrRequest(false, true, 0, 0) == VrRequestLane::Reject);
    REQUIRE(ClassifyVrRequest(true, false, 0, 0) == VrRequestLane::Reject);
    REQUIRE(ClassifyVrRequest(true, false, 7, 0) == VrRequestLane::Reject);
    REQUIRE(ClassifyVrRequest(true, false, 0, 9) == VrRequestLane::Reject);
}

TEST_CASE("Server authority policies reject unreserved events and spoofed object routing", "[server][vr-server-authority]")
{
    using SkyrimTogether::ServerAuthorityPolicy::CanCommitEventMutation;
    using SkyrimTogether::ServerAuthorityPolicy::IsAuthorizedObjectInteraction;
    using SkyrimTogether::ServerAuthorityPolicy::ObjectInteractionAuthority;

    REQUIRE_FALSE(CanCommitEventMutation(0));
    REQUIRE(CanCommitEventMutation(1));

    const ObjectInteractionAuthority canonical{
        true, true, true, true, true, true, true, true,
    };
    REQUIRE(IsAuthorizedObjectInteraction(canonical));

    auto spoofedActivator = canonical;
    spoofedActivator.RequestedActivatorIsCanonical = false;
    REQUIRE_FALSE(IsAuthorizedObjectInteraction(spoofedActivator));

    auto spoofedCell = canonical;
    spoofedCell.RequestedCellIsCanonical = false;
    REQUIRE_FALSE(IsAuthorizedObjectInteraction(spoofedCell));

    auto spoofedTarget = canonical;
    spoofedTarget.RequestedTargetIsCanonical = false;
    REQUIRE_FALSE(IsAuthorizedObjectInteraction(spoofedTarget));
}
