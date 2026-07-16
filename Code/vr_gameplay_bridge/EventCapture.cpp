#include "EventCapture.h"

#include "AvatarManager.h"
#include "HumanoidAnimationGraph.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <vector>

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
std::uint64_t g_animationSnapshotId{};
std::chrono::steady_clock::time_point g_lastAnimationSnapshotTime{};
constexpr auto kAnimationSnapshotInterval = std::chrono::milliseconds(100);

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

void PopulateAnimationChunk(
    EventRecord& a_record,
    const std::uint64_t a_snapshotId,
    const AnimationGraphProtocol::ValueType a_type,
    const std::uint16_t a_startIndex,
    const std::uint16_t a_valueCount,
    const float a_direction) noexcept
{
    PopulateHeader(a_record, EventKind::LocalAnimationGraphChunk);
    auto& payload = a_record.Payload.LocalAnimationGraphChunk;
    payload.AvatarHandle = kLocalPlayerHandle;
    payload.SnapshotId = a_snapshotId;
    payload.DescriptorVersion = AnimationGraphProtocol::kDescriptorVersion;
    payload.ValueType = static_cast<std::uint16_t>(a_type);
    payload.StartIndex = a_startIndex;
    payload.ValueCount = a_valueCount;
    payload.TotalCount = AnimationGraphProtocol::ExpectedCount(a_type);
    payload.ChunkFlags = AnimationGraphProtocol::FullSnapshot;
    payload.Direction = a_direction;
}

void PublishCurrentLocalAnimationStateImpl() noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational() ||
        !HasCapability(endpoint.Mapping()->Header.ActiveCapabilities.load(std::memory_order_acquire),
                       Capability::LocalAnimationGraphSnapshot))
        return;

    const auto now = std::chrono::steady_clock::now();
    if (g_lastAnimationSnapshotTime.time_since_epoch().count() != 0 && now - g_lastAnimationSnapshotTime < kAnimationSnapshotInterval)
        return;
    g_lastAnimationSnapshotTime = now;

    auto* player = RE::PlayerCharacter::GetSingleton();
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if (!player || !player->GetAnimationGraphManager(manager) || !manager)
        return;

    std::array<bool, AnimationGraphProtocol::kBooleanCount> booleans{};
    std::array<float, AnimationGraphProtocol::kFloatCount> floats{};
    std::array<std::int32_t, AnimationGraphProtocol::kIntegerCount> integers{};
    bool captured = true;
    for (std::size_t index = 0; index < booleans.size(); ++index)
        captured = player->GetGraphVariableBool(RE::BSFixedString(HumanoidAnimationGraph::kBooleanNames[index].data()), booleans[index]) && captured;
    for (std::size_t index = 0; index < floats.size(); ++index)
        captured = player->GetGraphVariableFloat(RE::BSFixedString(HumanoidAnimationGraph::kFloatNames[index].data()), floats[index]) && captured;
    for (std::size_t index = 0; index < integers.size(); ++index)
        captured = player->GetGraphVariableInt(RE::BSFixedString(HumanoidAnimationGraph::kIntegerNames[index].data()), integers[index]) && captured;
    if (!captured)
        return;

    const auto snapshotId = ++g_animationSnapshotId;
    constexpr auto graphRecordCount = 1u +
        (AnimationGraphProtocol::kFloatCount + AnimationGraphProtocol::kValuesPerChunk - 1) /
            AnimationGraphProtocol::kValuesPerChunk +
        (AnimationGraphProtocol::kIntegerCount + AnimationGraphProtocol::kValuesPerChunk - 1) /
            AnimationGraphProtocol::kValuesPerChunk;
    std::vector<EventRecord> records;
    records.reserve(graphRecordCount);
    auto& booleanChunk = records.emplace_back();
    PopulateAnimationChunk(booleanChunk, snapshotId, AnimationGraphProtocol::ValueType::BooleanBits, 0,
                           AnimationGraphProtocol::kBooleanCount, floats[1]);
    for (std::size_t index = 0; index < booleans.size(); ++index) {
        if (booleans[index])
            booleanChunk.Payload.LocalAnimationGraphChunk.Values[index / 32] |= 1u << (index % 32);
    }
    for (std::uint16_t start = 0; start < floats.size(); start += AnimationGraphProtocol::kValuesPerChunk) {
        const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(AnimationGraphProtocol::kValuesPerChunk, floats.size() - start));
        auto& chunk = records.emplace_back();
        PopulateAnimationChunk(chunk, snapshotId, AnimationGraphProtocol::ValueType::Float, start, count, floats[1]);
        for (std::uint16_t index = 0; index < count; ++index)
            chunk.Payload.LocalAnimationGraphChunk.Values[index] = std::bit_cast<std::uint32_t>(floats[start + index]);
    }
    for (std::uint16_t start = 0; start < integers.size(); start += AnimationGraphProtocol::kValuesPerChunk) {
        const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(AnimationGraphProtocol::kValuesPerChunk, integers.size() - start));
        auto& chunk = records.emplace_back();
        PopulateAnimationChunk(chunk, snapshotId, AnimationGraphProtocol::ValueType::Integer, start, count, floats[1]);
        for (std::uint16_t index = 0; index < count; ++index)
            chunk.Payload.LocalAnimationGraphChunk.Values[index] = std::bit_cast<std::uint32_t>(integers[start + index]);
    }
    TP_UNUSED(endpoint.TryPushEvents(records.data(), records.size()));
}
} // namespace

void PublishCurrentLocalPlayerState() noexcept
{
    PublishCurrentLocalPlayerStateImpl();
}

void PublishCurrentLocalAnimationState() noexcept
{
    PublishCurrentLocalAnimationStateImpl();
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
    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational())
        return false;

    LifecycleState state{};
    std::uint32_t reason{};
    bool transitionPending = pending != 0;
    if (transitionPending) {
        state = (pending & kPendingPreLoadGame) != 0 ? LifecycleState::PreLoadGame : LifecycleState::NewGame;
    } else {
        const auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* cell = player ? player->GetParentCell() : nullptr;
        const auto* worldspace = player ? player->GetWorldspace() : nullptr;
        const auto cellFormId = cell ? cell->GetFormID() : 0;
        const auto worldspaceFormId = worldspace ? worldspace->GetFormID() : 0;
        transitionPending = g_lastLocalCellFormId != 0 &&
                            (cellFormId == 0 || cellFormId != g_lastLocalCellFormId ||
                             worldspaceFormId != g_lastLocalWorldspaceFormId);
        g_lastLocalCellFormId = cellFormId;
        g_lastLocalWorldspaceFormId = worldspaceFormId;
        if (transitionPending) {
            state = LifecycleState::CellChanged;
            reason = cellFormId;
        }
    }

    if (!transitionPending)
        return false;

    AvatarManager::Get().RetireAllOnCommandPumpOwner();
    g_lastLocalCellFormId = 0;
    g_lastLocalWorldspaceFormId = 0;
    g_lastAnimationSnapshotTime = {};
    endpoint.Mapping()->Header.LifecycleEpoch.fetch_add(1, std::memory_order_acq_rel);
    PublishLifecycle(state, reason);
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
    const std::uint32_t a_localActorReferenceFormId,
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
    payload.LocalActorReferenceFormId = a_localActorReferenceFormId;
    payload.Root = EventSafeRoot(a_root);
    endpoint.TryPushEvent(record);
}

void PublishRemoteAnimationGraphState(
    const BridgeIdentity& a_identity,
    const AdapterHandle a_avatarHandle,
    const std::uint64_t a_snapshotId,
    const RemoteAnimationGraphState a_state,
    const CommandStatus a_status) noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational())
        return;

    EventRecord record{};
    record.Header.Kind = static_cast<std::uint16_t>(EventKind::RemoteAnimationGraphState);
    record.Header.PayloadSize = kFixedPayloadBytes;
    record.Header.Identity = a_identity;
    auto& payload = record.Payload.RemoteAnimationGraphState;
    payload.AvatarHandle = a_avatarHandle;
    payload.SnapshotId = a_snapshotId;
    payload.State = static_cast<std::uint32_t>(a_state);
    payload.Status = static_cast<std::uint32_t>(a_status);
    endpoint.TryPushEvent(record);
}

void PublishRemoteSpatialTransferState(
    const BridgeIdentity& a_identity,
    const AdapterHandle a_avatarHandle,
    const std::uint32_t a_sourceCellFormId,
    const std::uint32_t a_sourceWorldspaceFormId,
    const std::uint32_t a_targetCellFormId,
    const std::uint32_t a_targetWorldspaceFormId,
    const CommandStatus a_status) noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational())
        return;
    EventRecord record{};
    record.Header.Kind = static_cast<std::uint16_t>(EventKind::RemoteSpatialTransferState);
    record.Header.PayloadSize = kFixedPayloadBytes;
    record.Header.Identity = a_identity;
    auto& payload = record.Payload.RemoteSpatialTransferState;
    payload.AvatarHandle = a_avatarHandle;
    payload.SourceCellFormId = a_sourceCellFormId;
    payload.SourceWorldspaceFormId = a_sourceWorldspaceFormId;
    payload.TargetCellFormId = a_targetCellFormId;
    payload.TargetWorldspaceFormId = a_targetWorldspaceFormId;
    payload.Status = static_cast<std::uint32_t>(a_status);
    endpoint.TryPushEvent(record);
}

void PublishRemoteGameplayActionState(
    const BridgeIdentity& a_identity,
    const AdapterHandle a_targetHandle,
    const GameplayDomain a_domain,
    const GameplayAction a_action,
    const std::uint32_t a_targetLocalFormId,
    const CommandStatus a_status) noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational())
        return;
    EventRecord record{};
    record.Header.Kind = static_cast<std::uint16_t>(EventKind::RemoteGameplayActionState);
    record.Header.PayloadSize = kFixedPayloadBytes;
    record.Header.Identity = a_identity;
    auto& payload = record.Payload.RemoteGameplayActionState;
    payload.TargetHandle = a_targetHandle;
    payload.Domain = static_cast<std::uint16_t>(a_domain);
    payload.Action = static_cast<std::uint16_t>(a_action);
    payload.Status = static_cast<std::uint32_t>(a_status);
    payload.TargetLocalFormId = a_targetLocalFormId;
    endpoint.TryPushEvent(record);
}
} // namespace SkyrimTogetherVR::GameplayAdapter
