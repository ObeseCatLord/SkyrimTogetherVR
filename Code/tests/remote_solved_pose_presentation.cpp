#include <catch2/catch.hpp>

#include "../vr_gameplay_bridge/RemoteSolvedPosePresentation.h"

#include <limits>

namespace Pose = SkyrimTogetherVR::GameplayAdapter::RemoteSolvedPosePresentation;

namespace
{
Pose::Frame BodyFrame(
    const std::uint32_t a_sequence = 11,
    const std::uint64_t a_time = 100)
{
    Pose::Frame frame{};
    frame.ActorHandle = 42;
    frame.ActorAddress = 0x1000;
    frame.RootAddress = 0x2000;
    frame.ServerInstanceNonce = 7;
    frame.ConnectionGeneration = 8;
    frame.LifecycleEpoch = 9;
    frame.RootGeneration = 10;
    frame.Sequence = a_sequence;
    frame.AdmittedAtMilliseconds = a_time;
    frame.NodeMask = 1;
    frame.Nodes[0] = {0.0F, 0.0F, 0.0F, {0.0F, 0.0F, 0.0F, 1.0F}, 1.0F};
    return frame;
}

Pose::Frame JointFrame(
    const std::uint32_t a_sequence = 11,
    const std::uint64_t a_time = 100)
{
    auto frame = BodyFrame(a_sequence, a_time);
    frame.NodeMask = 0;
    frame.JointMask = 1;
    frame.Joints[0] = {0.0F, 0.0F, 0.0F, 1.0F};
    return frame;
}
} // namespace

TEST_CASE("remote solved-pose cache presents one admitted frame immediately", "[vr][pose]")
{
    Pose::FrameCache cache;
    const auto frame = BodyFrame();
    REQUIRE(cache.Admit(frame));

    Pose::PoseSnapshot snapshot{};
    REQUIRE(cache.TrySnapshot(frame.ActorAddress, frame.RootAddress, 110, snapshot));
    REQUIRE(snapshot.Body.Count == 1);
    REQUIRE(snapshot.Body.Newer.Sequence == frame.Sequence);
    REQUIRE(Pose::ResolveSelection(snapshot.Body, 110).Sequence == frame.Sequence);
}

TEST_CASE("remote solved-pose cache interpolates two frames on delayed monotonic time", "[vr][pose]")
{
    Pose::FrameCache cache;
    auto older = BodyFrame(11, 100);
    auto newer = BodyFrame(12, 200);
    newer.Nodes[0] = {10.0F, 20.0F, 30.0F, {0.0F, 0.0F, 1.0F, 0.0F}, 2.0F};
    REQUIRE(cache.Admit(older));
    REQUIRE(cache.Admit(newer));

    Pose::PoseSnapshot snapshot{};
    REQUIRE(cache.TrySnapshot(newer.ActorAddress, newer.RootAddress, 200, snapshot));
    REQUIRE(snapshot.Body.Count == 2);
    REQUIRE(Pose::InterpolationAlpha(snapshot.Body.Older, snapshot.Body.Newer, 200) == Approx(0.5F));
    const auto interpolated = Pose::ResolveSelection(snapshot.Body, 200);
    REQUIRE(interpolated.Nodes[0].X == Approx(5.0F));
    REQUIRE(interpolated.Nodes[0].Y == Approx(10.0F));
    REQUIRE(interpolated.Nodes[0].Z == Approx(15.0F));
    REQUIRE(interpolated.Nodes[0].Scale == Approx(1.5F));
    REQUIRE(interpolated.Nodes[0].Rotation.Z == Approx(0.7071067F));
    REQUIRE(interpolated.Nodes[0].Rotation.W == Approx(0.7071067F));
}

TEST_CASE("body and joint commits retain independent two-frame histories", "[vr][pose]")
{
    Pose::FrameCache cache;
    REQUIRE(cache.Admit(BodyFrame(11, 100)));
    REQUIRE(cache.Admit(JointFrame(11, 101)));
    REQUIRE(cache.Admit(BodyFrame(12, 110)));
    REQUIRE(cache.Admit(JointFrame(12, 111)));

    Pose::PoseSnapshot snapshot{};
    REQUIRE(cache.TrySnapshot(0x1000, 0x2000, 120, snapshot));
    REQUIRE(snapshot.Body.Count == 2);
    REQUIRE(snapshot.Joints.Count == 2);
    REQUIRE(Pose::ChannelFor(snapshot.Body.Newer) == Pose::FrameChannel::Body);
    REQUIRE(Pose::ChannelFor(snapshot.Joints.Newer) == Pose::FrameChannel::Joints);
}

TEST_CASE("cache preserves full 64-bit connection generation", "[vr][pose]")
{
    Pose::FrameCache cache;
    auto frame = BodyFrame();
    frame.ConnectionGeneration = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 17;
    REQUIRE(cache.Admit(frame));

    Pose::PoseSnapshot snapshot{};
    REQUIRE(cache.TrySnapshot(frame.ActorAddress, frame.RootAddress, 110, snapshot));
    REQUIRE(snapshot.Identity.ConnectionGeneration == frame.ConnectionGeneration);
    REQUIRE(snapshot.Body.Newer.ConnectionGeneration == frame.ConnectionGeneration);
}

TEST_CASE("cache rejects unrelated actors and mismatched identity", "[vr][pose]")
{
    Pose::FrameCache cache;
    const auto frame = BodyFrame();
    REQUIRE(cache.Admit(frame));
    REQUIRE_FALSE(Pose::MatchesFastMetadata(frame.ActorAddress, frame.RootAddress, 0x3000, frame.RootAddress));

    Pose::PoseSnapshot snapshot{};
    REQUIRE_FALSE(cache.TrySnapshot(0x3000, frame.RootAddress, 110, snapshot));
    REQUIRE_FALSE(cache.TrySnapshot(frame.ActorAddress, 0x4000, 110, snapshot));

    auto replacement = frame;
    replacement.RootGeneration++;
    replacement.Sequence++;
    replacement.AdmittedAtMilliseconds++;
    REQUIRE(cache.Admit(replacement));
    REQUIRE(cache.TrySnapshot(frame.ActorAddress, frame.RootAddress, 120, snapshot));
    REQUIRE(snapshot.Body.Count == 1);
    REQUIRE(snapshot.Identity.RootGeneration == replacement.RootGeneration);
}

TEST_CASE("avatar eviction prevents allocator-address reuse from presenting stale pose", "[vr][pose]")
{
    Pose::FrameCache cache;
    const auto retired = BodyFrame();
    REQUIRE(cache.Admit(retired));
    cache.Evict(retired.ActorHandle);

    Pose::PoseSnapshot snapshot{};
    REQUIRE_FALSE(cache.TrySnapshot(retired.ActorAddress, retired.RootAddress, 110, snapshot));

    auto replacement = retired;
    replacement.ActorHandle++;
    replacement.ServerInstanceNonce++;
    replacement.LifecycleEpoch++;
    replacement.Sequence++;
    replacement.AdmittedAtMilliseconds++;
    REQUIRE(cache.Admit(replacement));
    REQUIRE(cache.TrySnapshot(replacement.ActorAddress, replacement.RootAddress, 120, snapshot));
    REQUIRE(snapshot.Identity.ActorHandle == replacement.ActorHandle);
    REQUIRE(snapshot.Identity.LifecycleEpoch == replacement.LifecycleEpoch);
}

TEST_CASE("snapshot scan skips stale reused actor-address slots with a different root", "[vr][pose]")
{
    Pose::FrameCache cache;
    auto stale = BodyFrame(11, 100);
    stale.RootAddress = 0x3000;
    REQUIRE(cache.Admit(stale));

    auto current = BodyFrame(12, 101);
    current.ActorHandle = 43;
    current.ServerInstanceNonce = 17;
    current.LifecycleEpoch = 19;
    REQUIRE(cache.Admit(current));

    Pose::PoseSnapshot snapshot{};
    REQUIRE(cache.TrySnapshot(current.ActorAddress, current.RootAddress, 110, snapshot));
    REQUIRE(snapshot.Identity.ActorHandle == current.ActorHandle);
    REQUIRE(snapshot.Identity.RootAddress == current.RootAddress);
    REQUIRE(snapshot.Body.Newer.Sequence == current.Sequence);
}

TEST_CASE("presentation policy rejects stale player and ragdoll state", "[vr][pose]")
{
    const auto frame = BodyFrame();
    REQUIRE(Pose::IsFresh(frame, frame.AdmittedAtMilliseconds + Pose::kMaximumFrameAgeMilliseconds));
    REQUIRE_FALSE(Pose::IsFresh(frame, frame.AdmittedAtMilliseconds + Pose::kMaximumFrameAgeMilliseconds + 1));
    REQUIRE(Pose::ShouldPresentActor(false, false));
    REQUIRE_FALSE(Pose::ShouldPresentActor(true, false));
    REQUIRE_FALSE(Pose::ShouldPresentActor(false, true));
}

TEST_CASE("Character UpdateAnimation target constants are exact and exclude PlayerCharacter", "[vr][pose]")
{
    constexpr std::array<std::uint8_t, 22> expectedPrefix{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0x29, 0x74, 0x24, 0x20,
        0x48, 0x8B, 0xD9, 0x0F, 0x28, 0xF1, 0xE8, 0x1A, 0x02, 0x00, 0x00};
    REQUIRE(Pose::HasPinnedTargetConfiguration());
    REQUIRE(Pose::kCharacterUpdateAnimationSlotVrRva ==
            Pose::kCharacterVtableVrRva + Pose::kUpdateAnimationSlot * sizeof(void*));
    REQUIRE(Pose::kActorUpdateAnimationVrEndRva - Pose::kActorUpdateAnimationVrRva == 0x7C);
    REQUIRE(Pose::kActorUpdateAnimationVrPrefix == expectedPrefix);
    REQUIRE(Pose::ShouldPatchCharacterVtableSlot(0x16D71C8, 0x16D71C8, 0x5E1F10, 0x5E1F10, false));
    REQUIRE_FALSE(Pose::ShouldPatchCharacterVtableSlot(0x16D71C8, 0x16D71C8, 0x5E1F10, 0x5E1F10, true));
}

TEST_CASE("direct slot detach clears callable state only after restoration and quiescence", "[vr][pose]")
{
    REQUIRE(Pose::CanClearCallableState(true, true, 0, true));
    REQUIRE_FALSE(Pose::CanClearCallableState(false, true, 0, true));
    REQUIRE_FALSE(Pose::CanClearCallableState(true, false, 0, true));
    REQUIRE_FALSE(Pose::CanClearCallableState(true, true, 1, true));
    REQUIRE_FALSE(Pose::CanClearCallableState(true, true, 0, false));
}
