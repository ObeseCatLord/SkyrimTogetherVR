#pragma once

#include <cstdint>

/**
 * A connection is not ready for gameplay until the local CommonLib bridge has
 * rebuilt its assignment and avatar state for the current lifecycle epoch.
 */
enum class VRRehydrationState : std::uint8_t
{
    Offline,
    Retiring,
    Stable,
    Connecting,
    Authenticated,
    Bootstrap,
    Assigned,
    DomainsActive,
    Ready,
    Failed,
};

enum class VRRehydrationProfile : std::uint8_t
{
    ConnectionOnly,
    AvatarSync,
    Gameplay,
};

[[nodiscard]] constexpr const char* VRRehydrationProfileName(const VRRehydrationProfile aProfile) noexcept
{
    switch (aProfile)
    {
    case VRRehydrationProfile::ConnectionOnly: return "connection_only";
    case VRRehydrationProfile::AvatarSync: return "avatar_sync";
    case VRRehydrationProfile::Gameplay: return "gameplay";
    }
    return "unknown";
}

[[nodiscard]] constexpr bool VRRehydrationProfileRequiresAvatar(const VRRehydrationProfile aProfile) noexcept
{
    return aProfile != VRRehydrationProfile::ConnectionOnly;
}

[[nodiscard]] constexpr const char* VRRehydrationStateName(const VRRehydrationState aState) noexcept
{
    switch (aState)
    {
    case VRRehydrationState::Offline: return "offline";
    case VRRehydrationState::Retiring: return "retiring";
    case VRRehydrationState::Stable: return "stable";
    case VRRehydrationState::Connecting: return "connecting";
    case VRRehydrationState::Authenticated: return "authenticated";
    case VRRehydrationState::Bootstrap: return "bootstrap";
    case VRRehydrationState::Assigned: return "assigned";
    case VRRehydrationState::DomainsActive: return "domains_active";
    case VRRehydrationState::Ready: return "ready";
    case VRRehydrationState::Failed: return "failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr bool IsVRRehydrationTerminal(const VRRehydrationState aState) noexcept
{
    return aState == VRRehydrationState::Failed;
}

[[nodiscard]] constexpr double VRRehydrationDeadlineSeconds(const VRRehydrationState aState) noexcept
{
    switch (aState)
    {
    case VRRehydrationState::Retiring: return 15.0;
    case VRRehydrationState::Connecting: return 30.0;
    case VRRehydrationState::Authenticated: return 5.0;
    case VRRehydrationState::Bootstrap: return 45.0;
    case VRRehydrationState::Assigned: return 15.0;
    case VRRehydrationState::DomainsActive: return 10.0;
    default: return 0.0;
    }
}

[[nodiscard]] constexpr bool CanTransitionVRRehydrationState(
    const VRRehydrationState aFrom, const VRRehydrationState aTo) noexcept
{
    if (aFrom == VRRehydrationState::Failed)
        return aTo == VRRehydrationState::Failed || aTo == VRRehydrationState::Offline ||
               aTo == VRRehydrationState::Stable;

    if (aFrom == aTo || aTo == VRRehydrationState::Failed || aTo == VRRehydrationState::Retiring)
        return true;

    switch (aFrom)
    {
    case VRRehydrationState::Offline:
        return aTo == VRRehydrationState::Stable || aTo == VRRehydrationState::Connecting ||
               aTo == VRRehydrationState::Bootstrap ||
               aTo == VRRehydrationState::Authenticated;
    case VRRehydrationState::Retiring:
        return aTo == VRRehydrationState::Stable || aTo == VRRehydrationState::Offline ||
               aTo == VRRehydrationState::Bootstrap;
    case VRRehydrationState::Stable:
        return aTo == VRRehydrationState::Offline || aTo == VRRehydrationState::Connecting ||
               aTo == VRRehydrationState::Authenticated;
    case VRRehydrationState::Connecting:
        return aTo == VRRehydrationState::Offline || aTo == VRRehydrationState::Authenticated;
    case VRRehydrationState::Authenticated:
        return aTo == VRRehydrationState::Bootstrap || aTo == VRRehydrationState::Ready ||
               aTo == VRRehydrationState::Offline;
    case VRRehydrationState::Bootstrap:
        return aTo == VRRehydrationState::Assigned || aTo == VRRehydrationState::Offline;
    case VRRehydrationState::Assigned:
        return aTo == VRRehydrationState::DomainsActive || aTo == VRRehydrationState::Offline;
    case VRRehydrationState::DomainsActive:
        return aTo == VRRehydrationState::Ready || aTo == VRRehydrationState::Offline;
    case VRRehydrationState::Ready:
        return aTo == VRRehydrationState::Offline;
    case VRRehydrationState::Failed: break;
    }
    return false;
}
