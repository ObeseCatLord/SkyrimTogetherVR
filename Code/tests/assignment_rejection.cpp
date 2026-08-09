#include <catch2/catch.hpp>

#include <Structs/GameplayCapabilities.h>

#include <entt/entt.hpp>

TEST_CASE("assignment rejection is limited to VR gameplay clients", "[skyrim-vr][assignment]")
{
    using namespace SkyrimTogether::Protocol;

    CHECK_FALSE(CanReceiveAssignmentRejection(kCoreCapabilities));
    CHECK(CanReceiveAssignmentRejection(kCoreCapabilities | ToMask(GameplayCapability::VRPoseRelay)));
}

TEST_CASE("World reserves EnTT entity zero before network assignments", "[skyrim-vr][assignment]")
{
    entt::registry registry;

    const auto worldReservation = registry.create();
    const auto firstNetworkCandidate = registry.create();

    CHECK(entt::to_integral(worldReservation) == 0u);
    CHECK(entt::to_integral(firstNetworkCandidate) != 0u);
}
