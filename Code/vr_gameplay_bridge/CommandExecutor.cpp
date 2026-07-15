#include "CommandExecutor.h"

#include "AvatarManager.h"
#include "EventCapture.h"

#include <vr_common/VRCanonicalEntity.h>

#include <cmath>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
std::atomic<std::uint64_t> g_lastRetireActionId{};

[[nodiscard]] bool IsZero(const std::uint8_t* a_bytes, const std::size_t a_size) noexcept
{
    for (std::size_t index = 0; index < a_size; ++index) {
        if (a_bytes[index] != 0)
            return false;
    }
    return true;
}

[[nodiscard]] bool IsCurrentSession(const MappingHeader& a_header, const BridgeIdentity& a_identity) noexcept
{
    return a_identity.ServerInstanceNonce == a_header.ServerInstanceNonce.load(std::memory_order_acquire) &&
           a_identity.ConnectionGeneration == a_header.ConnectionGeneration.load(std::memory_order_acquire);
}

[[nodiscard]] bool AdvanceMonotonic(std::atomic<std::uint64_t>& a_last, const std::uint64_t a_value) noexcept
{
    std::uint64_t observed = a_last.load(std::memory_order_acquire);
    while (a_value > observed) {
        if (a_last.compare_exchange_weak(observed, a_value, std::memory_order_acq_rel, std::memory_order_acquire))
            return true;
    }
    return false;
}

void CountRejected(MappingHeader& a_header, const CommandStatus a_status) noexcept
{
    a_header.RejectedCommandCount.fetch_add(1, std::memory_order_relaxed);
    if (a_status == CommandStatus::StaleSession || a_status == CommandStatus::StaleEpoch || a_status == CommandStatus::StaleEntity)
        a_header.StaleCommandCount.fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] CommandStatus ValidateCommon(const MappingHeader& a_header, const CommandRecord& a_command) noexcept
{
    if (a_command.Header.PayloadSize != kFixedPayloadBytes || a_command.Header.Flags != 0 || a_command.Header.Identity.Reserved0 != 0)
        return CommandStatus::Malformed;
    if (!IsCurrentSession(a_header, a_command.Header.Identity))
        return CommandStatus::StaleSession;
    if (a_command.Header.Identity.LifecycleEpoch != a_header.LifecycleEpoch.load(std::memory_order_acquire))
        return CommandStatus::StaleEpoch;
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ValidateAvatarCommand(const MappingHeader& a_header, const CommandRecord& a_command) noexcept
{
    if (!CanonicalEntity::IsValid(
            a_command.Header.Identity.EntityId,
            a_command.Header.Identity.EntityGeneration))
        return CommandStatus::StaleEntity;

    switch (static_cast<CommandKind>(a_command.Header.Kind)) {
    case CommandKind::CreateRemoteAvatar:
        if (a_command.Header.Identity.SequenceId != 0 || a_command.Header.Identity.ActionId == 0 ||
            a_command.Payload.CreateRemoteAvatar.LocalActorBaseFormId == 0 || a_command.Payload.CreateRemoteAvatar.CreateFlags != 0 ||
            !IsZero(a_command.Payload.CreateRemoteAvatar.Reserved, sizeof(a_command.Payload.CreateRemoteAvatar.Reserved)))
            return CommandStatus::Malformed;
        break;
    case CommandKind::DestroyRemoteAvatar:
        if (a_command.Header.Identity.SequenceId != 0 || a_command.Header.Identity.ActionId == 0 ||
            a_command.Payload.DestroyRemoteAvatar.AvatarHandle.Value == 0 || a_command.Payload.DestroyRemoteAvatar.DestroyFlags != 0 ||
            !IsZero(a_command.Payload.DestroyRemoteAvatar.Reserved, sizeof(a_command.Payload.DestroyRemoteAvatar.Reserved)))
            return CommandStatus::Malformed;
        break;
    case CommandKind::UpdateRemoteRootTransform:
        if (a_command.Header.Identity.SequenceId == 0 || a_command.Header.Identity.ActionId != 0 ||
            a_command.Payload.UpdateRemoteRootTransform.AvatarHandle.Value == 0 ||
            (a_command.Payload.UpdateRemoteRootTransform.UpdateFlags & ~GameplayBridge::SpatialTransfer) != 0 ||
            a_command.Payload.UpdateRemoteRootTransform.LocalCellFormId == 0 ||
            !IsZero(a_command.Payload.UpdateRemoteRootTransform.Reserved, sizeof(a_command.Payload.UpdateRemoteRootTransform.Reserved)))
            return CommandStatus::Malformed;
        break;
    case CommandKind::ApplyRemoteAnimationGraphChunk:
    {
        const auto& payload = a_command.Payload.ApplyRemoteAnimationGraphChunk;
        const auto valueType = static_cast<AnimationGraphProtocol::ValueType>(payload.ValueType);
        if (a_command.Header.Identity.SequenceId == 0 || a_command.Header.Identity.ActionId != 0 ||
            payload.AvatarHandle.Value == 0 || payload.SnapshotId == 0 ||
            payload.DescriptorVersion != AnimationGraphProtocol::kDescriptorVersion || payload.Reserved0 != 0 ||
            payload.ChunkFlags != AnimationGraphProtocol::FullSnapshot || !std::isfinite(payload.Direction) ||
            !AnimationGraphProtocol::IsValidChunk(valueType, payload.StartIndex, payload.ValueCount, payload.TotalCount) ||
            !AnimationGraphProtocol::AreChunkValuesValid(valueType, payload.ValueCount, payload.Values))
            return CommandStatus::Malformed;
        break;
    }
    default:
        return CommandStatus::Malformed;
    }

    CapabilityMask requiredCapability{};
    switch (static_cast<CommandKind>(a_command.Header.Kind)) {
    case CommandKind::CreateRemoteAvatar:
    case CommandKind::DestroyRemoteAvatar:
        requiredCapability = static_cast<CapabilityMask>(Capability::RemoteAvatarLifecycle);
        break;
    case CommandKind::UpdateRemoteRootTransform:
        requiredCapability = static_cast<CapabilityMask>(Capability::RemoteRootTransform) |
                             static_cast<CapabilityMask>(Capability::RemoteSpatialTransfer);
        break;
    case CommandKind::ApplyRemoteAnimationGraphChunk:
        requiredCapability = static_cast<CapabilityMask>(Capability::RemoteAnimationGraphSnapshot);
        break;
    default:
        return CommandStatus::Malformed;
    }
    const auto active = a_header.ActiveCapabilities.load(std::memory_order_acquire);
    if ((active & requiredCapability) != requiredCapability)
        return CommandStatus::Unsupported;
    return CommandStatus::Success;
}

[[nodiscard]] RemoteAvatarState StateForAvatarResult(const CommandKind a_kind, const CommandStatus a_status) noexcept
{
    if (a_status != CommandStatus::Success)
        return a_status == CommandStatus::EngineRejected ? RemoteAvatarState::Faulted : RemoteAvatarState::Rejected;
    return a_kind == CommandKind::DestroyRemoteAvatar ? RemoteAvatarState::Destroyed : RemoteAvatarState::Created;
}

void PublishAvatarCommandResult(const CommandRecord& a_command, const AvatarCommandResult& a_result) noexcept
{
    PublishRemoteAvatarState(
        a_command.Header.Identity,
        a_result.AvatarHandle,
        StateForAvatarResult(static_cast<CommandKind>(a_command.Header.Kind), a_result.Status),
        a_result.Status,
        a_result.LocalCellFormId,
        a_result.LocalWorldspaceFormId,
        a_result.Root);
}

void PublishAnimationCommandResult(const CommandRecord& a_command, const AvatarCommandResult& a_result) noexcept
{
    const auto snapshotId = a_result.AnimationSnapshotId != 0 ?
                                a_result.AnimationSnapshotId :
                                a_command.Payload.ApplyRemoteAnimationGraphChunk.SnapshotId;
    const auto handle = a_result.AvatarHandle.Value != 0 ?
                            a_result.AvatarHandle :
                            a_command.Payload.ApplyRemoteAnimationGraphChunk.AvatarHandle;
    PublishRemoteAnimationGraphState(
        a_command.Header.Identity,
        handle,
        snapshotId,
        a_result.Status == CommandStatus::Success ? RemoteAnimationGraphState::Applied : RemoteAnimationGraphState::Faulted,
        a_result.Status);
}

void PublishSpatialTransferResult(const CommandRecord& a_command, const AvatarCommandResult& a_result) noexcept
{
    const auto& payload = a_command.Payload.UpdateRemoteRootTransform;
    PublishRemoteSpatialTransferState(
        a_command.Header.Identity,
        a_result.AvatarHandle.Value != 0 ? a_result.AvatarHandle : payload.AvatarHandle,
        a_result.SourceCellFormId != 0 ? a_result.SourceCellFormId : a_result.LocalCellFormId,
        a_result.SourceWorldspaceFormId != 0 ? a_result.SourceWorldspaceFormId : a_result.LocalWorldspaceFormId,
        payload.LocalCellFormId,
        payload.LocalWorldspaceFormId,
        a_result.Status);
}

[[nodiscard]] AvatarCommandResult ExecuteAvatarCommand(const MappingHeader& a_header, const CommandRecord& a_command) noexcept
{
    AvatarCommandResult result{};
    const auto common = ValidateCommon(a_header, a_command);
    if (common != CommandStatus::Success) {
        result.Status = common;
        return result;
    }
    const auto validation = ValidateAvatarCommand(a_header, a_command);
    if (validation != CommandStatus::Success) {
        result.Status = validation;
        return result;
    }

    switch (static_cast<CommandKind>(a_command.Header.Kind)) {
    case CommandKind::CreateRemoteAvatar:
        return AvatarManager::Get().CreateRemoteAvatar(a_command);
    case CommandKind::DestroyRemoteAvatar:
        return AvatarManager::Get().DestroyRemoteAvatar(a_command);
    case CommandKind::UpdateRemoteRootTransform:
        return AvatarManager::Get().UpdateRemoteRootTransform(a_command);
    case CommandKind::ApplyRemoteAnimationGraphChunk:
        return AvatarManager::Get().ApplyRemoteAnimationGraphChunk(a_command);
    default:
        result.Status = CommandStatus::Malformed;
        return result;
    }
}

[[nodiscard]] CommandStatus ExecuteCommand(BridgeEndpoint& a_endpoint, const CommandRecord& a_command) noexcept
{
    auto& header = a_endpoint.Mapping()->Header;
    switch (static_cast<CommandKind>(a_command.Header.Kind)) {
    case CommandKind::CreateRemoteAvatar:
    case CommandKind::DestroyRemoteAvatar:
    case CommandKind::UpdateRemoteRootTransform:
    {
        const auto result = ExecuteAvatarCommand(header, a_command);
        const auto kind = static_cast<CommandKind>(a_command.Header.Kind);
        if (kind == CommandKind::UpdateRemoteRootTransform &&
            (a_command.Payload.UpdateRemoteRootTransform.UpdateFlags & GameplayBridge::SpatialTransfer) != 0)
            PublishSpatialTransferResult(a_command, result);
        if (kind != CommandKind::UpdateRemoteRootTransform || result.Status != CommandStatus::Success)
            PublishAvatarCommandResult(a_command, result);
        return result.Status;
    }
    case CommandKind::ApplyRemoteAnimationGraphChunk:
    {
        const auto result = ExecuteAvatarCommand(header, a_command);
        if (result.Status != CommandStatus::Success || result.AnimationApplied)
            PublishAnimationCommandResult(a_command, result);
        return result.Status;
    }
    case CommandKind::RetireEpoch:
    {
        const auto common = ValidateCommon(header, a_command);
        if (common != CommandStatus::Success)
            return common;
        if (a_command.Header.Identity.EntityId != 0 || a_command.Header.Identity.EntityGeneration != 0 ||
            a_command.Header.Identity.SequenceId != 0 || a_command.Header.Identity.ActionId == 0 ||
            a_command.Payload.RetireEpoch.RetiredLifecycleEpoch != a_command.Header.Identity.LifecycleEpoch ||
            a_command.Payload.RetireEpoch.RetireFlags != 0 ||
            !IsZero(a_command.Payload.RetireEpoch.Reserved, sizeof(a_command.Payload.RetireEpoch.Reserved)))
            return CommandStatus::Malformed;
        if ((header.ActiveCapabilities.load(std::memory_order_acquire) & static_cast<CapabilityMask>(Capability::Lifecycle)) == 0)
            return CommandStatus::Inactive;
        if (!AdvanceMonotonic(g_lastRetireActionId, a_command.Header.Identity.ActionId))
            return CommandStatus::StaleEntity;

        AvatarManager::Get().RetireAllOnCommandPumpOwner();
        std::uint64_t expected = a_command.Header.Identity.LifecycleEpoch;
        if (!header.LifecycleEpoch.compare_exchange_strong(expected, expected + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            return CommandStatus::StaleEpoch;
        PublishEpochRetired(a_command.Payload.RetireEpoch.Reason);
        g_lastRetireActionId.store(0, std::memory_order_release);
        return CommandStatus::Success;
    }
    default:
        return CommandStatus::Malformed;
    }
}
} // namespace

CommandPumpResult ProcessCommands(
    const std::uint32_t a_callerProcessId,
    const std::uint32_t a_callerThreadId,
    const std::uint64_t a_lifecycleEpoch,
    const std::uint32_t a_maxCommands) noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    const auto precondition = endpoint.ValidateCommandPump(a_callerProcessId, a_callerThreadId, a_lifecycleEpoch);
    if (precondition != CommandPumpResult::Success)
        return precondition;

    AvatarManager::Get().BindCommandPumpOwner(a_callerThreadId);
    if (ProcessPendingLifecycleTransitions())
        g_lastRetireActionId.store(0, std::memory_order_release);

    auto* mapping = endpoint.Mapping();
    auto& commands = mapping->Commands;
    const auto limit = a_maxCommands > kDefaultCommandRingCapacity ? kDefaultCommandRingCapacity : a_maxCommands;
    for (std::uint32_t index = 0; index < limit; ++index) {
        CommandRecord command{};
        if (!TryPop(commands, command))
            break;

        const auto status = ExecuteCommand(endpoint, command);
        if (status == CommandStatus::Success)
            mapping->Header.ExecutedCommandCount.fetch_add(1, std::memory_order_relaxed);
        else
            CountRejected(mapping->Header, status);
    }

    AvatarManager::Get().ProcessPendingAnimationSnapshots();
    PublishCurrentLocalPlayerState();
    PublishCurrentLocalAnimationState();
    return CommandPumpResult::Success;
}
} // namespace SkyrimTogetherVR::GameplayAdapter
