#include <TiltedOnlinePCH.h>

#include <Services/VRAvatarService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Services/TransportService.h>
#include <Structs/GameplayCapabilities.h>
#include <World.h>
#include <VRCanonicalEntityIdentity.h>
#include <VRGameplayBridge.h>
#include <vr_common/VRHandoffPath.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace GameplayBridge = SkyrimTogetherVR::GameplayBridge;

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS
#define TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS 0
#endif

namespace
{
constexpr double kAssignmentRetrySeconds = 5.0;
constexpr double kLocalMovementIntervalSeconds = 0.1;
constexpr double kMaxInterpolationDeltaSeconds = 0.1;
constexpr double kStatusWriteIntervalSeconds = 1.0;
constexpr double kCreateRetryIntervalSeconds = 0.25;
constexpr double kCommandResultTimeoutSeconds = 2.0;
constexpr float kInterpolationRate = 12.0f;
constexpr float kPositionConvergenceSquared = 0.01f;
constexpr float kRotationConvergenceDot = 0.99999f;
constexpr float kScaleConvergence = 0.0001f;
constexpr std::size_t kMaxRemoteAvatars = 64;
constexpr std::uint8_t kMaximumCreateAttempts = 3;
constexpr std::uint64_t kSnapshotHasPlayer = 1ull << 0;
constexpr std::uint64_t kSnapshotHasCell = 1ull << 1;

[[nodiscard]] bool IsFinite(const float aValue) noexcept
{
    return std::isfinite(aValue);
}

[[nodiscard]] bool IsFinite(const glm::vec2& acValue) noexcept
{
    return IsFinite(acValue.x) && IsFinite(acValue.y);
}

[[nodiscard]] bool IsFinite(const glm::vec3& acValue) noexcept
{
    return IsFinite(acValue.x) && IsFinite(acValue.y) && IsFinite(acValue.z);
}

[[nodiscard]] bool IsFinite(const GameplayBridge::RootTransform& acRoot) noexcept
{
    return IsFinite(acRoot.PositionX) && IsFinite(acRoot.PositionY) && IsFinite(acRoot.PositionZ) &&
           IsFinite(acRoot.RotationX) && IsFinite(acRoot.RotationY) && IsFinite(acRoot.RotationZ) &&
           IsFinite(acRoot.RotationW) && IsFinite(acRoot.Scale);
}

[[nodiscard]] bool NormalizeRotation(GameplayBridge::RootTransform& arRoot) noexcept
{
    const auto lengthSquared = arRoot.RotationX * arRoot.RotationX + arRoot.RotationY * arRoot.RotationY +
                               arRoot.RotationZ * arRoot.RotationZ + arRoot.RotationW * arRoot.RotationW;
    if (!IsFinite(lengthSquared) || lengthSquared <= std::numeric_limits<float>::epsilon())
        return false;

    const auto inverseLength = 1.0f / std::sqrt(lengthSquared);
    arRoot.RotationX *= inverseLength;
    arRoot.RotationY *= inverseLength;
    arRoot.RotationZ *= inverseLength;
    arRoot.RotationW *= inverseLength;
    return IsFinite(arRoot);
}

[[nodiscard]] GameplayBridge::RootTransform ToRootTransform(const Vector3_NetQuantize& acPosition, const Rotator2_NetQuantize& acRotation) noexcept
{
    const auto halfPitch = acRotation.x * 0.5f;
    const auto halfYaw = acRotation.y * 0.5f;
    const auto sinPitch = std::sin(halfPitch);
    const auto cosPitch = std::cos(halfPitch);
    const auto sinYaw = std::sin(halfYaw);
    const auto cosYaw = std::cos(halfYaw);

    GameplayBridge::RootTransform root{};
    root.PositionX = acPosition.x;
    root.PositionY = acPosition.y;
    root.PositionZ = acPosition.z;
    root.RotationX = sinPitch * cosYaw;
    root.RotationY = sinPitch * sinYaw;
    root.RotationZ = cosPitch * sinYaw;
    root.RotationW = cosPitch * cosYaw;
    root.Scale = 1.0f;
    return root;
}

[[nodiscard]] bool ToNetworkRotation(const GameplayBridge::RootTransform& acRoot, Rotator2_NetQuantize& arRotation) noexcept
{
    auto root = acRoot;
    if (!NormalizeRotation(root))
        return false;

    const auto sinPitch = 2.0f * (root.RotationW * root.RotationX + root.RotationY * root.RotationZ);
    const auto cosPitch = 1.0f - 2.0f * (root.RotationX * root.RotationX + root.RotationY * root.RotationY);
    const auto sinYaw = 2.0f * (root.RotationW * root.RotationZ + root.RotationX * root.RotationY);
    const auto cosYaw = 1.0f - 2.0f * (root.RotationY * root.RotationY + root.RotationZ * root.RotationZ);
    arRotation.x = std::atan2(sinPitch, cosPitch);
    arRotation.y = std::atan2(sinYaw, cosYaw);
    return IsFinite(arRotation);
}

void InterpolateRoot(GameplayBridge::RootTransform& arCurrent, const GameplayBridge::RootTransform& acTarget, const float aAlpha) noexcept
{
    const auto interpolate = [aAlpha](const float aCurrent, const float aTarget) noexcept
    {
        return aCurrent + (aTarget - aCurrent) * aAlpha;
    };

    arCurrent.PositionX = interpolate(arCurrent.PositionX, acTarget.PositionX);
    arCurrent.PositionY = interpolate(arCurrent.PositionY, acTarget.PositionY);
    arCurrent.PositionZ = interpolate(arCurrent.PositionZ, acTarget.PositionZ);
    const auto rotationDot = arCurrent.RotationX * acTarget.RotationX + arCurrent.RotationY * acTarget.RotationY +
                             arCurrent.RotationZ * acTarget.RotationZ + arCurrent.RotationW * acTarget.RotationW;
    const auto targetSign = rotationDot < 0.0f ? -1.0f : 1.0f;
    arCurrent.RotationX = interpolate(arCurrent.RotationX, acTarget.RotationX * targetSign);
    arCurrent.RotationY = interpolate(arCurrent.RotationY, acTarget.RotationY * targetSign);
    arCurrent.RotationZ = interpolate(arCurrent.RotationZ, acTarget.RotationZ * targetSign);
    arCurrent.RotationW = interpolate(arCurrent.RotationW, acTarget.RotationW * targetSign);
    arCurrent.Scale = interpolate(arCurrent.Scale, acTarget.Scale);
}

[[nodiscard]] bool IsRootConverged(
    const GameplayBridge::RootTransform& acCurrent,
    const GameplayBridge::RootTransform& acTarget) noexcept
{
    const auto dx = acCurrent.PositionX - acTarget.PositionX;
    const auto dy = acCurrent.PositionY - acTarget.PositionY;
    const auto dz = acCurrent.PositionZ - acTarget.PositionZ;
    const auto rotationDot = std::abs(acCurrent.RotationX * acTarget.RotationX + acCurrent.RotationY * acTarget.RotationY +
                                      acCurrent.RotationZ * acTarget.RotationZ + acCurrent.RotationW * acTarget.RotationW);
    return dx * dx + dy * dy + dz * dz <= kPositionConvergenceSquared &&
           rotationDot >= kRotationConvergenceDot && std::abs(acCurrent.Scale - acTarget.Scale) <= kScaleConvergence;
}

[[nodiscard]] bool IsRetryableCreateStatus(const GameplayBridge::CommandStatus aStatus) noexcept
{
    return aStatus == GameplayBridge::CommandStatus::Inactive ||
           aStatus == GameplayBridge::CommandStatus::MissingCell ||
           aStatus == GameplayBridge::CommandStatus::EngineRejected ||
           aStatus == GameplayBridge::CommandStatus::QueueOverflow;
}
} // namespace

VRAvatarService::VRAvatarService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_statusPath(SkyrimTogetherVR::Handoff::GetFile("SkyrimTogetherVR.avatar"))
{
    std::error_code ec;
    std::filesystem::create_directories(m_statusPath.parent_path(), ec);

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRAvatarService::OnUpdate>(this);
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&VRAvatarService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRAvatarService::OnDisconnected>(this);
    m_assignCharacterConnection = aDispatcher.sink<AssignCharacterResponse>().connect<&VRAvatarService::OnAssignCharacter>(this);
    m_characterSpawnConnection = aDispatcher.sink<CharacterSpawnRequest>().connect<&VRAvatarService::OnCharacterSpawn>(this);
    m_referencesMoveConnection = aDispatcher.sink<ServerReferencesMoveRequest>().connect<&VRAvatarService::OnReferencesMoveRequest>(this);
    m_removeCharacterConnection = aDispatcher.sink<NotifyRemoveCharacter>().connect<&VRAvatarService::OnRemoveCharacter>(this);
}

void VRAvatarService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    ConsumeBridgeEvents();

    const auto delta = std::clamp(acEvent.Delta, 0.0, kMaxInterpolationDeltaSeconds);
    if (m_connected && m_hasLocalSnapshot && !m_localServerId)
    {
        m_assignmentElapsed += delta;
        if (!m_assignmentPending || m_assignmentElapsed >= kAssignmentRetrySeconds)
        {
            m_assignmentPending = false;
            TryRequestLocalAssignment();
        }
    }

    if (m_localServerId)
    {
        m_localMovementElapsed += delta;
        if (m_localMovementElapsed >= kLocalMovementIntervalSeconds)
        {
            m_localMovementElapsed = 0.0;
            SendLocalMovement();
        }
    }

    UpdateRemoteAvatars(delta);

    m_statusElapsed += delta;
    if (m_statusDirty || m_statusElapsed >= kStatusWriteIntervalSeconds)
    {
        m_statusDirty = false;
        m_statusElapsed = 0.0;
        WriteStatus();
    }
}

void VRAvatarService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    m_remoteAvatars.clear();
    ResetStatusCounters();
    m_localServerId.reset();
    m_assignmentCookie = 0;
    m_assignmentElapsed = 0.0;
    m_localMovementElapsed = 0.0;
    m_assignmentPending = false;
    m_connected = true;
    m_localPlayerId = acEvent.PlayerId;
    m_capabilityWarningLogged = false;
    m_statusDirty = true;
    TryRequestLocalAssignment();
}

void VRAvatarService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);
    ResetSessionState();
}

void VRAvatarService::OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept
{
    if (!m_connected || !m_assignmentPending || acMessage.Cookie != m_assignmentCookie)
        return;

    if (!acMessage.Owner)
    {
        spdlog::warn("VR avatar assignment response rejected because the response does not grant ownership");
        return;
    }

    m_localServerId = acMessage.ServerId;
    if (m_remoteAvatars.erase(*m_localServerId) != 0)
        spdlog::warn("VR avatar canonical server-id conflict cleared for local server id {}", *m_localServerId);
    m_assignmentPending = false;
    m_assignmentElapsed = 0.0;
    m_localMovementElapsed = 0.0;
    m_statusDirty = true;
}

void VRAvatarService::OnCharacterSpawn(const CharacterSpawnRequest& acMessage) noexcept
{
    if (!m_connected || !acMessage.IsPlayer || acMessage.PlayerId == 0 ||
        acMessage.PlayerId == m_localPlayerId || (m_localServerId && acMessage.ServerId == *m_localServerId) || !HasValidLocalSnapshot())
        return;

    if (!CanSubmitAvatarCommands())
        return;

    const auto localCellId = m_world.GetModSystem().GetGameId(acMessage.CellId);
    if (!acMessage.CellId || localCellId == 0 || localCellId != m_localSnapshot.LocalCellFormId ||
        !IsFinite(acMessage.Position) || !IsFinite(acMessage.Rotation) || m_localSnapshot.LocalActorBaseFormId == 0)
    {
        if (!IsFinite(acMessage.Position) || !IsFinite(acMessage.Rotation))
        {
            ++m_invalidTransformCount;
            m_statusDirty = true;
        }
        spdlog::warn("VR avatar spawn rejected for server id {} because its cell, transform, or local visual base is invalid", acMessage.ServerId);
        return;
    }

    auto root = ToRootTransform(acMessage.Position, acMessage.Rotation);
    if (!IsFinite(root) || !NormalizeRotation(root))
    {
        ++m_invalidTransformCount;
        m_statusDirty = true;
        return;
    }

    if (const auto existingIt = m_remoteAvatars.find(acMessage.ServerId); existingIt != m_remoteAvatars.end())
    {
        auto& existing = existingIt->second;
        if (existing.PlayerId == acMessage.PlayerId && existing.RemovalRequested)
        {
            if (existing.DestroyPending)
            {
                existing.CurrentRoot = root;
                existing.TargetRoot = root;
                existing.HasTarget = false;
                existing.RespawnRequested = true;
            }
            else
            {
                existing.RemovalRequested = false;
                existing.RespawnRequested = false;
                existing.TargetRoot = root;
                existing.HasTarget = existing.Handle.Value != 0 || existing.CreatePending;
            }
            m_statusDirty = true;
            return;
        }

        spdlog::warn("VR avatar spawn rejected for duplicate server id {}", acMessage.ServerId);
        return;
    }

    if (m_remoteAvatars.size() >= kMaxRemoteAvatars)
    {
        spdlog::warn("VR avatar spawn rejected because the avatar limit was reached for server id {}", acMessage.ServerId);
        return;
    }

    for (const auto& [serverId, avatar] : m_remoteAvatars)
    {
        if (avatar.PlayerId == acMessage.PlayerId)
        {
            spdlog::warn("VR avatar spawn rejected for player id {} with conflicting server id {}", acMessage.PlayerId, serverId);
            return;
        }
    }

    RemoteAvatar avatar{};
    avatar.PlayerId = acMessage.PlayerId;
    avatar.CurrentRoot = root;
    avatar.TargetRoot = root;
    avatar.HasTarget = false;
    const auto [it, inserted] = m_remoteAvatars.emplace(acMessage.ServerId, avatar);
    if (inserted)
    {
        ++m_sameSpaceCount;
        m_statusDirty = true;
        SubmitCreateRemoteAvatar(acMessage.ServerId, it->second);
    }
}

void VRAvatarService::OnReferencesMoveRequest(const ServerReferencesMoveRequest& acMessage) noexcept
{
    if (!m_connected || !m_hasLocalSnapshot)
        return;

    for (const auto& [serverId, update] : acMessage.Updates)
    {
        const auto avatarIt = m_remoteAvatars.find(serverId);
        if (avatarIt == m_remoteAvatars.end() || avatarIt->second.RemovalRequested)
            continue;

        const auto& movement = update.UpdatedMovement;
        const auto localCellId = m_world.GetModSystem().GetGameId(movement.CellId);
        if (!movement.CellId || localCellId == 0 || localCellId != m_localSnapshot.LocalCellFormId ||
            !IsFinite(movement.Position) || !IsFinite(movement.Rotation))
        {
            if (!IsFinite(movement.Position) || !IsFinite(movement.Rotation))
            {
                ++m_invalidTransformCount;
                m_statusDirty = true;
            }
            continue;
        }

        if (movement.WorldSpaceId)
        {
            const auto localWorldspaceId = m_world.GetModSystem().GetGameId(movement.WorldSpaceId);
            if (localWorldspaceId == 0 || localWorldspaceId != m_localSnapshot.LocalWorldspaceFormId)
                continue;
        }

        auto root = ToRootTransform(movement.Position, movement.Rotation);
        if (!IsFinite(root) || !NormalizeRotation(root))
        {
            ++m_invalidTransformCount;
            m_statusDirty = true;
            continue;
        }

        avatarIt->second.TargetRoot = root;
        avatarIt->second.HasTarget = true;
        ++m_remoteMovementAcceptedCount;
        ++m_sameSpaceCount;
    }
}

void VRAvatarService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept
{
    const auto avatarIt = m_remoteAvatars.find(acMessage.ServerId);
    if (avatarIt == m_remoteAvatars.end())
        return;

    auto& avatar = avatarIt->second;
    avatar.RemovalRequested = true;
    m_statusDirty = true;
    if (avatar.CreatePending)
        return;

    if (avatar.Handle.Value != 0 && !avatar.DestroyPending)
        SubmitDestroyRemoteAvatar(acMessage.ServerId, avatar);
    else if (avatar.Handle.Value == 0)
        m_remoteAvatars.erase(avatarIt);
}

void VRAvatarService::ConsumeBridgeEvents() noexcept
{
    GameplayBridge::EventRecord event{};
    while (SkyrimTogetherVR::GameplayBridgeClient::TryConsumeEvent(event))
    {
        switch (static_cast<GameplayBridge::EventKind>(event.Header.Kind))
        {
        case GameplayBridge::EventKind::Lifecycle:
            HandleBridgeLifecycle(event);
            break;
        case GameplayBridge::EventKind::LocalPlayerState:
            HandleBridgeLocalPlayerState(event);
            break;
        case GameplayBridge::EventKind::RemoteAvatarState:
            HandleBridgeRemoteAvatarState(event);
            break;
        default:
            break;
        }
    }
}

void VRAvatarService::HandleBridgeLifecycle(const GameplayBridge::EventRecord& acEvent) noexcept
{
    const auto state = static_cast<GameplayBridge::LifecycleState>(acEvent.Payload.Lifecycle.ObservedState);
    if (state == GameplayBridge::LifecycleState::NewGame || state == GameplayBridge::LifecycleState::PreLoadGame ||
        state == GameplayBridge::LifecycleState::CellChanged || state == GameplayBridge::LifecycleState::EpochRetired)
        ResetLifecycleState();
}

void VRAvatarService::HandleBridgeLocalPlayerState(const GameplayBridge::EventRecord& acEvent) noexcept
{
    const auto& snapshot = acEvent.Payload.LocalPlayerState;
    auto normalizedRoot = snapshot.Root;
    if (snapshot.LocalPlayerHandle.Value != GameplayBridge::kLocalPlayerHandle.Value ||
        (snapshot.SnapshotFlags & (kSnapshotHasPlayer | kSnapshotHasCell)) != (kSnapshotHasPlayer | kSnapshotHasCell) ||
        snapshot.LocalCellFormId == 0 || snapshot.LocalActorBaseFormId == 0 || !IsFinite(normalizedRoot) ||
        normalizedRoot.Scale <= 0.0f || !NormalizeRotation(normalizedRoot))
        return;

    m_localSnapshot = snapshot;
    m_localSnapshot.Root = normalizedRoot;
    m_hasLocalSnapshot = true;
    if (m_connected && !m_localServerId && !m_assignmentPending)
        TryRequestLocalAssignment();
}

void VRAvatarService::HandleBridgeRemoteAvatarState(const GameplayBridge::EventRecord& acEvent) noexcept
{
    std::uint32_t serverId{};
    if (!SkyrimTogetherVR::CanonicalEntity::TryJoinServerId(
            acEvent.Header.Identity.EntityId,
            acEvent.Header.Identity.EntityGeneration,
            serverId))
        return;

    const auto avatarIt = m_remoteAvatars.find(serverId);
    if (avatarIt == m_remoteAvatars.end())
        return;

    auto& avatar = avatarIt->second;
    const auto state = static_cast<GameplayBridge::RemoteAvatarState>(acEvent.Payload.RemoteAvatarState.State);
    const auto status = static_cast<GameplayBridge::CommandStatus>(acEvent.Payload.RemoteAvatarState.Status);
    const auto actionId = acEvent.Header.Identity.ActionId;
    const auto sequenceId = acEvent.Header.Identity.SequenceId;
    switch (state)
    {
    case GameplayBridge::RemoteAvatarState::Created:
        if (status != GameplayBridge::CommandStatus::Success || !avatar.CreatePending ||
            actionId != avatar.PendingCreateActionId || sequenceId != 0 ||
            acEvent.Payload.RemoteAvatarState.AvatarHandle.Value == 0)
            return;
        avatar.CreatePending = false;
        avatar.CreatePendingElapsed = 0.0;
        avatar.PendingCreateActionId = 0;
        avatar.Handle = acEvent.Payload.RemoteAvatarState.AvatarHandle;
        ++m_createSucceededCount;
        m_statusDirty = true;
        if (avatar.RemovalRequested)
            SubmitDestroyRemoteAvatar(serverId, avatar);
        break;
    case GameplayBridge::RemoteAvatarState::Destroyed:
        if (status != GameplayBridge::CommandStatus::Success || !avatar.DestroyPending ||
            actionId != avatar.PendingDestroyActionId || sequenceId != 0)
            return;
        ++m_destroySucceededCount;
        m_statusDirty = true;
        if (avatar.RespawnRequested)
        {
            avatar.Handle = {};
            avatar.CreatePending = false;
            avatar.DestroyPending = false;
            avatar.RemovalRequested = false;
            avatar.RespawnRequested = false;
            avatar.CreateAttempts = 0;
            avatar.CreatePendingElapsed = 0.0;
            avatar.DestroyPendingElapsed = 0.0;
            avatar.PendingCreateActionId = 0;
            avatar.PendingDestroyActionId = 0;
            avatar.LastSubmittedSequenceId = 0;
            SubmitCreateRemoteAvatar(serverId, avatar);
            break;
        }
        m_remoteAvatars.erase(avatarIt);
        break;
    case GameplayBridge::RemoteAvatarState::Rejected:
    case GameplayBridge::RemoteAvatarState::Faulted:
        if (sequenceId != 0)
        {
            if (sequenceId == avatar.LastSubmittedSequenceId)
                RetireAvatarLifecycle("latest remote root update was rejected");
            return;
        }

        if (avatar.DestroyPending && actionId == avatar.PendingDestroyActionId)
        {
            RetireAvatarLifecycle("remote avatar destroy was rejected");
            return;
        }
        if (!avatar.CreatePending || actionId != avatar.PendingCreateActionId)
            return;

        avatar.CreatePending = false;
        avatar.CreatePendingElapsed = 0.0;
        avatar.PendingCreateActionId = 0;
        m_statusDirty = true;
        if (avatar.RemovalRequested || !IsRetryableCreateStatus(status))
        {
            if (status == GameplayBridge::CommandStatus::StaleEntity ||
                status == GameplayBridge::CommandStatus::StaleEpoch ||
                status == GameplayBridge::CommandStatus::StaleSession ||
                status == GameplayBridge::CommandStatus::WrongThread ||
                status == GameplayBridge::CommandStatus::Malformed ||
                status == GameplayBridge::CommandStatus::InvalidHandle)
            {
                RetireAvatarLifecycle("remote avatar create violated bridge identity or ownership invariants");
                return;
            }
            m_remoteAvatars.erase(avatarIt);
        }
        break;
    default:
        break;
    }
}

void VRAvatarService::ResetSessionState() noexcept
{
    m_remoteAvatars.clear();
    m_localSnapshot = {};
    m_localPlayerId = 0;
    m_localServerId.reset();
    m_assignmentCookie = 0;
    m_assignmentElapsed = 0.0;
    m_localMovementElapsed = 0.0;
    m_connected = false;
    m_hasLocalSnapshot = false;
    m_assignmentPending = false;
    m_statusDirty = true;
}

void VRAvatarService::ResetLifecycleState() noexcept
{
    m_remoteAvatars.clear();
    ResetStatusCounters();
    m_localSnapshot = {};
    m_localMovementElapsed = 0.0;
    m_hasLocalSnapshot = false;
    m_statusDirty = true;
    if (!m_localServerId)
    {
        m_assignmentElapsed = 0.0;
        m_assignmentPending = false;
    }
}

void VRAvatarService::TryRequestLocalAssignment() noexcept
{
    if (!m_connected || !m_transport.IsOnline() || m_localServerId || m_assignmentPending || !HasValidLocalSnapshot())
        return;

    GameId cellId{};
    GameId worldspaceId{};
    if (!BuildLocalLocation(cellId, worldspaceId))
        return;

    Rotator2_NetQuantize rotation{};
    if (!ToNetworkRotation(m_localSnapshot.Root, rotation))
        return;

    AssignCharacterRequest request{};
    request.Cookie = m_nextAssignmentCookie++;
    if (m_nextAssignmentCookie == 0)
        m_nextAssignmentCookie = 1;
    request.ReferenceId = GameId{0, 0x14};
    request.CellId = cellId;
    request.WorldSpaceId = worldspaceId;
    request.Position = glm::vec3{m_localSnapshot.Root.PositionX, m_localSnapshot.Root.PositionY, m_localSnapshot.Root.PositionZ};
    request.Rotation = rotation;

    m_assignmentCookie = request.Cookie;
    m_assignmentPending = true;
    m_assignmentElapsed = 0.0;
    if (!m_transport.Send(request))
        spdlog::warn("VR avatar assignment request was not queued; retry is bounded to {} seconds", kAssignmentRetrySeconds);
}

void VRAvatarService::SendLocalMovement() noexcept
{
    if (!m_connected || !m_transport.IsOnline() || !m_localServerId || !HasValidLocalSnapshot())
        return;

    GameId cellId{};
    GameId worldspaceId{};
    if (!BuildLocalLocation(cellId, worldspaceId))
        return;

    Rotator2_NetQuantize rotation{};
    if (!ToNetworkRotation(m_localSnapshot.Root, rotation))
        return;

    ClientReferencesMoveRequest request{};
    request.Tick = m_world.GetTick();
    auto& movement = request.Updates[*m_localServerId].UpdatedMovement;
    movement.CellId = cellId;
    movement.WorldSpaceId = worldspaceId;
    movement.Position = glm::vec3{m_localSnapshot.Root.PositionX, m_localSnapshot.Root.PositionY, m_localSnapshot.Root.PositionZ};
    movement.Rotation = rotation;
    m_transport.Send(request);
}

void VRAvatarService::UpdateRemoteAvatars(const double aDelta) noexcept
{
    if (m_remoteAvatars.empty() || !CanSubmitAvatarCommands())
        return;

    const auto alpha = std::min(1.0f, static_cast<float>(std::clamp(aDelta, 0.0, kMaxInterpolationDeltaSeconds)) * kInterpolationRate);
    for (auto avatarIt = m_remoteAvatars.begin(); avatarIt != m_remoteAvatars.end();)
    {
        const auto serverId = avatarIt->first;
        auto& avatar = avatarIt->second;

        if (avatar.CreatePending)
        {
            avatar.CreatePendingElapsed += aDelta;
            if (avatar.CreatePendingElapsed >= kCommandResultTimeoutSeconds)
            {
                if (avatar.CreateAttempts >= kMaximumCreateAttempts)
                {
                    RetireAvatarLifecycle("remote avatar create acknowledgement timed out");
                    return;
                }
                avatar.CreatePending = false;
                avatar.CreatePendingElapsed = 0.0;
                avatar.PendingCreateActionId = 0;
                SubmitCreateRemoteAvatar(serverId, avatar);
            }
            ++avatarIt;
            continue;
        }

        if (avatar.Handle.Value == 0)
        {
            if (avatar.RemovalRequested || avatar.CreateAttempts >= kMaximumCreateAttempts)
            {
                avatarIt = m_remoteAvatars.erase(avatarIt);
                m_statusDirty = true;
                continue;
            }

            avatar.CreatePendingElapsed += aDelta;
            if (avatar.CreatePendingElapsed >= kCreateRetryIntervalSeconds)
            {
                avatar.CreatePendingElapsed = 0.0;
                SubmitCreateRemoteAvatar(serverId, avatar);
            }
            ++avatarIt;
            continue;
        }

        if (avatar.DestroyPending)
        {
            avatar.DestroyPendingElapsed += aDelta;
            if (avatar.DestroyPendingElapsed >= kCommandResultTimeoutSeconds)
            {
                RetireAvatarLifecycle("remote avatar destroy acknowledgement timed out");
                return;
            }
            ++avatarIt;
            continue;
        }

        if (avatar.RemovalRequested)
        {
            avatar.DestroyPendingElapsed += aDelta;
            if (!avatar.DestroyPending && avatar.DestroyPendingElapsed >= kCreateRetryIntervalSeconds)
                SubmitDestroyRemoteAvatar(serverId, avatar);
            ++avatarIt;
            continue;
        }

        if (!avatar.HasTarget)
        {
            ++avatarIt;
            continue;
        }

        InterpolateRoot(avatar.CurrentRoot, avatar.TargetRoot, alpha);
        if (!IsFinite(avatar.CurrentRoot) || !NormalizeRotation(avatar.CurrentRoot))
        {
            ++m_invalidTransformCount;
            m_statusDirty = true;
            ++avatarIt;
            continue;
        }

        const auto converged = IsRootConverged(avatar.CurrentRoot, avatar.TargetRoot);
        if (converged)
            avatar.CurrentRoot = avatar.TargetRoot;

        GameplayBridge::CommandRecord command{};
        if (!BuildCommand(GameplayBridge::CommandKind::UpdateRemoteRootTransform, serverId, command))
        {
            ++avatarIt;
            continue;
        }
        command.Payload.UpdateRemoteRootTransform.AvatarHandle = avatar.Handle;
        command.Payload.UpdateRemoteRootTransform.Root = avatar.CurrentRoot;
        if (SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command))
        {
            avatar.LastSubmittedSequenceId = command.Header.Identity.SequenceId;
            if (converged)
                avatar.HasTarget = false;
            ++m_updateSubmittedCount;
        }
        else
            m_statusDirty = true;
        ++avatarIt;
    }
}

void VRAvatarService::SubmitCreateRemoteAvatar(const std::uint32_t aServerId, RemoteAvatar& arAvatar) noexcept
{
    if (!CanSubmitAvatarCommands() || !HasValidLocalSnapshot() || arAvatar.CreatePending ||
        arAvatar.Handle.Value != 0 || arAvatar.CreateAttempts >= kMaximumCreateAttempts)
        return;

    ++arAvatar.CreateAttempts;
    arAvatar.CreatePendingElapsed = 0.0;

    GameplayBridge::CommandRecord command{};
    if (!BuildCommand(GameplayBridge::CommandKind::CreateRemoteAvatar, aServerId, command))
        return;

    // Player spawn messages do not carry a usable NPC base yet. The local
    // player's base is an explicit visual-template fallback, not appearance parity.
    command.Payload.CreateRemoteAvatar.LocalActorBaseFormId = m_localSnapshot.LocalActorBaseFormId;
    command.Payload.CreateRemoteAvatar.LocalCellFormId = m_localSnapshot.LocalCellFormId;
    command.Payload.CreateRemoteAvatar.LocalWorldspaceFormId = m_localSnapshot.LocalWorldspaceFormId;
    command.Payload.CreateRemoteAvatar.InitialRoot = arAvatar.CurrentRoot;
    if (SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command))
    {
        arAvatar.CreatePending = true;
        arAvatar.PendingCreateActionId = command.Header.Identity.ActionId;
        ++m_createSubmittedCount;
        m_statusDirty = true;
    }
    else
    {
        arAvatar.CreatePending = false;
        arAvatar.PendingCreateActionId = 0;
        m_statusDirty = true;
    }
}

void VRAvatarService::SubmitDestroyRemoteAvatar(const std::uint32_t aServerId, RemoteAvatar& arAvatar) noexcept
{
    if (!CanSubmitAvatarCommands() || arAvatar.Handle.Value == 0 || arAvatar.DestroyPending)
        return;

    arAvatar.DestroyPendingElapsed = 0.0;

    GameplayBridge::CommandRecord command{};
    if (!BuildCommand(GameplayBridge::CommandKind::DestroyRemoteAvatar, aServerId, command))
        return;

    command.Payload.DestroyRemoteAvatar.AvatarHandle = arAvatar.Handle;
    if (SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command))
    {
        arAvatar.DestroyPending = true;
        arAvatar.PendingDestroyActionId = command.Header.Identity.ActionId;
        ++m_destroySubmittedCount;
        m_statusDirty = true;
    }
    else
    {
        arAvatar.DestroyPending = false;
        arAvatar.PendingDestroyActionId = 0;
        m_statusDirty = true;
    }
}

void VRAvatarService::RetireAvatarLifecycle(const char* apReason) noexcept
{
    spdlog::error("VR avatar lifecycle retirement requested: {}", apReason ? apReason : "unspecified failure");
    if (!m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset))
        spdlog::error("VR avatar lifecycle retirement failed; avatar capabilities are quarantined until cleanup succeeds");
    ResetLifecycleState();
}

void VRAvatarService::ResetStatusCounters() noexcept
{
    m_statusElapsed = 0.0;
    m_createSubmittedCount = 0;
    m_createSucceededCount = 0;
    m_updateSubmittedCount = 0;
    m_destroySubmittedCount = 0;
    m_destroySucceededCount = 0;
    m_invalidTransformCount = 0;
    m_remoteMovementAcceptedCount = 0;
    m_sameSpaceCount = 0;

    const auto diagnostics = SkyrimTogetherVR::GameplayBridgeClient::GetDiagnostics();
    m_rejectedCommandBaseline = diagnostics.RejectedCommandCount + diagnostics.RejectedSubmissionCount;
}

void VRAvatarService::WriteStatus() noexcept
{
    try
    {
        std::error_code ec;
        std::filesystem::create_directories(m_statusPath.parent_path(), ec);
        std::ofstream file(m_statusPath, std::ios::trunc);
        if (!file)
            return;

        const auto diagnostics = SkyrimTogetherVR::GameplayBridgeClient::GetDiagnostics();
        const auto rejectedCommandTotal = diagnostics.RejectedCommandCount + diagnostics.RejectedSubmissionCount;
        const auto rejectedCommandCount = rejectedCommandTotal >= m_rejectedCommandBaseline
            ? rejectedCommandTotal - m_rejectedCommandBaseline
            : rejectedCommandTotal;
        std::size_t activeAvatarCount = 0;
        for (const auto& [serverId, avatar] : m_remoteAvatars)
        {
            TP_UNUSED(serverId);
            if (avatar.Handle.Value != 0 && !avatar.DestroyPending)
                ++activeAvatarCount;
        }

        file << "schema=commonlib_bridge_v1\n";
        file << "ready=1\n";
        file << "connected=" << (m_connected ? 1 : 0) << "\n";
        file << "bridgeReady=" << (diagnostics.Ready ? 1 : 0) << "\n";
        file << "actorTargetsEnabled=" << (HasAvatarCapabilities() ? 1 : 0) << "\n";
        file << "actorSkeletonWritesEnabled=0\n";
        file << "visualPolicy=player_template_fallback\n";
        file << "cleanupRequired=" << (m_transport.IsGameplayCleanupRequired() ? 1 : 0) << "\n";
        file << "lifecycleEpoch=" << diagnostics.LifecycleEpoch << "\n";
        file << "localSnapshotReady=" << (HasValidLocalSnapshot() ? 1 : 0) << "\n";
        file << "localServerAssigned=" << (m_localServerId ? 1 : 0) << "\n";
        file << "localServerId=" << m_localServerId.value_or(0) << "\n";
        file << "trackedAvatarCount=" << m_remoteAvatars.size() << "\n";
        file << "activeAvatarCount=" << activeAvatarCount << "\n";
        file << "createSubmittedCount=" << m_createSubmittedCount << "\n";
        file << "createSucceededCount=" << m_createSucceededCount << "\n";
        file << "updateSubmittedCount=" << m_updateSubmittedCount << "\n";
        file << "destroySubmittedCount=" << m_destroySubmittedCount << "\n";
        file << "destroySucceededCount=" << m_destroySucceededCount << "\n";
        file << "rejectedCommandCount=" << rejectedCommandCount << "\n";
        file << "invalidTransformCount=" << m_invalidTransformCount << "\n";
        file << "remoteMovementAcceptedCount=" << m_remoteMovementAcceptedCount << "\n";
        file << "sameSpaceCount=" << m_sameSpaceCount << "\n";
    }
    catch (...)
    {
    }
}

bool VRAvatarService::HasValidLocalSnapshot() const noexcept
{
    return m_hasLocalSnapshot && m_localSnapshot.LocalCellFormId != 0 && m_localSnapshot.LocalActorBaseFormId != 0 &&
           IsFinite(m_localSnapshot.Root) && m_localSnapshot.Root.Scale > 0.0f;
}

bool VRAvatarService::HasAvatarCapabilities() const noexcept
{
    const auto networkCapabilities = m_transport.GetNegotiatedGameplayCapabilities();
    const auto bridgeCapabilities = SkyrimTogetherVR::GameplayBridgeClient::GetActiveCapabilities();
    return TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS != 0 &&
           !m_transport.IsGameplayCleanupRequired() &&
           SkyrimTogether::Protocol::HasCapability(networkCapabilities, SkyrimTogether::Protocol::GameplayCapability::RemoteAvatarLifecycle) &&
           SkyrimTogether::Protocol::HasCapability(networkCapabilities, SkyrimTogether::Protocol::GameplayCapability::RemoteRootTransform) &&
           SkyrimTogetherVR::GameplayBridgeClient::IsReady() &&
           GameplayBridge::HasCapability(bridgeCapabilities, GameplayBridge::Capability::RemoteAvatarLifecycle) &&
           GameplayBridge::HasCapability(bridgeCapabilities, GameplayBridge::Capability::RemoteRootTransform);
}

bool VRAvatarService::CanSubmitAvatarCommands() noexcept
{
    if (HasAvatarCapabilities())
        return true;

    if (!m_capabilityWarningLogged)
    {
        m_capabilityWarningLogged = true;
        spdlog::warn("VR avatar service is inert until actor targets, negotiated remote-avatar capabilities, and the gameplay bridge are active");
    }
    return false;
}

bool VRAvatarService::BuildLocalLocation(GameId& arCellId, GameId& arWorldspaceId) const noexcept
{
    if (!HasValidLocalSnapshot() || !m_world.GetModSystem().GetServerModId(m_localSnapshot.LocalCellFormId, arCellId))
        return false;

    if (m_localSnapshot.LocalWorldspaceFormId != 0 &&
        !m_world.GetModSystem().GetServerModId(m_localSnapshot.LocalWorldspaceFormId, arWorldspaceId))
        return false;
    return true;
}

bool VRAvatarService::BuildCommand(const GameplayBridge::CommandKind aKind, const std::uint32_t aServerId, GameplayBridge::CommandRecord& arCommand) const noexcept
{
    const auto serverNonce = m_transport.GetServerInstanceNonce();
    const auto connectionGeneration = m_transport.GetConnectionGeneration();
    const auto lifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    SkyrimTogetherVR::CanonicalEntity::BridgeIdentity entityIdentity{};
    if (serverNonce == 0 || connectionGeneration == 0 || lifecycleEpoch == 0 ||
        !SkyrimTogetherVR::CanonicalEntity::TrySplitServerId(aServerId, entityIdentity))
        return false;

    arCommand.Header.Kind = static_cast<std::uint16_t>(aKind);
    arCommand.Header.PayloadSize = GameplayBridge::kFixedPayloadBytes;
    arCommand.Header.Identity.ServerInstanceNonce = serverNonce;
    arCommand.Header.Identity.ConnectionGeneration = connectionGeneration;
    arCommand.Header.Identity.LifecycleEpoch = lifecycleEpoch;
    arCommand.Header.Identity.EntityId = entityIdentity.EntityId;
    arCommand.Header.Identity.EntityGeneration = entityIdentity.EntityGeneration;
    return true;
}
