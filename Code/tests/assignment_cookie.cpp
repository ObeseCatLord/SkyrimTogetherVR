#include <catch2/catch.hpp>

#include <Services/AssignmentCookie.h>

#include <cstdint>

TEST_CASE("Assignment cookies keep local-player and NPC flows disjoint", "[skyrim-vr][assignment]")
{
    auto localCookie = SkyrimTogetherVR::AssignmentCookie::kFirstLocalPlayer;
    auto npcCookie = SkyrimTogetherVR::AssignmentCookie::kFirstNpc;
    for (std::uint32_t index = 0; index < 1024; ++index) {
        const auto local = SkyrimTogetherVR::AssignmentCookie::TakeLocalPlayer(localCookie);
        const auto npc = SkyrimTogetherVR::AssignmentCookie::TakeNpc(npcCookie);
        REQUIRE((local & 1u) == 1u);
        REQUIRE((npc & 1u) == 0u);
        REQUIRE(local != npc);
    }
}

TEST_CASE("Assignment cookie cycles wrap without emitting zero", "[skyrim-vr][assignment]")
{
    auto localCookie = SkyrimTogetherVR::AssignmentCookie::kLastLocalPlayer;
    REQUIRE(SkyrimTogetherVR::AssignmentCookie::TakeLocalPlayer(localCookie) ==
            SkyrimTogetherVR::AssignmentCookie::kLastLocalPlayer);
    REQUIRE(localCookie == SkyrimTogetherVR::AssignmentCookie::kFirstLocalPlayer);

    auto npcCookie = SkyrimTogetherVR::AssignmentCookie::kLastNpc;
    REQUIRE(SkyrimTogetherVR::AssignmentCookie::TakeNpc(npcCookie) ==
            SkyrimTogetherVR::AssignmentCookie::kLastNpc);
    REQUIRE(npcCookie == SkyrimTogetherVR::AssignmentCookie::kFirstNpc);
}
