#pragma once

#include "BridgeEndpoint.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
void HandleSkseMessage(SKSE::MessagingInterface::Message* a_message) noexcept;
[[nodiscard]] bool ProcessPendingLifecycleTransitions() noexcept;
void PublishPluginLoaded() noexcept;
void PublishEpochRetired(std::uint32_t a_reason) noexcept;
void PublishCurrentLocalPlayerState() noexcept;
void PublishCurrentLocalAnimationState() noexcept;
void PublishRemoteAvatarState(
    const BridgeIdentity& a_identity,
    AdapterHandle a_avatarHandle,
    RemoteAvatarState a_state,
    CommandStatus a_status,
    std::uint32_t a_localCellFormId,
    std::uint32_t a_localWorldspaceFormId,
    const RootTransform& a_root) noexcept;
void PublishRemoteAnimationGraphState(
    const BridgeIdentity& a_identity,
    AdapterHandle a_avatarHandle,
    std::uint64_t a_snapshotId,
    RemoteAnimationGraphState a_state,
    CommandStatus a_status) noexcept;
void PublishRemoteSpatialTransferState(
    const BridgeIdentity& a_identity,
    AdapterHandle a_avatarHandle,
    std::uint32_t a_sourceCellFormId,
    std::uint32_t a_sourceWorldspaceFormId,
    std::uint32_t a_targetCellFormId,
    std::uint32_t a_targetWorldspaceFormId,
    CommandStatus a_status) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter
