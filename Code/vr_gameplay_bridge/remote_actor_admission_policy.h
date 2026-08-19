#pragma once

#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::RemoteActorAdmissionPolicy
{
enum class AiDisableAdmission : std::uint8_t
{
    NotAttempted,
    DisableRequested,
    ConfirmedDisabled,
    Rejected,
};

inline constexpr std::uint64_t kAiAdmissionLogInterval = 64;

// A remote actor is publishable only after this create operation has requested
// AI disablement and then observed the disabled state.
[[nodiscard]] constexpr bool CanPublishRemoteAvatar(const AiDisableAdmission a_admission) noexcept
{
    return a_admission == AiDisableAdmission::ConfirmedDisabled;
}

// Existing references are caller-owned. Once a create path has requested an
// AI transition, a rejected create must restore the captured original state.
[[nodiscard]] constexpr bool MustRestoreExistingAiOnFailedCreate(
    const bool a_usesExistingReference,
    const AiDisableAdmission a_admission) noexcept
{
    return a_usesExistingReference && a_admission != AiDisableAdmission::NotAttempted;
}

// Registration transfers cleanup to the two-phase authority retirement path.
[[nodiscard]] constexpr bool MustRetireRegisteredActorOnFailedCreate(const bool a_authorityRegistered) noexcept
{
    return a_authorityRegistered;
}

// A quiescent registry lease remains closed when a required restoration fails.
// Releasing it would make an actor with unknown caller-owned state usable.
[[nodiscard]] constexpr bool CanReleaseRetiredActorAfterRestoration(const bool a_restorationSucceeded) noexcept
{
    return a_restorationSucceeded;
}

[[nodiscard]] constexpr bool ShouldLogAiAdmissionFailure(const std::uint64_t a_total) noexcept
{
    return a_total == 1 || (a_total != 0 && a_total % kAiAdmissionLogInterval == 0);
}
} // namespace SkyrimTogetherVR::GameplayAdapter::RemoteActorAdmissionPolicy
