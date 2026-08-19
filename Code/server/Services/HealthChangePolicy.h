#pragma once

#include <cstdint>

namespace SkyrimTogether::HealthChangePolicy
{
enum class VrRequestLane : std::uint8_t
{
    Reject,
    OwnerState,
    PhysicalNpcDamage,
};

[[nodiscard]] constexpr VrRequestLane ClassifyVrRequest(
    const bool a_hasGameplayCapability,
    const bool a_senderOwnsTarget,
    const std::uint32_t a_attackerId,
    const std::uint64_t a_actionNonce) noexcept
{
    if (!a_hasGameplayCapability)
        return VrRequestLane::Reject;

    if (a_attackerId == 0 && a_actionNonce == 0)
        return a_senderOwnsTarget ? VrRequestLane::OwnerState : VrRequestLane::Reject;

    if (a_attackerId != 0 && a_actionNonce != 0)
        return VrRequestLane::PhysicalNpcDamage;

    return VrRequestLane::Reject;
}
} // namespace SkyrimTogether::HealthChangePolicy
