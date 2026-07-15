#include <catch2/catch.hpp>

#include <VRCanonicalEntityIdentity.h>

namespace CanonicalEntity = SkyrimTogetherVR::CanonicalEntity;

TEST_CASE("VR canonical entity identity preserves packed EnTT values", "[skyrim-vr][canonical-entity]")
{
    using Traits = CanonicalEntity::EntityTraits;

    const auto verifyRoundTrip = [](const entt::entity aEntity)
    {
        CanonicalEntity::BridgeIdentity identity{};
        REQUIRE(CanonicalEntity::TrySplitServerId(entt::to_integral(aEntity), identity));

        std::uint32_t packed{};
        REQUIRE(CanonicalEntity::TryJoinServerId(identity.EntityId, identity.EntityGeneration, packed));
        CHECK(packed == entt::to_integral(aEntity));
    };

    verifyRoundTrip(Traits::construct(0, 0));
    verifyRoundTrip(Traits::construct(42, 0));
    verifyRoundTrip(Traits::construct(42, 1));
    verifyRoundTrip(Traits::construct(
        static_cast<Traits::entity_type>(CanonicalEntity::kMaximumEntityId - 1),
        static_cast<Traits::version_type>(CanonicalEntity::kMaximumEntityGeneration - 1)));

    CanonicalEntity::BridgeIdentity first{};
    CanonicalEntity::BridgeIdentity reused{};
    REQUIRE(CanonicalEntity::TrySplitServerId(entt::to_integral(Traits::construct(42, 0)), first));
    REQUIRE(CanonicalEntity::TrySplitServerId(entt::to_integral(Traits::construct(42, 1)), reused));
    CHECK(first.EntityId == reused.EntityId);
    CHECK(first.EntityGeneration != reused.EntityGeneration);
}

TEST_CASE("VR canonical entity identity rejects EnTT sentinels and invalid bridge values", "[skyrim-vr][canonical-entity]")
{
    using Traits = CanonicalEntity::EntityTraits;

    CanonicalEntity::BridgeIdentity identity{};
    CHECK_FALSE(CanonicalEntity::TrySplitServerId(
        entt::to_integral(Traits::construct(static_cast<Traits::entity_type>(CanonicalEntity::kMaximumEntityId), 0)), identity));
    CHECK_FALSE(CanonicalEntity::TrySplitServerId(
        entt::to_integral(Traits::construct(0, static_cast<Traits::version_type>(CanonicalEntity::kMaximumEntityGeneration))), identity));

    std::uint32_t packed{};
    CHECK_FALSE(CanonicalEntity::TryJoinServerId(0, 1, packed));
    CHECK_FALSE(CanonicalEntity::TryJoinServerId(CanonicalEntity::kMaximumEntityId + 1, 1, packed));
    CHECK_FALSE(CanonicalEntity::TryJoinServerId(1, 0, packed));
    CHECK_FALSE(CanonicalEntity::TryJoinServerId(1, CanonicalEntity::kMaximumEntityGeneration + 1, packed));
}

TEST_CASE("VR canonical entity generation comparison handles rollover", "[skyrim-vr][canonical-entity]")
{
    using enum CanonicalEntity::GenerationOrder;

    CHECK(CanonicalEntity::CompareGenerations(8, 8) == Same);
    CHECK(CanonicalEntity::CompareGenerations(9, 8) == Newer);
    CHECK(CanonicalEntity::CompareGenerations(8, 9) == OlderOrAmbiguous);
    CHECK(CanonicalEntity::CompareGenerations(1, CanonicalEntity::kMaximumEntityGeneration) == Newer);
    CHECK(CanonicalEntity::CompareGenerations(CanonicalEntity::kMaximumEntityGeneration, 1) == OlderOrAmbiguous);
    CHECK(CanonicalEntity::CompareGenerations(2048, 1) == Newer);
    CHECK(CanonicalEntity::CompareGenerations(2049, 1) == OlderOrAmbiguous);
    CHECK(CanonicalEntity::CompareGenerations(0, 1) == Invalid);
}

TEST_CASE("VR canonical entity permits visibility recreation only after destroy ordering", "[skyrim-vr][canonical-entity]")
{
    CHECK(CanonicalEntity::CanCreateAfterDestroyedGeneration(7, 12, 7, 11));
    CHECK_FALSE(CanonicalEntity::CanCreateAfterDestroyedGeneration(7, 11, 7, 11));
    CHECK_FALSE(CanonicalEntity::CanCreateAfterDestroyedGeneration(7, 10, 7, 11));
    CHECK(CanonicalEntity::CanCreateAfterDestroyedGeneration(8, 1, 7, 99));
    CHECK_FALSE(CanonicalEntity::CanCreateAfterDestroyedGeneration(6, 100, 7, 99));
}
