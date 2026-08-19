#include <catch2/catch.hpp>

#include <vr_common/VRGameplayBridge.h>
#include <vr_gameplay_bridge/remote_actor_admission_policy.h>

#include <limits>

namespace
{
using namespace SkyrimTogetherVR::GameplayBridge;
using namespace SkyrimTogetherVR::GameplayAdapter::RemoteActorAdmissionPolicy;
}

TEST_CASE("VR remote avatar create flags reject unknown and conflicting ownership", "[skyrim-vr][remote-authority]")
{
    REQUIRE(IsValidRemoteAvatarCreateFlags(0));
    REQUIRE(IsValidRemoteAvatarCreateFlags(UseExistingReference));
    REQUIRE(IsValidRemoteAvatarCreateFlags(PlayerAvatar));
    REQUIRE(IsValidRemoteAvatarCreateFlags(PlayerSummon));
    REQUIRE(IsValidRemoteAvatarCreateFlags(UseExistingReference | PlayerSummon));
    REQUIRE_FALSE(IsValidRemoteAvatarCreateFlags(PlayerAvatar | PlayerSummon));
    REQUIRE_FALSE(IsValidRemoteAvatarCreateFlags(1u << 31));
}

TEST_CASE("VR remote actor authority retains only applied health transitions", "[skyrim-vr][remote-authority]")
{
    RemoteActorAuthorityState state{};
    REQUIRE_FALSE(state.HasHealth);
    REQUIRE_FALSE(state.IsDead);

    REQUIRE(ApplyRemoteActorAuthorityTransition(state, RemoteActorAuthorityTransition::InitialHealth, 100.0F));
    REQUIRE(state.HasHealth);
    REQUIRE(state.Health == 100.0F);
    REQUIRE_FALSE(state.IsDead);

    REQUIRE(ApplyRemoteActorAuthorityTransition(state, RemoteActorAuthorityTransition::SetHealth, 80.0F));
    REQUIRE(state.Health == 80.0F);
    REQUIRE_FALSE(state.IsDead);

    REQUIRE(ApplyRemoteActorAuthorityTransition(state, RemoteActorAuthorityTransition::ModifyHealth, 55.0F));
    REQUIRE(state.Health == 55.0F);
    REQUIRE_FALSE(state.IsDead);

    REQUIRE(ApplyRemoteActorAuthorityTransition(state, RemoteActorAuthorityTransition::Death, 0.0F));
    REQUIRE(state.Health == 0.0F);
    REQUIRE(state.IsDead);

    REQUIRE(ApplyRemoteActorAuthorityTransition(state, RemoteActorAuthorityTransition::Respawn, 100.0F));
    REQUIRE(state.Health == 100.0F);
    REQUIRE_FALSE(state.IsDead);
    REQUIRE_FALSE(ApplyRemoteActorAuthorityTransition(state, RemoteActorAuthorityTransition::SetHealth, std::numeric_limits<float>::infinity()));
    REQUIRE(state.Health == 100.0F);
}

TEST_CASE("VR remote avatar admission requires observed AI disablement", "[skyrim-vr][remote-authority]")
{
    REQUIRE_FALSE(CanPublishRemoteAvatar(AiDisableAdmission::NotAttempted));
    REQUIRE_FALSE(CanPublishRemoteAvatar(AiDisableAdmission::DisableRequested));
    REQUIRE(CanPublishRemoteAvatar(AiDisableAdmission::ConfirmedDisabled));
    REQUIRE_FALSE(CanPublishRemoteAvatar(AiDisableAdmission::Rejected));
}

TEST_CASE("VR remote avatar failed admission restores existing AI and retires only registered actors", "[skyrim-vr][remote-authority]")
{
    REQUIRE_FALSE(MustRestoreExistingAiOnFailedCreate(false, AiDisableAdmission::NotAttempted));
    REQUIRE_FALSE(MustRestoreExistingAiOnFailedCreate(true, AiDisableAdmission::NotAttempted));
    REQUIRE(MustRestoreExistingAiOnFailedCreate(true, AiDisableAdmission::DisableRequested));
    REQUIRE(MustRestoreExistingAiOnFailedCreate(true, AiDisableAdmission::ConfirmedDisabled));
    REQUIRE(MustRestoreExistingAiOnFailedCreate(true, AiDisableAdmission::Rejected));

    REQUIRE_FALSE(MustRetireRegisteredActorOnFailedCreate(false));
    REQUIRE(MustRetireRegisteredActorOnFailedCreate(true));
    REQUIRE_FALSE(CanReleaseRetiredActorAfterRestoration(false));
    REQUIRE(CanReleaseRetiredActorAfterRestoration(true));

    REQUIRE_FALSE(ShouldLogAiAdmissionFailure(0));
    REQUIRE(ShouldLogAiAdmissionFailure(1));
    REQUIRE_FALSE(ShouldLogAiAdmissionFailure(kAiAdmissionLogInterval - 1));
    REQUIRE(ShouldLogAiAdmissionFailure(kAiAdmissionLogInterval));
}
