#include <catch2/catch.hpp>

#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>
#include <glm/glm.hpp>

#include <server/Services/RevisionedRecoveryPolicy.h>
#include <Services/VRActorReplicationService.h>
#include <Structs/GameplayCapabilities.h>

#include <limits>

TEST_CASE("revisioned canonical recovery rejects future requests and late responses", "[server][recovery]")
{
    using namespace SkyrimTogether::RevisionedRecovery;

    CHECK(CanIssueSnapshot(5, 0));
    CHECK(CanIssueSnapshot(5, 5));
    CHECK_FALSE(CanIssueSnapshot(5, 6));
    CHECK_FALSE(CanIssueSnapshot(std::numeric_limits<std::uint64_t>::max(), 0));

    CHECK_FALSE(IsLateOrStaleResponse(7, 7, 3, 4));
    CHECK(IsLateOrStaleResponse(7, 8, 3, 4));
    CHECK(IsLateOrStaleResponse(7, 7, 4, 4));
    CHECK(IsLateOrStaleResponse(7, 7, 4, 3));
}

TEST_CASE("revisioned mount recovery accepts only fresh canonical responses", "[client][recovery]")
{
    using namespace SkyrimTogether::Protocol::RevisionedCanonicalRecoveryPolicy;

    CHECK(CanAcceptCanonicalResponse(41, 41, 5, 5, 6));
    CHECK_FALSE(CanAcceptCanonicalResponse(41, 42, 5, 5, 6));
    CHECK_FALSE(CanAcceptCanonicalResponse(41, 41, 5, 5, 5));
    CHECK_FALSE(CanAcceptCanonicalResponse(41, 41, 5, 6, 6));
    CHECK_FALSE(CanAcceptCanonicalResponse(0, 0, 0, 0, 1));
}

TEST_CASE("revisioned mount recovery preserves dismount and bounded retry semantics", "[client][recovery]")
{
    using namespace SkyrimTogether::Protocol::RevisionedCanonicalRecoveryPolicy;

    CHECK(IsAuthoritativeDismount(0));
    CHECK_FALSE(IsAuthoritativeDismount(1));
    CHECK(ShouldRetryMountApplication(0, 3));
    CHECK(ShouldRetryMountApplication(2, 3));
    CHECK_FALSE(ShouldRetryMountApplication(3, 3));
    CHECK_FALSE(ShouldRetryMountApplication(0, 0));
}

TEST_CASE("revisioned mount recovery request IDs stay nonzero across wrap", "[client][recovery]")
{
    using namespace SkyrimTogether::Protocol::RevisionedCanonicalRecoveryPolicy;

    std::uint32_t next = (std::numeric_limits<std::uint32_t>::max)();
    CHECK(NextNonZeroRequestId(next) == (std::numeric_limits<std::uint32_t>::max)());
    CHECK(next == 1);
    CHECK(NextNonZeroRequestId(next) == 1);
    CHECK(next == 2);

    next = 0;
    CHECK(NextNonZeroRequestId(next) == 1);
    CHECK(next == 2);
}

TEST_CASE("owner-scoped quest recovery rejects cross-party owners and late snapshots", "[quest][recovery]")
{
    using namespace SkyrimTogether::Protocol::RevisionedCanonicalRecoveryPolicy;

    CHECK(CanAuthorizeQuestOwner(10, 20, 30, 30));
    CHECK_FALSE(CanAuthorizeQuestOwner(10, 20, 30, 31));
    CHECK_FALSE(CanAuthorizeQuestOwner(0, 20, 30, 30));
    CHECK_FALSE(CanAuthorizeQuestOwner(10, 0, 30, 30));

    CHECK(DoesQuestUpdateSupersedeSnapshot(20, 40, 20, 41));
    CHECK_FALSE(DoesQuestUpdateSupersedeSnapshot(20, 40, 21, 41));
    CHECK_FALSE(DoesQuestUpdateSupersedeSnapshot(20, 40, 20, 40));

    CHECK(CanCommitQuestSnapshot(20, 20, 50, 50, 39, 39, 40, 41));
    CHECK_FALSE(CanCommitQuestSnapshot(20, 21, 50, 50, 39, 39, 40, 41));
    CHECK_FALSE(CanCommitQuestSnapshot(20, 20, 50, 51, 39, 39, 40, 41));
    CHECK_FALSE(CanCommitQuestSnapshot(20, 20, 50, 50, 39, 39, 42, 41));
}

TEST_CASE("revisioned mount recovery commits only the matching native completion", "[client][recovery]")
{
    using namespace SkyrimTogetherVR::ActorReplicationRecovery;

    CHECK(CanCommitCanonicalMountCompletion(true, true, 9));
    CHECK_FALSE(CanCommitCanonicalMountCompletion(false, true, 9));
    CHECK_FALSE(CanCommitCanonicalMountCompletion(true, false, 9));
    CHECK_FALSE(CanCommitCanonicalMountCompletion(true, true, 0));

    CHECK(MustRefreshCanonicalMountRecovery(false, true, false));
    CHECK(MustRefreshCanonicalMountRecovery(true, false, false));
    CHECK_FALSE(MustRefreshCanonicalMountRecovery(true, true, false));
    CHECK_FALSE(MustRefreshCanonicalMountRecovery(false, false, true));
}
