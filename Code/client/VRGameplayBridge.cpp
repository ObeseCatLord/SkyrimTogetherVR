#include <TiltedOnlinePCH.h>

#include "VRGameplayBridge.h"

#include <cmath>
#include <new>
#include <vector>

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC
#define TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC 0
#endif

namespace SkyrimTogetherVR::GameplayBridgeClient
{
using namespace GameplayBridge;

namespace
{
HANDLE s_mappingHandle = nullptr;
GameplayBridgeMapping* s_mapping = nullptr;
std::atomic<std::uint32_t> s_ownerThreadId{0};
std::atomic<std::uint64_t> s_lastBridgeSequence{0};
std::atomic<std::uint64_t> s_lastBridgeAction{0};
std::atomic<std::uint64_t> s_discardedEventCount{0};
std::atomic<std::uint64_t> s_rejectedSubmissionCount{0};

constexpr CapabilityMask kRequestedCapabilities =
    static_cast<CapabilityMask>(Capability::Lifecycle)
#if TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC
    | kInitialCapabilities
#endif
    ;

[[nodiscard]] bool IsZero(const std::uint8_t* apBytes, const std::size_t aSize) noexcept
{
    for (std::size_t i = 0; i < aSize; ++i)
    {
        if (apBytes[i] != 0)
            return false;
    }
    return true;
}

[[nodiscard]] bool IsFinite(const RootTransform& acTransform) noexcept
{
    return std::isfinite(acTransform.PositionX) && std::isfinite(acTransform.PositionY) &&
           std::isfinite(acTransform.PositionZ) && std::isfinite(acTransform.RotationX) &&
           std::isfinite(acTransform.RotationY) && std::isfinite(acTransform.RotationZ) &&
           std::isfinite(acTransform.RotationW) && std::isfinite(acTransform.Scale);
}

[[nodiscard]] bool IsExecutableProtection(const DWORD aProtection) noexcept
{
    if ((aProtection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    const auto protection = aProtection & 0xffu;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool IsLocalMappingValid() noexcept
{
    if (!s_mapping)
        return false;

    const auto& header = s_mapping->Header;
    return header.Magic == kMappingMagic && header.AbiVersion == kMappingAbiVersion &&
           header.HeaderSize == sizeof(MappingHeader) && header.MappingSize == sizeof(GameplayBridgeMapping) &&
           header.PublisherProcessId == GetCurrentProcessId() && header.RuntimeVersion == kSkyrimVrRuntimeVersion &&
           header.CapabilityRevision == kCapabilityRevision && header.Reserved0 == 0 &&
           header.RequestedCapabilities.load(std::memory_order_acquire) == kRequestedCapabilities;
}

[[nodiscard]] EndpointState ReadState() noexcept
{
    if (!IsLocalMappingValid())
        return EndpointState::Faulted;
    return static_cast<EndpointState>(s_mapping->Header.State.load(std::memory_order_acquire));
}

[[nodiscard]] bool IsOwnerThread() noexcept
{
    const auto owner = s_ownerThreadId.load(std::memory_order_acquire);
    return owner != 0 && owner == GetCurrentThreadId() && IsLocalMappingValid() &&
           s_mapping->Header.EventConsumerThreadId.load(std::memory_order_acquire) == owner;
}

[[nodiscard]] bool IsOperational() noexcept
{
    return IsOwnerThread() && ReadState() == EndpointState::Ready;
}

[[nodiscard]] bool IdentityMatchesCurrent(const BridgeIdentity& acIdentity) noexcept
{
    const auto& header = s_mapping->Header;
    const auto nonce = header.ServerInstanceNonce.load(std::memory_order_acquire);
    const auto generation = header.ConnectionGeneration.load(std::memory_order_acquire);
    const auto epoch = header.LifecycleEpoch.load(std::memory_order_acquire);
    return acIdentity.Reserved0 == 0 && acIdentity.ServerInstanceNonce == nonce &&
           acIdentity.ConnectionGeneration == generation && acIdentity.LifecycleEpoch == epoch;
}

[[nodiscard]] bool IdentityIsCurrentOrUnspecified(const BridgeIdentity& acIdentity) noexcept
{
    const auto& header = s_mapping->Header;
    const auto nonce = header.ServerInstanceNonce.load(std::memory_order_acquire);
    const auto generation = header.ConnectionGeneration.load(std::memory_order_acquire);
    const auto epoch = header.LifecycleEpoch.load(std::memory_order_acquire);
    return acIdentity.Reserved0 == 0 &&
           (acIdentity.ServerInstanceNonce == 0 || acIdentity.ServerInstanceNonce == nonce) &&
           (acIdentity.ConnectionGeneration == 0 || acIdentity.ConnectionGeneration == generation) &&
           (acIdentity.LifecycleEpoch == 0 || acIdentity.LifecycleEpoch == epoch);
}

[[nodiscard]] bool HasActiveCapabilities(const CapabilityMask aRequired) noexcept
{
    const auto requested = s_mapping->Header.RequestedCapabilities.load(std::memory_order_acquire);
    const auto available = s_mapping->Header.AvailableCapabilities.load(std::memory_order_acquire);
    const auto active = s_mapping->Header.ActiveCapabilities.load(std::memory_order_acquire);
    return (active & ~available) == 0 && (active & ~requested) == 0 && (active & aRequired) == aRequired;
}

[[nodiscard]] bool IsKnownLifecycleState(const std::uint32_t aValue) noexcept
{
    return aValue >= static_cast<std::uint32_t>(LifecycleState::PluginLoaded) &&
           aValue <= static_cast<std::uint32_t>(LifecycleState::EpochRetired);
}

[[nodiscard]] bool IsKnownRemoteAvatarState(const std::uint32_t aValue) noexcept
{
    return aValue >= static_cast<std::uint32_t>(RemoteAvatarState::Created) &&
           aValue <= static_cast<std::uint32_t>(RemoteAvatarState::Faulted);
}

[[nodiscard]] bool IsKnownRemoteAnimationGraphState(const std::uint32_t aValue) noexcept
{
    return aValue >= static_cast<std::uint32_t>(RemoteAnimationGraphState::Applied) &&
           aValue <= static_cast<std::uint32_t>(RemoteAnimationGraphState::Faulted);
}

[[nodiscard]] bool IsKnownCommandStatus(const std::uint32_t aValue) noexcept
{
    return aValue <= static_cast<std::uint32_t>(CommandStatus::QueueOverflow);
}

[[nodiscard]] bool IsValidProjectilePayload(const ApplyProjectileLaunchPayload& acPayload) noexcept
{
    const auto bounded = [](const float aValue, const float aLimit) noexcept {
        return std::isfinite(aValue) && aValue >= -aLimit && aValue <= aLimit;
    };
    return acPayload.LocalProjectileBaseFormId != 0 &&
           acPayload.LocalParentCellFormId != 0 &&
           bounded(acPayload.OriginX, kMaximumProjectileCoordinate) &&
           bounded(acPayload.OriginY, kMaximumProjectileCoordinate) &&
           bounded(acPayload.OriginZ, kMaximumProjectileCoordinate) &&
           bounded(acPayload.AngleX, kMaximumProjectileAngle) &&
           bounded(acPayload.AngleZ, kMaximumProjectileAngle) &&
           std::isfinite(acPayload.Power) && acPayload.Power >= 0.0F &&
           acPayload.Power <= kMaximumProjectilePower && std::isfinite(acPayload.Scale) &&
           acPayload.Scale >= 0.0F && acPayload.Scale <= kMaximumProjectileScale &&
           acPayload.CastingSource >= 0 && acPayload.CastingSource <= 3 && acPayload.Area >= 0 &&
           acPayload.Area <= kMaximumProjectileArea &&
           (acPayload.LaunchFlags & ~kProjectileLaunchKnownFlags) == 0 &&
           IsZero(acPayload.ReservedTail, sizeof(acPayload.ReservedTail));
}

[[nodiscard]] bool ValidateEvent(const EventRecord& acEvent) noexcept
{
    const auto& header = acEvent.Header;
    if (header.PayloadSize != kFixedPayloadBytes || header.Flags != 0 || !IdentityMatchesCurrent(header.Identity))
        return false;

    switch (static_cast<EventKind>(header.Kind))
    {
    case EventKind::Lifecycle:
    {
        const auto& payload = acEvent.Payload.Lifecycle;
        return header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 && header.Identity.SequenceId != 0 &&
               header.Identity.ActionId == 0 && IsKnownLifecycleState(payload.ObservedState) &&
               payload.ObservedLifecycleEpoch == header.Identity.LifecycleEpoch && IsZero(payload.Reserved, sizeof(payload.Reserved)) &&
               HasActiveCapabilities(static_cast<CapabilityMask>(Capability::Lifecycle));
    }
    case EventKind::LocalPlayerState:
    {
        const auto& payload = acEvent.Payload.LocalPlayerState;
        const auto required = static_cast<CapabilityMask>(Capability::LocalPlayerDiscovery) |
                              static_cast<CapabilityMask>(Capability::LocalPlayerSnapshot);
        return header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 && header.Identity.ActionId == 0 &&
               payload.LocalPlayerHandle.Value == kLocalPlayerHandle.Value && payload.LocalActorBaseFormId != 0 &&
               IsFinite(payload.Root) && IsZero(payload.Reserved, sizeof(payload.Reserved)) &&
               HasActiveCapabilities(required);
    }
    case EventKind::RemoteAvatarState:
    {
        const auto& payload = acEvent.Payload.RemoteAvatarState;
        const auto required = static_cast<CapabilityMask>(Capability::RemoteAvatarLifecycle) |
                              static_cast<CapabilityMask>(Capability::RemoteRootTransform);
        return header.Identity.EntityId != 0 && header.Identity.EntityGeneration != 0 && IsKnownRemoteAvatarState(payload.State) &&
               IsKnownCommandStatus(payload.Status) && IsFinite(payload.Root) &&
               (static_cast<RemoteAvatarState>(payload.State) != RemoteAvatarState::Created ||
                payload.LocalActorReferenceFormId != 0) &&
               IsZero(payload.Reserved, sizeof(payload.Reserved)) &&
               HasActiveCapabilities(required);
    }
    case EventKind::LocalAnimationGraphChunk:
    {
        const auto& payload = acEvent.Payload.LocalAnimationGraphChunk;
        const auto valueType = static_cast<AnimationGraphProtocol::ValueType>(payload.ValueType);
        return header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 && header.Identity.SequenceId != 0 &&
               header.Identity.ActionId == 0 && payload.AvatarHandle.Value == kLocalPlayerHandle.Value && payload.SnapshotId != 0 &&
               payload.DescriptorVersion == AnimationGraphProtocol::kDescriptorVersion && payload.Reserved0 == 0 &&
               payload.ChunkFlags == AnimationGraphProtocol::FullSnapshot && std::isfinite(payload.Direction) &&
               IsZero(payload.ReservedTail, sizeof(payload.ReservedTail)) &&
               AnimationGraphProtocol::IsValidChunk(valueType, payload.StartIndex, payload.ValueCount, payload.TotalCount) &&
               AnimationGraphProtocol::AreChunkValuesValid(valueType, payload.ValueCount, payload.Values) &&
               HasActiveCapabilities(static_cast<CapabilityMask>(Capability::LocalAnimationGraphSnapshot));
    }
    case EventKind::RemoteAnimationGraphState:
    {
        const auto& payload = acEvent.Payload.RemoteAnimationGraphState;
        return header.Identity.EntityId != 0 && header.Identity.EntityGeneration != 0 &&
               header.Identity.SequenceId != 0 && header.Identity.ActionId == 0 && payload.AvatarHandle.Value != 0 &&
               payload.SnapshotId != 0 && IsKnownRemoteAnimationGraphState(payload.State) &&
               IsKnownCommandStatus(payload.Status) && IsZero(payload.Reserved, sizeof(payload.Reserved)) &&
               HasActiveCapabilities(static_cast<CapabilityMask>(Capability::RemoteAnimationGraphSnapshot));
    }
    case EventKind::RemoteSpatialTransferState:
    {
        const auto& payload = acEvent.Payload.RemoteSpatialTransferState;
        return header.Identity.EntityId != 0 && header.Identity.EntityGeneration != 0 &&
               header.Identity.SequenceId != 0 && header.Identity.ActionId == 0 && payload.AvatarHandle.Value != 0 &&
               payload.SourceCellFormId != 0 && payload.TargetCellFormId != 0 && IsKnownCommandStatus(payload.Status) &&
               IsZero(payload.Reserved, sizeof(payload.Reserved)) &&
               HasActiveCapabilities(static_cast<CapabilityMask>(Capability::RemoteSpatialTransfer));
    }
    case EventKind::LocalGameplayAction:
    {
        const auto& payload = acEvent.Payload.LocalGameplayAction;
        const auto domain = static_cast<GameplayDomain>(payload.Domain);
        const auto action = static_cast<GameplayAction>(payload.Action);
        const auto capability = CapabilityForDomain(domain);
        const bool objectSnapshot = domain == GameplayDomain::Object &&
            action >= GameplayAction::ObjectSnapshotBegin && action <= GameplayAction::ObjectSnapshotEnd;
        return header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 &&
               header.Identity.SequenceId == 0 && header.Identity.ActionId != 0 &&
               ((objectSnapshot && payload.TargetHandle.Value == 0 && payload.TargetLocalFormId != 0) ||
                (!objectSnapshot && payload.TargetHandle.Value == kLocalPlayerHandle.Value)) &&
               IsActionInDomain(domain, action) &&
               capability != static_cast<Capability>(0) && std::isfinite(payload.ScalarA) &&
               std::isfinite(payload.ScalarB) && std::isfinite(payload.ScalarC) &&
               std::isfinite(payload.ScalarD) && payload.Reserved0 == 0 &&
               IsZero(payload.ReservedTail, sizeof(payload.ReservedTail)) &&
               HasActiveCapabilities(static_cast<CapabilityMask>(capability));
    }
    case EventKind::RemoteGameplayActionState:
    {
        const auto& payload = acEvent.Payload.RemoteGameplayActionState;
        const auto domain = static_cast<GameplayDomain>(payload.Domain);
        const auto action = static_cast<GameplayAction>(payload.Action);
        const auto capability = CapabilityForDomain(domain);
        return header.Identity.SequenceId == 0 && header.Identity.ActionId != 0 &&
               IsActionInDomain(domain, action) && IsKnownCommandStatus(payload.Status) &&
               IsZero(payload.Reserved, sizeof(payload.Reserved)) &&
               capability != static_cast<Capability>(0) &&
               HasActiveCapabilities(static_cast<CapabilityMask>(capability));
    }
    case EventKind::LocalGameplayTextChunk:
    {
        const auto& payload = acEvent.Payload.LocalGameplayTextChunk;
        const auto domain = static_cast<GameplayDomain>(payload.Domain);
        const auto action = static_cast<GameplayAction>(payload.Action);
        const auto capability = CapabilityForDomain(domain);
        return header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 &&
               header.Identity.SequenceId == 0 && header.Identity.ActionId != 0 &&
               payload.TargetHandle.Value == kLocalPlayerHandle.Value && payload.TextId != 0 &&
               IsActionInDomain(domain, action) && capability != static_cast<Capability>(0) &&
               payload.ChunkCount != 0 && payload.ChunkCount <= kMaximumGameplayTextChunks &&
               payload.ChunkIndex < payload.ChunkCount && payload.ByteCount <= kGameplayTextBytesPerChunk &&
               payload.Reserved0 == 0 && payload.AuxiliaryLocalFormId == 0 &&
               IsZero(reinterpret_cast<const std::uint8_t*>(payload.Utf8Bytes + payload.ByteCount),
                      kGameplayTextBytesPerChunk - payload.ByteCount) &&
               HasActiveCapabilities(static_cast<CapabilityMask>(capability));
    }
    case EventKind::LocalProjectileLaunch:
    {
        const auto& payload = acEvent.Payload.LocalProjectileLaunch;
        const bool localPlayer = payload.TargetHandle.Value == kLocalPlayerHandle.Value &&
                                 payload.LocalShooterFormId == 0;
        const bool localNpc = payload.TargetHandle.Value == 0 && payload.LocalShooterFormId != 0;
        return header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 &&
               header.Identity.SequenceId != 0 && header.Identity.ActionId == 0 &&
               (localPlayer || localNpc) && IsValidProjectilePayload(payload) &&
               HasActiveCapabilities(static_cast<CapabilityMask>(Capability::CombatAndMagic));
    }
    case EventKind::LocalActorActionMetadata:
    {
        const auto& payload = acEvent.Payload.LocalActorActionMetadata;
        const bool localActor = payload.TargetHandle.Value == kLocalPlayerHandle.Value ||
                                payload.TargetHandle.Value == 0;
        return header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 &&
               header.Identity.SequenceId == 0 && header.Identity.ActionId != 0 && localActor &&
               payload.ActorLocalFormId != 0 && payload.ActionLocalFormId != 0 &&
               payload.SnapshotId == header.Identity.ActionId && payload.TextId == header.Identity.ActionId &&
               (payload.Type & ~0x7u) == 0 && payload.ActionFlags == 0 &&
               IsZero(payload.Reserved, sizeof(payload.Reserved)) &&
               HasActiveCapabilities(static_cast<CapabilityMask>(Capability::ExactAnimationActions));
    }
    case EventKind::LocalActorActionGraphChunk:
    {
        const auto& payload = acEvent.Payload.LocalActorActionGraphChunk;
        const auto valueType = static_cast<AnimationGraphProtocol::ValueType>(payload.ValueType);
        return header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 &&
               header.Identity.SequenceId == 0 && header.Identity.ActionId != 0 &&
               (payload.TargetHandle.Value == kLocalPlayerHandle.Value || payload.TargetHandle.Value == 0) &&
               payload.ActorLocalFormId != 0 && payload.SnapshotId == header.Identity.ActionId &&
               payload.DescriptorVersion == AnimationGraphProtocol::kDescriptorVersion &&
               payload.Reserved0 == 0 && payload.Reserved1 == 0 &&
               payload.ChunkFlags == AnimationGraphProtocol::FullSnapshot && std::isfinite(payload.Direction) &&
               IsZero(payload.ReservedTail, sizeof(payload.ReservedTail)) &&
               AnimationGraphProtocol::IsValidChunk(valueType, payload.StartIndex, payload.ValueCount, payload.TotalCount) &&
               AnimationGraphProtocol::AreChunkValuesValid(valueType, payload.ValueCount, payload.Values) &&
               HasActiveCapabilities(static_cast<CapabilityMask>(Capability::ExactAnimationActions));
    }
    case EventKind::LocalActorActionTextChunk:
    {
        const auto& payload = acEvent.Payload.LocalActorActionTextChunk;
        return header.Identity.EntityId == 0 && header.Identity.EntityGeneration == 0 &&
               header.Identity.SequenceId == 0 && header.Identity.ActionId != 0 &&
               (payload.TargetHandle.Value == kLocalPlayerHandle.Value || payload.TargetHandle.Value == 0) &&
               payload.TargetLocalFormId != 0 &&
               payload.Domain == static_cast<std::uint16_t>(GameplayDomain::Animation) &&
               payload.Action == static_cast<std::uint16_t>(GameplayAction::ActorAction) &&
               payload.TextId == header.Identity.ActionId && payload.ChunkCount != 0 &&
               payload.ChunkCount <= kMaximumGameplayTextChunks && payload.ChunkIndex < payload.ChunkCount &&
               payload.ByteCount <= kGameplayTextBytesPerChunk && payload.Reserved0 == 0 &&
               payload.AuxiliaryLocalFormId == 0 &&
               IsZero(reinterpret_cast<const std::uint8_t*>(payload.Utf8Bytes + payload.ByteCount),
                      kGameplayTextBytesPerChunk - payload.ByteCount) &&
               HasActiveCapabilities(static_cast<CapabilityMask>(Capability::ExactAnimationActions));
    }
    default:
        return false;
    }
}

[[nodiscard]] CapabilityMask RequiredCapability(const CommandKind aKind) noexcept
{
    switch (aKind)
    {
    case CommandKind::CreateRemoteAvatar:
    case CommandKind::DestroyRemoteAvatar:
        return static_cast<CapabilityMask>(Capability::RemoteAvatarLifecycle);
    case CommandKind::UpdateRemoteRootTransform:
        return static_cast<CapabilityMask>(Capability::RemoteRootTransform) |
               static_cast<CapabilityMask>(Capability::RemoteSpatialTransfer);
    case CommandKind::ApplyRemoteAnimationGraphChunk:
        return static_cast<CapabilityMask>(Capability::RemoteAnimationGraphSnapshot);
    case CommandKind::RetireEpoch:
        return static_cast<CapabilityMask>(Capability::Lifecycle);
    case CommandKind::ApplyGameplayAction:
        return 0;
    case CommandKind::ApplyGameplayTextChunk:
        return 0;
    case CommandKind::ApplyProjectileLaunch:
        return static_cast<CapabilityMask>(Capability::CombatAndMagic);
    case CommandKind::StageActorActionGraphChunk:
    case CommandKind::StageActorActionTextChunk:
    case CommandKind::ApplyActorAction:
        return static_cast<CapabilityMask>(Capability::ExactAnimationActions);
    default:
        return 0;
    }
}

[[nodiscard]] bool ValidateCommandPayload(const CommandRecord& acCommand) noexcept
{
    const auto kind = static_cast<CommandKind>(acCommand.Header.Kind);
    const auto& identity = acCommand.Header.Identity;
    switch (kind)
    {
    case CommandKind::CreateRemoteAvatar:
    {
        const auto& payload = acCommand.Payload.CreateRemoteAvatar;
        const auto knownFlags = GameplayBridge::UseExistingReference | GameplayBridge::PlayerAvatar;
        return identity.EntityId != 0 && identity.EntityGeneration != 0 && identity.SequenceId == 0 &&
               payload.LocalActorBaseFormId != 0 && payload.LocalCellFormId != 0 && IsFinite(payload.InitialRoot) &&
               (payload.CreateFlags & ~knownFlags) == 0 &&
               (((payload.CreateFlags & GameplayBridge::UseExistingReference) != 0) ==
                (payload.LocalReferenceFormId != 0)) &&
               IsZero(payload.Reserved, sizeof(payload.Reserved));
    }
    case CommandKind::DestroyRemoteAvatar:
    {
        const auto& payload = acCommand.Payload.DestroyRemoteAvatar;
        return identity.EntityId != 0 && identity.EntityGeneration != 0 && identity.SequenceId == 0 &&
               payload.AvatarHandle.Value != 0 && IsZero(payload.Reserved, sizeof(payload.Reserved));
    }
    case CommandKind::UpdateRemoteRootTransform:
    {
        const auto& payload = acCommand.Payload.UpdateRemoteRootTransform;
        return identity.EntityId != 0 && identity.EntityGeneration != 0 && identity.ActionId == 0 &&
               payload.AvatarHandle.Value != 0 && payload.LocalCellFormId != 0 &&
               (payload.UpdateFlags & ~GameplayBridge::SpatialTransfer) == 0 && IsFinite(payload.Root) &&
               IsZero(payload.Reserved, sizeof(payload.Reserved));
    }
    case CommandKind::ApplyRemoteAnimationGraphChunk:
    {
        const auto& payload = acCommand.Payload.ApplyRemoteAnimationGraphChunk;
        const auto valueType = static_cast<AnimationGraphProtocol::ValueType>(payload.ValueType);
        return identity.EntityId != 0 && identity.EntityGeneration != 0 && identity.ActionId == 0 &&
               payload.AvatarHandle.Value != 0 && payload.SnapshotId != 0 &&
               payload.DescriptorVersion == AnimationGraphProtocol::kDescriptorVersion && payload.Reserved0 == 0 &&
               payload.ChunkFlags == AnimationGraphProtocol::FullSnapshot && std::isfinite(payload.Direction) &&
               IsZero(payload.ReservedTail, sizeof(payload.ReservedTail)) &&
               AnimationGraphProtocol::IsValidChunk(valueType, payload.StartIndex, payload.ValueCount, payload.TotalCount) &&
               AnimationGraphProtocol::AreChunkValuesValid(valueType, payload.ValueCount, payload.Values);
    }
    case CommandKind::ApplyGameplayAction:
    {
        const auto& payload = acCommand.Payload.ApplyGameplayAction;
        const auto domain = static_cast<GameplayDomain>(payload.Domain);
        const auto action = static_cast<GameplayAction>(payload.Action);
        const bool actorTarget = payload.TargetHandle.Value != 0;
        if (action == GameplayAction::ArmLocalCapture &&
            (domain != GameplayDomain::ActorState ||
             payload.TargetHandle.Value != kLocalPlayerHandle.Value || payload.SecondaryHandle.Value != 0 ||
             payload.TargetLocalFormId != 0 || payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 ||
             payload.LocalFormIdC != 0 || payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
             payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
             payload.ScalarD != 0.0F || payload.ActionFlags != 0))
            return false;
        return identity.SequenceId == 0 &&
               ((actorTarget && identity.EntityId != 0 && identity.EntityGeneration != 0) ||
                (!actorTarget && identity.EntityId == 0 && identity.EntityGeneration == 0)) &&
               IsActionInDomain(domain, action) && CapabilityForDomain(domain) != static_cast<Capability>(0) &&
               std::isfinite(payload.ScalarA) && std::isfinite(payload.ScalarB) &&
               std::isfinite(payload.ScalarC) && std::isfinite(payload.ScalarD) && payload.Reserved0 == 0 &&
               IsZero(payload.ReservedTail, sizeof(payload.ReservedTail));
    }
    case CommandKind::ApplyGameplayTextChunk:
    {
        const auto& payload = acCommand.Payload.ApplyGameplayTextChunk;
        const auto domain = static_cast<GameplayDomain>(payload.Domain);
        const auto action = static_cast<GameplayAction>(payload.Action);
        const bool actorTarget = payload.TargetHandle.Value != 0;
        const bool textAction = action == GameplayAction::AnimationEvent || action == GameplayAction::SetName ||
                                action == GameplayAction::ScriptAnimation || action == GameplayAction::Dialogue ||
                                action == GameplayAction::Subtitle;
        return identity.SequenceId == 0 && payload.TextId != 0 && textAction &&
               ((actorTarget && identity.EntityId != 0 && identity.EntityGeneration != 0) ||
                (!actorTarget && identity.EntityId == 0 && identity.EntityGeneration == 0)) &&
               IsActionInDomain(domain, action) && CapabilityForDomain(domain) != static_cast<Capability>(0) &&
               payload.ChunkCount != 0 && payload.ChunkCount <= kMaximumGameplayTextChunks &&
               payload.ChunkIndex < payload.ChunkCount && payload.ByteCount <= kGameplayTextBytesPerChunk &&
               payload.Reserved0 == 0 &&
               (action == GameplayAction::Subtitle || payload.AuxiliaryLocalFormId == 0) &&
               IsZero(reinterpret_cast<const std::uint8_t*>(payload.Utf8Bytes + payload.ByteCount),
                      kGameplayTextBytesPerChunk - payload.ByteCount);
    }
    case CommandKind::ApplyProjectileLaunch:
    {
        const auto& payload = acCommand.Payload.ApplyProjectileLaunch;
        return identity.EntityId != 0 && identity.EntityGeneration != 0 && identity.SequenceId == 0 &&
               payload.LocalShooterFormId == 0 && IsValidProjectilePayload(payload);
    }
    case CommandKind::StageActorActionGraphChunk:
    {
        const auto& payload = acCommand.Payload.StageActorActionGraphChunk;
        const auto valueType = static_cast<AnimationGraphProtocol::ValueType>(payload.ValueType);
        return identity.EntityId != 0 && identity.EntityGeneration != 0 && identity.SequenceId == 0 &&
               payload.TargetHandle.Value != 0 && payload.ActorLocalFormId == 0 && payload.SnapshotId != 0 &&
               payload.DescriptorVersion == AnimationGraphProtocol::kDescriptorVersion &&
               payload.Reserved0 == 0 && payload.Reserved1 == 0 &&
               payload.ChunkFlags == AnimationGraphProtocol::FullSnapshot && std::isfinite(payload.Direction) &&
               IsZero(payload.ReservedTail, sizeof(payload.ReservedTail)) &&
               AnimationGraphProtocol::IsValidChunk(valueType, payload.StartIndex, payload.ValueCount, payload.TotalCount) &&
               AnimationGraphProtocol::AreChunkValuesValid(valueType, payload.ValueCount, payload.Values);
    }
    case CommandKind::StageActorActionTextChunk:
    {
        const auto& payload = acCommand.Payload.StageActorActionTextChunk;
        return identity.EntityId != 0 && identity.EntityGeneration != 0 && identity.SequenceId == 0 &&
               payload.TargetHandle.Value != 0 && payload.TargetLocalFormId == 0 && payload.TextId != 0 &&
               payload.Domain == static_cast<std::uint16_t>(GameplayDomain::Animation) &&
               payload.Action == static_cast<std::uint16_t>(GameplayAction::ActorAction) &&
               payload.ChunkCount != 0 && payload.ChunkCount <= kMaximumGameplayTextChunks &&
               payload.ChunkIndex < payload.ChunkCount && payload.ByteCount <= kGameplayTextBytesPerChunk &&
               payload.Reserved0 == 0 && payload.AuxiliaryLocalFormId == 0 &&
               IsZero(reinterpret_cast<const std::uint8_t*>(payload.Utf8Bytes + payload.ByteCount),
                      kGameplayTextBytesPerChunk - payload.ByteCount);
    }
    case CommandKind::ApplyActorAction:
    {
        const auto& payload = acCommand.Payload.ApplyActorAction;
        return identity.EntityId != 0 && identity.EntityGeneration != 0 && identity.SequenceId == 0 &&
               payload.TargetHandle.Value != 0 && payload.ActorLocalFormId == 0 &&
               payload.ActionLocalFormId != 0 && payload.SnapshotId != 0 && payload.TextId == payload.SnapshotId &&
               (payload.Type & ~0x7u) == 0 && payload.ActionFlags == 0 &&
               IsZero(payload.Reserved, sizeof(payload.Reserved));
    }
    case CommandKind::RetireEpoch:
    {
        const auto& payload = acCommand.Payload.RetireEpoch;
        return identity.EntityId == 0 && identity.EntityGeneration == 0 && identity.SequenceId == 0 &&
               payload.RetiredLifecycleEpoch == s_mapping->Header.LifecycleEpoch.load(std::memory_order_acquire) &&
               IsZero(payload.Reserved, sizeof(payload.Reserved));
    }
    default:
        return false;
    }
}

void RejectSubmission() noexcept
{
    s_rejectedSubmissionCount.fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] std::uint64_t ClaimNextCounter(std::atomic<std::uint64_t>& arCounter) noexcept
{
    auto value = arCounter.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (value == 0)
        value = arCounter.fetch_add(1, std::memory_order_acq_rel) + 1;
    return value;
}

[[nodiscard]] bool IsBatchCommand(const CommandKind aKind) noexcept
{
    return aKind == CommandKind::ApplyGameplayAction ||
           aKind == CommandKind::ApplyGameplayTextChunk ||
           aKind == CommandKind::StageActorActionGraphChunk ||
           aKind == CommandKind::StageActorActionTextChunk ||
           aKind == CommandKind::ApplyActorAction;
}

void NormalizeBatchTransactionId(CommandRecord& arCommand, const std::uint64_t aActionId) noexcept
{
    arCommand.Header.Identity.ActionId = aActionId;
    switch (static_cast<CommandKind>(arCommand.Header.Kind))
    {
    case CommandKind::StageActorActionGraphChunk:
        arCommand.Payload.StageActorActionGraphChunk.SnapshotId = aActionId;
        break;
    case CommandKind::StageActorActionTextChunk:
        arCommand.Payload.StageActorActionTextChunk.TextId = aActionId;
        break;
    case CommandKind::ApplyActorAction:
        arCommand.Payload.ApplyActorAction.SnapshotId = aActionId;
        arCommand.Payload.ApplyActorAction.TextId = aActionId;
        break;
    default:
        break;
    }
}

[[nodiscard]] CommandPumpResult MapPumpResult(const std::uint32_t aResult) noexcept
{
    if (aResult <= static_cast<std::uint32_t>(CommandPumpResult::Faulted))
        return static_cast<CommandPumpResult>(aResult);
    return CommandPumpResult::Faulted;
}
} // namespace

bool Initialize() noexcept
{
    if (s_mapping)
        return IsLocalMappingValid() && ReadState() != EndpointState::Retired;

    HANDLE mappingHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(GameplayBridgeMapping), nullptr);
    if (!mappingHandle)
        return false;

    auto* mapping = static_cast<GameplayBridgeMapping*>(
        MapViewOfFile(mappingHandle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(GameplayBridgeMapping)));
    if (!mapping)
    {
        CloseHandle(mappingHandle);
        return false;
    }

    try
    {
        ::new (static_cast<void*>(mapping)) GameplayBridgeMapping;
        auto& header = mapping->Header;
        header.Magic = kMappingMagic;
        header.AbiVersion = kMappingAbiVersion;
        header.HeaderSize = static_cast<std::uint16_t>(sizeof(MappingHeader));
        header.MappingSize = sizeof(GameplayBridgeMapping);
        header.PublisherProcessId = GetCurrentProcessId();
        header.RuntimeVersion = kSkyrimVrRuntimeVersion;
        header.CapabilityRevision = kCapabilityRevision;
        header.Reserved0 = 0;
        header.RequestedCapabilities.store(kRequestedCapabilities, std::memory_order_relaxed);
        header.AvailableCapabilities.store(0, std::memory_order_relaxed);
        header.ActiveCapabilities.store(0, std::memory_order_relaxed);
        header.ServerInstanceNonce.store(0, std::memory_order_relaxed);
        header.ConnectionGeneration.store(0, std::memory_order_relaxed);
        header.LifecycleEpoch.store(1, std::memory_order_relaxed);
        header.EventConsumerThreadId.store(0, std::memory_order_relaxed);
        header.CommandExecutionThreadId.store(0, std::memory_order_relaxed);
        header.ProducedEventCount.store(0, std::memory_order_relaxed);
        header.ConsumedEventCount.store(0, std::memory_order_relaxed);
        header.SubmittedCommandCount.store(0, std::memory_order_relaxed);
        header.ExecutedCommandCount.store(0, std::memory_order_relaxed);
        header.RejectedCommandCount.store(0, std::memory_order_relaxed);
        header.StaleCommandCount.store(0, std::memory_order_relaxed);
        InitializeRing(mapping->Events);
        InitializeRing(mapping->Commands);
        header.State.store(static_cast<std::uint32_t>(EndpointState::Prepared), std::memory_order_release);

        wchar_t handleText[2 + sizeof(std::uintptr_t) * 2 + 1]{};
        const auto length = _snwprintf_s(handleText, _countof(handleText), _TRUNCATE, L"0x%llX",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(mappingHandle)));
        if (length < 0 || !SetEnvironmentVariableW(kMappingHandleEnvironment, handleText))
        {
            header.State.store(static_cast<std::uint32_t>(EndpointState::Faulted), std::memory_order_release);
            UnmapViewOfFile(mapping);
            CloseHandle(mappingHandle);
            return false;
        }
    }
    catch (...)
    {
        UnmapViewOfFile(mapping);
        CloseHandle(mappingHandle);
        return false;
    }

    s_mappingHandle = mappingHandle;
    s_mapping = mapping;
    s_ownerThreadId.store(0, std::memory_order_release);
    s_lastBridgeSequence.store(0, std::memory_order_release);
    s_lastBridgeAction.store(0, std::memory_order_release);
    s_discardedEventCount.store(0, std::memory_order_release);
    s_rejectedSubmissionCount.store(0, std::memory_order_release);
    return true;
}

bool Activate(const std::uint32_t aOwnerThreadId) noexcept
{
    if (aOwnerThreadId == 0 || aOwnerThreadId != GetCurrentThreadId() || !IsLocalMappingValid())
        return false;

    const auto state = ReadState();
    if (state != EndpointState::Prepared && state != EndpointState::Ready)
        return false;

    std::uint32_t expected = 0;
    if (!s_ownerThreadId.compare_exchange_strong(expected, aOwnerThreadId, std::memory_order_acq_rel) && expected != aOwnerThreadId)
        return false;

    auto& eventOwner = s_mapping->Header.EventConsumerThreadId;
    std::uint64_t expectedMappedOwner = 0;
    if (!eventOwner.compare_exchange_strong(expectedMappedOwner, aOwnerThreadId, std::memory_order_release, std::memory_order_acquire) &&
        expectedMappedOwner != aOwnerThreadId)
    {
        s_ownerThreadId.store(0, std::memory_order_release);
        return false;
    }

    return true;
}

void Retire() noexcept
{
    if (!IsLocalMappingValid())
        return;

    if (ReadState() == EndpointState::Ready && IsOwnerThread())
        (void)RetireSession(EpochRetireReason::Shutdown);

    auto& header = s_mapping->Header;
    auto state = header.State.load(std::memory_order_acquire);
    while (state == static_cast<std::uint32_t>(EndpointState::Prepared) || state == static_cast<std::uint32_t>(EndpointState::Ready))
    {
        if (header.State.compare_exchange_weak(state, static_cast<std::uint32_t>(EndpointState::Retiring),
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            header.ActiveCapabilities.store(0, std::memory_order_release);
            header.LifecycleEpoch.fetch_add(1, std::memory_order_acq_rel);
            header.State.store(static_cast<std::uint32_t>(EndpointState::Retired), std::memory_order_release);
            return;
        }
    }
}

bool RetireSession(const EpochRetireReason aReason) noexcept
{
    if (!IsOperational())
        return false;

    CommandRecord command{};
    command.Header.Kind = static_cast<std::uint16_t>(CommandKind::RetireEpoch);
    command.Header.PayloadSize = kFixedPayloadBytes;
    command.Header.Identity.LifecycleEpoch = s_mapping->Header.LifecycleEpoch.load(std::memory_order_acquire);
    command.Payload.RetireEpoch.RetiredLifecycleEpoch = command.Header.Identity.LifecycleEpoch;
    command.Payload.RetireEpoch.Reason = static_cast<std::uint32_t>(aReason);
    if (!TrySubmitCommand(command))
        return false;

    const auto previousEpoch = command.Header.Identity.LifecycleEpoch;
    if (PumpCommands(kDefaultCommandRingCapacity) != CommandPumpResult::Success ||
        s_mapping->Header.LifecycleEpoch.load(std::memory_order_acquire) == previousEpoch)
        return false;

    s_lastBridgeSequence.store(0, std::memory_order_release);
    s_lastBridgeAction.store(0, std::memory_order_release);
    return true;
}

bool IsReady() noexcept
{
    const auto owner = s_ownerThreadId.load(std::memory_order_acquire);
    return owner != 0 && IsLocalMappingValid() && ReadState() == EndpointState::Ready &&
           s_mapping->Header.EventConsumerThreadId.load(std::memory_order_acquire) == owner;
}

Diagnostics GetDiagnostics() noexcept
{
    Diagnostics diagnostics{};
    if (!IsLocalMappingValid())
        return diagnostics;

    const auto state = ReadState();
    diagnostics.Initialized = true;
    diagnostics.Ready = IsReady();
    diagnostics.Retired = state == EndpointState::Retired;
    diagnostics.OwnerThreadId = s_ownerThreadId.load(std::memory_order_acquire);
    diagnostics.EndpointState = static_cast<std::uint32_t>(state);
    diagnostics.LifecycleEpoch = s_mapping->Header.LifecycleEpoch.load(std::memory_order_acquire);
    diagnostics.RequestedCapabilities = s_mapping->Header.RequestedCapabilities.load(std::memory_order_acquire);
    diagnostics.AvailableCapabilities = s_mapping->Header.AvailableCapabilities.load(std::memory_order_acquire);
    diagnostics.ActiveCapabilities = s_mapping->Header.ActiveCapabilities.load(std::memory_order_acquire);
    diagnostics.ProducedEventCount = s_mapping->Header.ProducedEventCount.load(std::memory_order_acquire);
    diagnostics.ConsumedEventCount = s_mapping->Header.ConsumedEventCount.load(std::memory_order_acquire);
    diagnostics.SubmittedCommandCount = s_mapping->Header.SubmittedCommandCount.load(std::memory_order_acquire);
    diagnostics.ExecutedCommandCount = s_mapping->Header.ExecutedCommandCount.load(std::memory_order_acquire);
    diagnostics.RejectedCommandCount = s_mapping->Header.RejectedCommandCount.load(std::memory_order_acquire);
    diagnostics.StaleCommandCount = s_mapping->Header.StaleCommandCount.load(std::memory_order_acquire);
    diagnostics.DiscardedEventCount = s_discardedEventCount.load(std::memory_order_relaxed);
    diagnostics.RejectedSubmissionCount = s_rejectedSubmissionCount.load(std::memory_order_relaxed);
    diagnostics.EventRingDroppedPushCount = s_mapping->Events.DroppedPushCount.load(std::memory_order_acquire);
    diagnostics.CommandRingDroppedPushCount = s_mapping->Commands.DroppedPushCount.load(std::memory_order_acquire);
    return diagnostics;
}

void UpdateSessionIdentity(const std::uint64_t aServerInstanceNonce, const std::uint64_t aConnectionGeneration) noexcept
{
    const auto state = ReadState();
    if (!IsOwnerThread() || (state != EndpointState::Prepared && state != EndpointState::Ready))
        return;

    s_mapping->Header.ServerInstanceNonce.store(aServerInstanceNonce, std::memory_order_release);
    s_mapping->Header.ConnectionGeneration.store(aConnectionGeneration, std::memory_order_release);
}

std::uint64_t GetLifecycleEpoch() noexcept
{
    return IsLocalMappingValid() ? s_mapping->Header.LifecycleEpoch.load(std::memory_order_acquire) : 0;
}

CapabilityMask GetActiveCapabilities() noexcept
{
    if (!IsLocalMappingValid())
        return 0;

    const auto requested = s_mapping->Header.RequestedCapabilities.load(std::memory_order_acquire);
    const auto available = s_mapping->Header.AvailableCapabilities.load(std::memory_order_acquire);
    const auto active = s_mapping->Header.ActiveCapabilities.load(std::memory_order_acquire);
    return (active & ~available) == 0 && (active & ~requested) == 0 ? active : 0;
}

bool TryConsumeEvent(EventRecord& arEvent) noexcept
{
    if (!IsOperational())
        return false;

    EventRecord event{};
    while (TryPop(s_mapping->Events, event))
    {
        if (ValidateEvent(event))
        {
            s_mapping->Header.ConsumedEventCount.fetch_add(1, std::memory_order_relaxed);
            arEvent = event;
            return true;
        }
        s_discardedEventCount.fetch_add(1, std::memory_order_relaxed);
    }
    return false;
}

bool TrySubmitCommand(CommandRecord& arCommand) noexcept
{
    if (!IsOperational() || arCommand.Header.PayloadSize != kFixedPayloadBytes || arCommand.Header.Flags != 0 ||
        !IdentityIsCurrentOrUnspecified(arCommand.Header.Identity))
    {
        RejectSubmission();
        return false;
    }

    const auto kind = static_cast<CommandKind>(arCommand.Header.Kind);
    const auto requiredCapability = (kind == CommandKind::ApplyGameplayAction ||
                                     kind == CommandKind::ApplyGameplayTextChunk) ?
        static_cast<CapabilityMask>(CapabilityForDomain(
            static_cast<GameplayDomain>(kind == CommandKind::ApplyGameplayAction ?
                arCommand.Payload.ApplyGameplayAction.Domain : arCommand.Payload.ApplyGameplayTextChunk.Domain))) :
        RequiredCapability(kind);
    if (requiredCapability == 0 || !HasActiveCapabilities(requiredCapability) || !ValidateCommandPayload(arCommand))
    {
        RejectSubmission();
        return false;
    }

    CommandRecord command = arCommand;
    auto& identity = command.Header.Identity;
    identity.ServerInstanceNonce = s_mapping->Header.ServerInstanceNonce.load(std::memory_order_acquire);
    identity.ConnectionGeneration = s_mapping->Header.ConnectionGeneration.load(std::memory_order_acquire);
    identity.LifecycleEpoch = s_mapping->Header.LifecycleEpoch.load(std::memory_order_acquire);

    const bool isSequencedUpdate = kind == CommandKind::UpdateRemoteRootTransform ||
                                   kind == CommandKind::ApplyRemoteAnimationGraphChunk;
    auto& lastCounter = isSequencedUpdate ? s_lastBridgeSequence : s_lastBridgeAction;
    auto& suppliedCounter = isSequencedUpdate ? identity.SequenceId : identity.ActionId;
    if (suppliedCounter != 0)
    {
        auto previousCounter = lastCounter.load(std::memory_order_acquire);
        while (suppliedCounter > previousCounter &&
               !lastCounter.compare_exchange_weak(previousCounter, suppliedCounter,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire))
        {
        }
        if (suppliedCounter <= previousCounter)
        {
            RejectSubmission();
            return false;
        }
    }
    else
    {
        suppliedCounter = ClaimNextCounter(lastCounter);
    }
    if (suppliedCounter == 0 || !TryPush(s_mapping->Commands, command))
    {
        RejectSubmission();
        return false;
    }

    s_mapping->Header.SubmittedCommandCount.fetch_add(1, std::memory_order_relaxed);
    arCommand = command;
    return true;
}

bool TrySubmitCommandBatch(CommandRecord* apCommands, const std::size_t aCommandCount) noexcept
{
    if (!apCommands || aCommandCount == 0 || aCommandCount > kDefaultCommandRingCapacity || !IsOperational())
    {
        RejectSubmission();
        return false;
    }

    std::vector<CommandRecord> commands;
    commands.reserve(aCommandCount);
    const auto nonce = s_mapping->Header.ServerInstanceNonce.load(std::memory_order_acquire);
    const auto generation = s_mapping->Header.ConnectionGeneration.load(std::memory_order_acquire);
    const auto epoch = s_mapping->Header.LifecycleEpoch.load(std::memory_order_acquire);
    BridgeIdentity transactionIdentity{};
    AdapterHandle transactionTarget{};
    GameplayDomain transactionDomain{};
    bool gameplayActionBatch{};

    for (std::size_t index = 0; index < aCommandCount; ++index)
    {
        auto command = apCommands[index];
        const auto kind = static_cast<CommandKind>(command.Header.Kind);
        if (!IsBatchCommand(kind) || command.Header.PayloadSize != kFixedPayloadBytes || command.Header.Flags != 0 ||
            command.Header.Identity.ActionId != 0 || !IdentityIsCurrentOrUnspecified(command.Header.Identity))
        {
            RejectSubmission();
            return false;
        }

        const bool gameplayAction = kind == CommandKind::ApplyGameplayAction;
        if (index == 0)
            gameplayActionBatch = gameplayAction;
        else if (gameplayAction != gameplayActionBatch)
        {
            RejectSubmission();
            return false;
        }
        if (gameplayAction)
        {
            const auto& payload = command.Payload.ApplyGameplayAction;
            const auto domain = static_cast<GameplayDomain>(payload.Domain);
            if (payload.TargetHandle.Value == 0 || payload.TargetHandle.Value == kLocalPlayerHandle.Value ||
                (index != 0 && (payload.TargetHandle.Value != transactionTarget.Value || domain != transactionDomain)))
            {
                RejectSubmission();
                return false;
            }
            if (index == 0)
            {
                transactionTarget = payload.TargetHandle;
                transactionDomain = domain;
            }
        }

        const auto requiredCapability = (kind == CommandKind::ApplyGameplayAction ||
                                         kind == CommandKind::ApplyGameplayTextChunk) ?
            static_cast<CapabilityMask>(CapabilityForDomain(
                static_cast<GameplayDomain>(kind == CommandKind::ApplyGameplayAction ?
                    command.Payload.ApplyGameplayAction.Domain :
                    command.Payload.ApplyGameplayTextChunk.Domain))) :
            RequiredCapability(kind);
        if (requiredCapability == 0 || !HasActiveCapabilities(requiredCapability) || !ValidateCommandPayload(command))
        {
            RejectSubmission();
            return false;
        }

        auto& identity = command.Header.Identity;
        identity.ServerInstanceNonce = nonce;
        identity.ConnectionGeneration = generation;
        identity.LifecycleEpoch = epoch;
        if (index == 0)
            transactionIdentity = identity;
        else if (identity.EntityId != transactionIdentity.EntityId ||
                 identity.EntityGeneration != transactionIdentity.EntityGeneration ||
                 identity.SequenceId != transactionIdentity.SequenceId)
        {
            RejectSubmission();
            return false;
        }
        commands.push_back(command);
    }

    const auto actionId = ClaimNextCounter(s_lastBridgeAction);
    if (actionId == 0)
    {
        RejectSubmission();
        return false;
    }
    for (auto& command : commands)
        NormalizeBatchTransactionId(command, actionId);

    if (!TryPushBatch(s_mapping->Commands, commands.data(), commands.size()))
    {
        RejectSubmission();
        return false;
    }

    for (std::size_t index = 0; index < commands.size(); ++index)
        apCommands[index] = commands[index];
    s_mapping->Header.SubmittedCommandCount.fetch_add(commands.size(), std::memory_order_relaxed);
    return true;
}

CommandPumpResult PumpCommands(std::uint32_t aMaxCommands) noexcept
{
    if (!IsOwnerThread())
        return CommandPumpResult::WrongThread;

    const auto state = ReadState();
    if (state != EndpointState::Prepared && state != EndpointState::Ready)
        return CommandPumpResult::Inactive;

    const auto module = GetModuleHandleW(L"SkyrimTogetherVRGameplayBridge.dll");
    if (!module)
        return CommandPumpResult::Inactive;

    const auto exportAddress = GetProcAddress(module, "STVRGameplayBridge_ProcessCommands");
    MEMORY_BASIC_INFORMATION memoryInfo{};
    if (!exportAddress || VirtualQuery(reinterpret_cast<const void*>(exportAddress), &memoryInfo, sizeof(memoryInfo)) != sizeof(memoryInfo) ||
        memoryInfo.State != MEM_COMMIT || memoryInfo.AllocationBase != module || !IsExecutableProtection(memoryInfo.Protect))
        return CommandPumpResult::Faulted;

    using ProcessCommands = std::uint32_t(__cdecl*)(std::uint32_t, std::uint32_t, std::uint64_t, std::uint32_t) noexcept;
    const auto processCommands = reinterpret_cast<ProcessCommands>(exportAddress);
    const auto maxCommands = aMaxCommands > kDefaultCommandRingCapacity ? kDefaultCommandRingCapacity : aMaxCommands;
    return MapPumpResult(processCommands(GetCurrentProcessId(), GetCurrentThreadId(),
        s_mapping->Header.LifecycleEpoch.load(std::memory_order_acquire), maxCommands));
}
} // namespace SkyrimTogetherVR::GameplayBridgeClient
