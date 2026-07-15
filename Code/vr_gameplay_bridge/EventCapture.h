#pragma once

#include "BridgeEndpoint.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
void HandleSkseMessage(SKSE::MessagingInterface::Message* a_message) noexcept;
[[nodiscard]] bool ProcessPendingLifecycleTransitions() noexcept;
void PublishPluginLoaded() noexcept;
void PublishEpochRetired(std::uint32_t a_reason) noexcept;
void PublishCurrentLocalPlayerState() noexcept;
void PublishRemoteAvatarState(
    const BridgeIdentity& a_identity,
    AdapterHandle a_avatarHandle,
    RemoteAvatarState a_state,
    CommandStatus a_status,
    std::uint32_t a_localCellFormId,
    std::uint32_t a_localWorldspaceFormId,
    const RootTransform& a_root) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter
