#include "EventCapture.h"

#include "AvatarManager.h"

#include <cmath>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr std::uint64_t kSnapshotHasPlayer = 1ull << 0;
constexpr std::uint64_t kSnapshotHasCell = 1ull << 1;
constexpr std::uint64_t kSnapshotHasWorldspace = 1ull << 2;
constexpr std::uint32_t kPendingNewGame = 1u << 0;
constexpr std::uint32_t kPendingPreLoadGame = 1u << 1;

std::atomic<std::uint32_t> g_pendingLifecycleTransitions{};
std::uint32_t g_lastLocalCellFormId{};
std::uint32_t g_lastLocalWorldspaceFormId{};

void PopulateHeader(EventRecord& a_record, const EventKind a_kind) noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    a_record.Header.Kind = static_cast<std::uint16_t>(a_kind);
    a_record.Header.PayloadSize = kFixedPayloadBytes;
    a_record.Header.Flags = 0;
    a_record.Header.Identity = endpoint.SnapshotIdentity(endpoint.NextEventSequence());
}

[[nodiscard]] RootTransform EventSafeRoot(const RootTransform& a_root) noexcept
{
    if (std::isfinite(a_root.PositionX) && std::isfinite(a_root.PositionY) && std::isfinite(a_root.PositionZ) &&
        std::isfinite(a_root.RotationX) && std::isfinite(a_root.RotationY) && std::isfinite(a_root.RotationZ) &&
        std::isfinite(a_root.RotationW) && std::isfinite(a_root.Scale))
        return a_root;
    return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
}

void PublishLifecycle(const LifecycleState a_state, const std::uint32_t a_reason) noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational())
        return;

    EventRecord record{};
    PopulateHeader(record, EventKind::Lifecycle);
    record.Payload.Lifecycle.ObservedState = static_cast<std::uint32_t>(a_state);
    record.Payload.Lifecycle.Reason = a_reason;
    record.Payload.Lifecycle.ObservedLifecycleEpoch = record.Header.Identity.LifecycleEpoch;
    record.Payload.Lifecycle.AvailableCapabilities = endpoint.Mapping()->Header.AvailableCapabilities.load(std::memory_order_acquire);
    endpoint.TryPushEvent(record);
}

void PublishCurrentLocalPlayerStateImpl() noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational())
        return;
    const auto requiredCapabilities = static_cast<CapabilityMask>(Capability::LocalPlayerDiscovery) |
                                      static_cast<CapabilityMask>(Capability::LocalPlayerSnapshot);
    if ((endpoint.Mapping()->Header.ActiveCapabilities.load(std::memory_order_acquire) & requiredCapabilities) != requiredCapabilities)
        return;

    auto* player = RE::PlayerCharacter::GetSingleton();
    const auto* cell = player ? player->GetParentCell() : nullptr;
    const auto* worldspace = player ? player->GetWorldspace() : nullptr;
    const auto cellFormId = cell ? cell->GetFormID() : 0;
    const auto worldspaceFormId = worldspace ? worldspace->GetFormID() : 0;
    if (cellFormId != 0 && g_lastLocalCellFormId != 0 &&
        (cellFormId != g_lastLocalCellFormId || worldspaceFormId != g_lastLocalWorldspaceFormId)) {
        AvatarManager::Get().RetireAllOnCommandPumpOwner();
        endpoint.Mapping()->Header.LifecycleEpoch.fetch_add(1, std::memory_order_acq_rel);
        PublishLifecycle(LifecycleState::CellChanged, cellFormId);
    }
    g_lastLocalCellFormId = cellFormId;
    g_lastLocalWorldspaceFormId = worldspaceFormId;

    EventRecord record{};
    PopulateHeader(record, EventKind::LocalPlayerState);

    if (player) {
        record.Payload.LocalPlayerState.LocalPlayerHandle = kLocalPlayerHandle;
        record.Payload.LocalPlayerState.SnapshotFlags |= kSnapshotHasPlayer;
        const auto position = player->GetPosition();
        const auto angle = player->GetAngle();
        const auto x = angle.x * 0.5f;
        const auto y = angle.y * 0.5f;
        const auto z = angle.z * 0.5f;
        const auto cx = std::cos(x);
        const auto sx = std::sin(x);
        const auto cy = std::cos(y);
        const auto sy = std::sin(y);
        const auto cz = std::cos(z);
        const auto sz = std::sin(z);

        auto& root = record.Payload.LocalPlayerState.Root;
        root.PositionX = position.x;
        root.PositionY = position.y;
        root.PositionZ = position.z;
        root.RotationX = sx * cy * cz - cx * sy * sz;
        root.RotationY = cx * sy * cz + sx * cy * sz;
        root.RotationZ = cx * cy * sz - sx * sy * cz;
        root.RotationW = cx * cy * cz + sx * sy * sz;
        root.Scale = player->GetScale();

        if (cell) {
            record.Payload.LocalPlayerState.LocalCellFormId = cell->GetFormID();
            record.Payload.LocalPlayerState.SnapshotFlags |= kSnapshotHasCell;
        }
        if (worldspace) {
            record.Payload.LocalPlayerState.LocalWorldspaceFormId = worldspace->GetFormID();
            record.Payload.LocalPlayerState.SnapshotFlags |= kSnapshotHasWorldspace;
        }
        if (const auto* actorBase = player->GetActorBase())
            record.Payload.LocalPlayerState.LocalActorBaseFormId = actorBase->GetFormID();
    }

    endpoint.TryPushEvent(record);
}
} // namespace

void PublishCurrentLocalPlayerState() noexcept
{
    PublishCurrentLocalPlayerStateImpl();
}

void HandleSkseMessage(SKSE::MessagingInterface::Message* a_message) noexcept
{
    if (!a_message)
        return;

    try {
        auto& endpoint = BridgeEndpoint::Get();
        if (!endpoint.IsOperational())
            return;
        switch (a_message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            PublishLifecycle(LifecycleState::DataLoaded, 0);
            break;
        case SKSE::MessagingInterface::kNewGame:
            g_pendingLifecycleTransitions.fetch_or(kPendingNewGame, std::memory_order_release);
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            g_pendingLifecycleTransitions.fetch_or(kPendingPreLoadGame, std::memory_order_release);
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            PublishLifecycle(LifecycleState::PostLoadGame, 0);
            break;
        default:
            break;
        }
    } catch (...) {
        BridgeEndpoint::Get().Fault("exception while handling an SKSE lifecycle message");
    }
}

bool ProcessPendingLifecycleTransitions() noexcept
{
    const auto pending = g_pendingLifecycleTransitions.exchange(0, std::memory_order_acq_rel);
    if (pending == 0)
        return false;

    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational())
        return false;

    AvatarManager::Get().RetireAllOnCommandPumpOwner();
    g_lastLocalCellFormId = 0;
    g_lastLocalWorldspaceFormId = 0;
    endpoint.Mapping()->Header.LifecycleEpoch.fetch_add(1, std::memory_order_acq_rel);
    const auto state = (pending & kPendingPreLoadGame) != 0 ? LifecycleState::PreLoadGame : LifecycleState::NewGame;
    PublishLifecycle(state, 0);
    return true;
}

void PublishPluginLoaded() noexcept
{
    PublishLifecycle(LifecycleState::PluginLoaded, 0);
}

void PublishEpochRetired(const std::uint32_t a_reason) noexcept
{
    PublishLifecycle(LifecycleState::EpochRetired, a_reason);
}

void PublishRemoteAvatarState(
    const BridgeIdentity& a_identity,
    const AdapterHandle a_avatarHandle,
    const RemoteAvatarState a_state,
    const CommandStatus a_status,
    const std::uint32_t a_localCellFormId,
    const std::uint32_t a_localWorldspaceFormId,
    const RootTransform& a_root) noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational())
        return;

    EventRecord record{};
    record.Header.Kind = static_cast<std::uint16_t>(EventKind::RemoteAvatarState);
    record.Header.PayloadSize = kFixedPayloadBytes;
    record.Header.Flags = 0;
    record.Header.Identity = a_identity;
    auto& payload = record.Payload.RemoteAvatarState;
    payload.AvatarHandle = a_avatarHandle;
    payload.State = static_cast<std::uint32_t>(a_state);
    payload.Status = static_cast<std::uint32_t>(a_status);
    payload.LocalCellFormId = a_localCellFormId;
    payload.LocalWorldspaceFormId = a_localWorldspaceFormId;
    payload.Root = EventSafeRoot(a_root);
    endpoint.TryPushEvent(record);
}
} // namespace SkyrimTogetherVR::GameplayAdapter
