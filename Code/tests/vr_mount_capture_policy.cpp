#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/WaypointHooks.h>

namespace
{
namespace Mount = SkyrimTogetherVR::GameplayAdapter::MountCapturePolicy;
}

TEST_CASE("event-assisted mount capture only publishes a new valid HorseEnter mount", "[skyrim-vr][mount]")
{
    REQUIRE_FALSE(Mount::ShouldPublishEventAssistedMount(19, true, false, false));
    REQUIRE_FALSE(Mount::ShouldPublishEventAssistedMount(Mount::kHorseEnterAnimationEventId, false, false, false));
    REQUIRE(Mount::ShouldPublishEventAssistedMount(Mount::kHorseEnterAnimationEventId, true, false, false));
    REQUIRE_FALSE(Mount::ShouldPublishEventAssistedMount(Mount::kHorseEnterAnimationEventId, true, true, true));
    REQUIRE(Mount::ShouldPublishEventAssistedMount(Mount::kHorseEnterAnimationEventId, true, true, false));
}
