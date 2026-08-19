#pragma once

#include <cstdint>

namespace SkyrimTogether::ServerAuthorityPolicy
{
// Event IDs are protocol-visible mutation identities.  A mutation may only
// commit after a non-zero identity has been reserved from its service ledger.
[[nodiscard]] constexpr bool CanCommitEventMutation(const std::uint32_t aEventId) noexcept
{
    return aEventId != 0;
}

struct ObjectInteractionAuthority
{
    bool HasAuthenticatedSender{};
    bool HasOwnedPlayerCharacter{};
    bool RequestedTargetIsCanonical{};
    bool RequestedCellIsCanonical{};
    bool RequestedActivatorIsCanonical{};
    bool SenderCanObserveCharacter{};
    bool SenderCanObserveTarget{};
    bool CharacterCanObserveTarget{};
};

[[nodiscard]] constexpr bool IsAuthorizedObjectInteraction(
    const ObjectInteractionAuthority& acAuthority) noexcept
{
    return acAuthority.HasAuthenticatedSender && acAuthority.HasOwnedPlayerCharacter &&
           acAuthority.RequestedTargetIsCanonical && acAuthority.RequestedCellIsCanonical &&
           acAuthority.RequestedActivatorIsCanonical && acAuthority.SenderCanObserveCharacter &&
           acAuthority.SenderCanObserveTarget && acAuthority.CharacterCanObserveTarget;
}
} // namespace SkyrimTogether::ServerAuthorityPolicy
