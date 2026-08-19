#include "ActorActionHooks.h"

#include "AvatarManager.h"
#include "AnimationGraphDescriptors.h"
#include "VerifiedVrActorAction.h"
#include "VrHookDetachPolicy.h"

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <bitset>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace SkyrimTogetherVR::GameplayAdapter::ActorActionHooks
{
namespace
{
constexpr std::size_t kMaximumPendingActions = 64;
constexpr std::size_t kMaximumActionStringBytes = 127;
constexpr std::size_t kMaximumActionTextBytes = kMaximumActionStringBytes * 2 + 1;
constexpr std::size_t kMaximumActionGraphChunks =
    1 + (AnimationGraphProtocol::kMaximumFloatCount + AnimationGraphProtocol::kValuesPerChunk - 1) /
            AnimationGraphProtocol::kValuesPerChunk +
    (AnimationGraphProtocol::kMaximumIntegerCount + AnimationGraphProtocol::kValuesPerChunk - 1) /
            AnimationGraphProtocol::kValuesPerChunk;
constexpr std::size_t kMaximumActionEventRecords =
    kMaximumActionGraphChunks + kMaximumGameplayTextChunks + 1;

VerifiedVrActorAction::PerformAction g_originalPerformAction{};
void* g_performActionTarget{};
std::atomic<bool> g_installing{};
std::atomic<bool> g_installed{};
VrHookDetachPolicy::HookState g_hookState{};
std::atomic<std::uint64_t> g_nextActionId{};
thread_local std::uint32_t g_remoteActionDepth{};

struct PendingAction
{
    AdapterHandle TargetHandle{};
    AnimationGraphProtocol::SnapshotBuffer Graph{};
    std::array<std::array<char, kGameplayTextBytesPerChunk>, kMaximumGameplayTextChunks> TextChunks{};
    std::array<std::uint16_t, kMaximumGameplayTextChunks> TextLengths{};
    std::bitset<kMaximumGameplayTextChunks> TextReceived{};
    std::uint16_t TextChunkCount{};
    std::uint64_t TextId{};
};

struct PendingActionSlot
{
    std::uint64_t TransactionId{};
    PendingAction Action{};
};

std::array<PendingActionSlot, kMaximumPendingActions> g_pendingActions{};

[[nodiscard]] VrHookDetachPolicy::OperationResult DisablePerformActionHook(void*) noexcept
{
    const auto status = MH_DisableHook(g_performActionTarget);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_DISABLED)
        return VrHookDetachPolicy::OperationResult::AlreadyDisabled;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    SKSE::log::error("SkyrimTogetherVRGameplayBridge: ActorMediator::PerformAction hook disable failed ({})",
                     static_cast<int>(status));
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult RemovePerformActionHook(void*) noexcept
{
    const auto status = MH_RemoveHook(g_performActionTarget);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    SKSE::log::error("SkyrimTogetherVRGameplayBridge: ActorMediator::PerformAction hook remove failed ({})",
                     static_cast<int>(status));
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] bool DetachPerformActionHook() noexcept
{
    return VrHookDetachPolicy::Detach(
        g_hookState, {DisablePerformActionHook, RemovePerformActionHook, nullptr});
}

void ForgetDetachedPerformActionHook() noexcept
{
    g_performActionTarget = nullptr;
    g_originalPerformAction = nullptr;
    g_hookState = {};
}

void LogRetainedPerformActionHook(const char* a_operation) noexcept
{
    BridgeEndpoint::Get().Fault("ActorMediator::PerformAction hook rollback could not prove detachment");
    SKSE::log::error(
        "SkyrimTogetherVRGameplayBridge: ActorMediator::PerformAction {} could not prove detachment; retaining "
        "target, trampoline, and optional capability so a possible live detour remains callable",
        a_operation);
}

struct LocalActionCapture
{
    AdapterHandle Handle{};
    RE::FormID ActorFormId{};
    RE::FormID ActionFormId{};
    RE::FormID TargetFormId{};
    std::uint32_t State1{};
    std::uint32_t State2{};
    std::uint32_t Type{};
    AnimationGraphProtocol::SnapshotBuffer Graph{};
    bool Valid{};
};

[[nodiscard]] PendingAction* FindPending(const std::uint64_t a_transactionId) noexcept
{
    if (a_transactionId == 0)
        return nullptr;
    for (auto& pending : g_pendingActions) {
        if (pending.TransactionId == a_transactionId)
            return &pending.Action;
    }
    return nullptr;
}

[[nodiscard]] PendingAction& GetOrCreatePending(const std::uint64_t a_transactionId) noexcept
{
    if (auto* existing = FindPending(a_transactionId))
        return *existing;

    auto slot = std::find_if(g_pendingActions.begin(), g_pendingActions.end(),
                             [](const auto& a_pending) { return a_pending.TransactionId == 0; });
    if (slot == g_pendingActions.end()) {
        slot = std::min_element(g_pendingActions.begin(), g_pendingActions.end(),
                                [](const auto& a_left, const auto& a_right) {
                                    return a_left.TransactionId < a_right.TransactionId;
                                });
    }
    *slot = {};
    slot->TransactionId = a_transactionId;
    return slot->Action;
}

void DiscardPending(const std::uint64_t a_transactionId) noexcept
{
    for (auto& pending : g_pendingActions) {
        if (pending.TransactionId == a_transactionId) {
            pending = {};
            return;
        }
    }
}

void ClearPending() noexcept
{
    for (auto& pending : g_pendingActions)
        pending = {};
}

[[nodiscard]] bool IsZero(const std::uint8_t* a_bytes, const std::size_t a_size) noexcept
{
    for (std::size_t index = 0; index < a_size; ++index) {
        if (a_bytes[index] != 0)
            return false;
    }
    return true;
}

[[nodiscard]] bool IsValidFormId(const RE::FormID a_formId) noexcept
{
    return a_formId != 0 && a_formId != std::numeric_limits<RE::FormID>::max() &&
           RE::TESForm::LookupByID(a_formId) != nullptr;
}


[[nodiscard]] std::uint64_t NextActionId() noexcept
{
    auto actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    if (actionId == 0)
        actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    return actionId;
}

void PopulateHeader(EventRecord& a_record, const EventKind a_kind, const std::uint64_t a_actionId) noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    a_record.Header.Kind = static_cast<std::uint16_t>(a_kind);
    a_record.Header.PayloadSize = kFixedPayloadBytes;
    a_record.Header.Identity = endpoint.SnapshotIdentity(0);
    a_record.Header.Identity.ActionId = a_actionId;
}


class ScopedRemoteAction final
{
public:
    ScopedRemoteAction() noexcept { ++g_remoteActionDepth; }
    ~ScopedRemoteAction() { --g_remoteActionDepth; }

    ScopedRemoteAction(const ScopedRemoteAction&) = delete;
    ScopedRemoteAction& operator=(const ScopedRemoteAction&) = delete;
};

void PopulateGraphChunk(
    EventRecord& a_record,
    const AdapterHandle a_handle,
    const std::uint32_t a_actorFormId,
    const std::uint64_t a_actionId,
    const AnimationGraphProtocol::ValueType a_type,
    const std::uint16_t a_start,
    const std::uint16_t a_count, const std::uint16_t a_totalCount, const float a_direction) noexcept
{
    PopulateHeader(a_record, EventKind::LocalActorActionGraphChunk, a_actionId);
    auto& payload = a_record.Payload.LocalActorActionGraphChunk;
    payload.TargetHandle = a_handle;
    payload.ActorLocalFormId = a_actorFormId;
    payload.SnapshotId = a_actionId;
    payload.DescriptorVersion = AnimationGraphProtocol::kDescriptorVersion;
    payload.ValueType = static_cast<std::uint16_t>(a_type);
    payload.StartIndex = a_start;
    payload.ValueCount = a_count;
    payload.TotalCount = a_totalCount;
    payload.ChunkFlags = AnimationGraphProtocol::FullSnapshot;
    payload.Direction = a_direction;
}

[[nodiscard]] bool AppendGraphRecords(
    std::array<EventRecord, kMaximumActionEventRecords>& ar_records,
    std::size_t& ar_count,
    const AdapterHandle a_handle,
    const std::uint32_t a_actorFormId,
    const std::uint64_t a_actionId,
    const AnimationGraphProtocol::SnapshotBuffer& a_snapshot) noexcept
{
    if (ar_records.size() - ar_count < kMaximumActionGraphChunks)
        return false;

    auto& booleanChunk = ar_records[ar_count++];
    PopulateGraphChunk(booleanChunk, a_handle, a_actorFormId, a_actionId,
                       AnimationGraphProtocol::ValueType::BooleanBits, 0,
                       a_snapshot.BooleanCount, a_snapshot.BooleanCount, a_snapshot.Direction);
    for (std::size_t index = 0; index < a_snapshot.BooleanCount; ++index) {
        if (a_snapshot.Booleans[index])
            booleanChunk.Payload.LocalActorActionGraphChunk.Values[index / 32] |= 1u << (index % 32);
    }

    for (std::uint16_t start = 0; start < a_snapshot.FloatCount; start += AnimationGraphProtocol::kValuesPerChunk) {
        const auto count = static_cast<std::uint16_t>(
            std::min<std::uint16_t>(AnimationGraphProtocol::kValuesPerChunk, a_snapshot.FloatCount - start));
        auto& chunk = ar_records[ar_count++];
        PopulateGraphChunk(chunk, a_handle, a_actorFormId, a_actionId,
                           AnimationGraphProtocol::ValueType::Float, start, count, a_snapshot.FloatCount, a_snapshot.Direction);
        for (std::uint16_t index = 0; index < count; ++index)
            chunk.Payload.LocalActorActionGraphChunk.Values[index] = std::bit_cast<std::uint32_t>(a_snapshot.Floats[start + index]);
    }
    for (std::uint16_t start = 0; start < a_snapshot.IntegerCount; start += AnimationGraphProtocol::kValuesPerChunk) {
        const auto count = static_cast<std::uint16_t>(
            std::min<std::uint16_t>(AnimationGraphProtocol::kValuesPerChunk, a_snapshot.IntegerCount - start));
        auto& chunk = ar_records[ar_count++];
        PopulateGraphChunk(chunk, a_handle, a_actorFormId, a_actionId,
                           AnimationGraphProtocol::ValueType::Integer, start, count, a_snapshot.IntegerCount, a_snapshot.Direction);
        for (std::uint16_t index = 0; index < count; ++index)
            chunk.Payload.LocalActorActionGraphChunk.Values[index] = std::bit_cast<std::uint32_t>(a_snapshot.Integers[start + index]);
    }
    return true;
}

[[nodiscard]] bool AppendTextRecords(
    std::array<EventRecord, kMaximumActionEventRecords>& ar_records,
    std::size_t& ar_count,
    const AdapterHandle a_handle,
    const std::uint32_t a_actorFormId,
    const std::uint64_t a_actionId,
    const char* a_text,
    const std::size_t a_textSize) noexcept
{
    if (!a_text || a_textSize == 0 || a_textSize >
                              static_cast<std::size_t>(kGameplayTextBytesPerChunk) * kMaximumGameplayTextChunks)
        return false;
    const auto chunkCount = static_cast<std::uint16_t>(
        (a_textSize + kGameplayTextBytesPerChunk - 1) / kGameplayTextBytesPerChunk);
    if (ar_records.size() - ar_count < chunkCount)
        return false;
    for (std::uint16_t index = 0; index < chunkCount; ++index) {
        auto& record = ar_records[ar_count++];
        PopulateHeader(record, EventKind::LocalActorActionTextChunk, a_actionId);
        auto& payload = record.Payload.LocalActorActionTextChunk;
        payload.TargetHandle = a_handle;
        payload.TargetLocalFormId = a_actorFormId;
        payload.Domain = static_cast<std::uint16_t>(GameplayDomain::Animation);
        payload.Action = static_cast<std::uint16_t>(GameplayAction::ActorAction);
        payload.TextId = a_actionId;
        payload.ChunkIndex = index;
        payload.ChunkCount = chunkCount;
        const auto offset = static_cast<std::size_t>(index) * kGameplayTextBytesPerChunk;
        payload.ByteCount = static_cast<std::uint16_t>(
            std::min<std::size_t>(kGameplayTextBytesPerChunk, a_textSize - offset));
        std::memcpy(payload.Utf8Bytes, a_text + offset, payload.ByteCount);
    }
    return true;
}

[[nodiscard]] bool CaptureLocalAction(
    RE::Actor& a_actor,
    const RE::TESActionData& a_data,
    LocalActionCapture& ar_capture) noexcept
{
    ar_capture = {};
    const auto actorFormId = a_actor.GetFormID();
    const auto actionFormId = a_data.action ? a_data.action->GetFormID() : 0;
    const auto targetFormId = a_data.target ? a_data.target->GetFormID() : 0;
    if (!IsValidFormId(actorFormId) || !IsValidFormId(actionFormId) ||
        (targetFormId != 0 && !IsValidFormId(targetFormId)))
        return false;

    auto* actorState = a_actor.AsActorState();
    if (!actorState || !AnimationGraphs::Capture(a_actor, ar_capture.Graph))
        return false;

    ar_capture.Handle = &a_actor == RE::PlayerCharacter::GetSingleton() ? kLocalPlayerHandle : AdapterHandle{};
    ar_capture.ActorFormId = actorFormId;
    ar_capture.ActionFormId = actionFormId;
    ar_capture.TargetFormId = targetFormId;
    ar_capture.State1 = std::bit_cast<std::uint32_t>(actorState->actorState1);
    ar_capture.State2 = std::bit_cast<std::uint32_t>(actorState->actorState2);
    ar_capture.Type = static_cast<std::uint32_t>(a_data.priority.underlying()) |
                     ((a_data.flags & 1u) != 0 ? 0x4u : 0u);
    ar_capture.Valid = true;
    return true;
}

void PublishLocalAction(const LocalActionCapture& a_capture, const RE::TESActionData& a_data)
{
    auto& endpoint = BridgeEndpoint::Get();
    if (!a_capture.Valid || !endpoint.IsOperational() ||
        !HasCapability(endpoint.Mapping()->Header.ActiveCapabilities.load(std::memory_order_acquire),
                       Capability::ExactAnimationActions))
        return;

    const auto idleFormId = a_data.animObjIdle ? a_data.animObjIdle->GetFormID() : 0;
    if (idleFormId != 0 && !IsValidFormId(idleFormId))
        return;

    const std::string_view eventName{a_data.animEvent.c_str()};
    const std::string_view targetEventName{a_data.targetAnimEvent.c_str()};
    if (eventName.size() > kMaximumActionStringBytes || targetEventName.size() > kMaximumActionStringBytes)
        return;
    std::array<char, kMaximumActionTextBytes> text{};
    std::memcpy(text.data(), eventName.data(), eventName.size());
    text[eventName.size()] = '\0';
    std::memcpy(text.data() + eventName.size() + 1, targetEventName.data(), targetEventName.size());
    const auto textSize = eventName.size() + targetEventName.size() + 1;

    const auto actionId = NextActionId();
    std::array<EventRecord, kMaximumActionEventRecords> records{};
    std::size_t recordCount{};
    if (!AppendGraphRecords(records, recordCount, a_capture.Handle, a_capture.ActorFormId, actionId, a_capture.Graph) ||
        !AppendTextRecords(records, recordCount, a_capture.Handle, a_capture.ActorFormId, actionId,
                           text.data(), textSize))
        return;

    auto& metadata = records[recordCount++];
    PopulateHeader(metadata, EventKind::LocalActorActionMetadata, actionId);
    auto& payload = metadata.Payload.LocalActorActionMetadata;
    payload.TargetHandle = a_capture.Handle;
    payload.ActorLocalFormId = a_capture.ActorFormId;
    payload.ActionLocalFormId = a_capture.ActionFormId;
    payload.ActionTargetLocalFormId = a_capture.TargetFormId;
    payload.IdleLocalFormId = idleFormId;
    payload.State1 = a_capture.State1;
    payload.State2 = a_capture.State2;
    payload.Type = a_capture.Type;
    payload.SnapshotId = actionId;
    payload.TextId = actionId;
    if (!endpoint.TryPushEvents(records.data(), recordCount))
        return;
}

std::uint8_t HookPerformAction(void* a_mediator, RE::TESActionData* a_data) noexcept
{
    const auto original = g_originalPerformAction;
    if (!original)
        return 0;

    RE::Actor* actor{};
    bool managedRemote{};
    bool locallyOwned{};
    LocalActionCapture capture{};
    try {
        if (VerifiedVrActorAction::IsActorMediator(a_mediator) && VerifiedVrActorAction::IsTesActionData(a_data)) {
            actor = a_data->source->As<RE::Actor>();
            managedRemote = actor && AvatarManager::Get().IsManagedRemoteActor(actor);
            if (managedRemote && g_remoteActionDepth == 0)
                return 0;

            locallyOwned = actor && !managedRemote && g_remoteActionDepth == 0;
            if (locallyOwned) {
                static_cast<void>(CaptureLocalAction(*actor, *a_data, capture));
            }
        }
    } catch (...) {
        actor = nullptr;
        managedRemote = false;
        locallyOwned = false;
        capture = {};
    }

    // The hook must never invoke an engine action twice, including when local
    // validation or serialization throws.
    const auto result = original(a_mediator, a_data);
    if (!locallyOwned)
        return result;

    try {
        // Original Skyrim Together emits the event even when PerformAction
        // rejects it. Engine success only controlled latest-action bookkeeping.
        if (capture.Valid && a_data->flags != 1 && g_remoteActionDepth == 0)
            PublishLocalAction(capture, *a_data);
    } catch (...) {
        // Serialization failures must not change the engine result.
    }
    return result;
}

[[nodiscard]] CommandStatus StageGraph(const CommandRecord& a_command)
{
    const auto& payload = a_command.Payload.StageActorActionGraphChunk;
    if (payload.TargetHandle.Value == 0 || payload.ActorLocalFormId != 0 || payload.Reserved0 != 0 ||
        payload.SnapshotId == 0 || payload.Reserved1 != 0 ||
        payload.DescriptorVersion != AnimationGraphProtocol::kDescriptorVersion ||
        payload.ChunkFlags != AnimationGraphProtocol::FullSnapshot ||
        !IsZero(payload.ReservedTail, sizeof(payload.ReservedTail))) {
        if (payload.SnapshotId != 0)
            DiscardPending(payload.SnapshotId);
        return CommandStatus::Malformed;
    }
    const auto type = static_cast<AnimationGraphProtocol::ValueType>(payload.ValueType);
    if (!AnimationGraphProtocol::IsValidChunk(type, payload.StartIndex, payload.ValueCount, payload.TotalCount) ||
        !AnimationGraphProtocol::AreChunkValuesValid(type, payload.ValueCount, payload.TotalCount, payload.Values) ||
        !std::isfinite(payload.Direction)) {
        DiscardPending(payload.SnapshotId);
        return CommandStatus::Malformed;
    }
    auto& pending = GetOrCreatePending(payload.SnapshotId);
    if (pending.TargetHandle.Value == 0)
        pending.TargetHandle = payload.TargetHandle;
    if (pending.TargetHandle.Value != payload.TargetHandle.Value) {
        DiscardPending(payload.SnapshotId);
        return CommandStatus::Malformed;
    }
    const auto accepted = AnimationGraphProtocol::AcceptChunk(
        pending.Graph, payload.SnapshotId, type, payload.StartIndex, payload.ValueCount,
        payload.TotalCount, payload.Direction, payload.Values);
    if (accepted == AnimationGraphProtocol::ChunkAcceptResult::Malformed) {
        DiscardPending(payload.SnapshotId);
        return CommandStatus::Malformed;
    }
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus StageText(const CommandRecord& a_command)
{
    const auto& payload = a_command.Payload.StageActorActionTextChunk;
    if (payload.TargetHandle.Value == 0 || payload.TargetLocalFormId != 0 ||
        payload.Domain != static_cast<std::uint16_t>(GameplayDomain::Animation) ||
        payload.Action != static_cast<std::uint16_t>(GameplayAction::ActorAction) ||
        payload.TextId == 0 || payload.ChunkCount == 0 ||
        payload.ChunkCount > kMaximumGameplayTextChunks || payload.ChunkIndex >= payload.ChunkCount ||
        payload.ByteCount > kGameplayTextBytesPerChunk || payload.Reserved0 != 0 ||
        payload.AuxiliaryLocalFormId != 0 ||
        !IsZero(reinterpret_cast<const std::uint8_t*>(payload.Utf8Bytes + payload.ByteCount),
                kGameplayTextBytesPerChunk - payload.ByteCount)) {
        if (payload.TextId != 0)
            DiscardPending(payload.TextId);
        return CommandStatus::Malformed;
    }
    auto& pending = GetOrCreatePending(payload.TextId);
    if (pending.TargetHandle.Value == 0)
        pending.TargetHandle = payload.TargetHandle;
    if (pending.TargetHandle.Value != payload.TargetHandle.Value) {
        DiscardPending(payload.TextId);
        return CommandStatus::Malformed;
    }
    if (pending.TextChunkCount == 0) {
        pending.TextChunkCount = payload.ChunkCount;
        pending.TextId = payload.TextId;
    }
    if (pending.TextChunkCount != payload.ChunkCount || pending.TextId != payload.TextId ||
        pending.TextReceived.test(payload.ChunkIndex)) {
        DiscardPending(payload.TextId);
        return CommandStatus::Malformed;
    }
    std::copy_n(payload.Utf8Bytes, payload.ByteCount, pending.TextChunks[payload.ChunkIndex].begin());
    pending.TextLengths[payload.ChunkIndex] = payload.ByteCount;
    pending.TextReceived.set(payload.ChunkIndex);
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ApplyAction(const CommandRecord& a_command)
{
    if (!g_installed.load(std::memory_order_acquire) || !VerifiedVrActorAction::IsReady() ||
        !AvatarManager::Get().IsOnCommandPumpThread())
        return CommandStatus::Inactive;

    const auto payload = a_command.Payload.ApplyActorAction;
    if (payload.TargetHandle.Value == 0 || payload.ActorLocalFormId != 0 || payload.ActionLocalFormId == 0 ||
        payload.SnapshotId == 0 || payload.TextId != payload.SnapshotId ||
        payload.ActionFlags != 0 || (payload.Type & ~0x7u) != 0 || (payload.Type & 0x3u) == 0x3u ||
        !IsZero(payload.Reserved, sizeof(payload.Reserved))) {
        if (payload.SnapshotId != 0)
            DiscardPending(payload.SnapshotId);
        return CommandStatus::Malformed;
    }

    const auto mediator = VerifiedVrActorAction::GetActorMediator();
    if (!mediator)
        return CommandStatus::Inactive;

    const auto* staged = FindPending(payload.SnapshotId);
    if (!staged || !staged->Graph.IsComplete() || staged->TextChunkCount == 0 ||
        staged->TextReceived.count() != staged->TextChunkCount)
        return CommandStatus::Inactive;

    // The fixed slot is released before resolving game objects, just as the
    // desktop client removes the queued action after selecting it.
    const auto pending = *staged;
    DiscardPending(payload.SnapshotId);
    if (pending.TargetHandle.Value != payload.TargetHandle.Value)
        return CommandStatus::Malformed;

    std::array<char, kMaximumActionTextBytes + 1> text{};
    std::size_t textSize{};
    for (std::uint16_t index = 0; index < pending.TextChunkCount; ++index) {
        const auto length = pending.TextLengths[index];
        if (length > text.size() - 1 - textSize)
            return CommandStatus::Malformed;
        std::memcpy(text.data() + textSize, pending.TextChunks[index].data(), length);
        textSize += length;
    }

    std::size_t separator = textSize;
    for (std::size_t index = 0; index < textSize; ++index) {
        if (text[index] == '\0') {
            if (separator != textSize)
                return CommandStatus::Malformed;
            separator = index;
        }
    }
    if (separator == textSize || separator > kMaximumActionStringBytes ||
        textSize - separator - 1 > kMaximumActionStringBytes)
        return CommandStatus::Malformed;

    CommandRecord resolver{};
    resolver.Header = a_command.Header;
    resolver.Header.Kind = static_cast<std::uint16_t>(CommandKind::ApplyGameplayAction);
    resolver.Payload.ApplyGameplayAction.TargetHandle = payload.TargetHandle;
    resolver.Payload.ApplyGameplayAction.Domain = static_cast<std::uint16_t>(GameplayDomain::Animation);
    resolver.Payload.ApplyGameplayAction.Action = static_cast<std::uint16_t>(GameplayAction::ActorAction);
    RE::NiPointer<RE::Actor> actor;
    const auto resolved = AvatarManager::Get().ResolveGameplayActor(resolver, actor);
    if (resolved != CommandStatus::Success)
        return resolved;
    if (!actor)
        return CommandStatus::EngineRejected;

    auto* action = RE::TESForm::LookupByID<RE::BGSAction>(payload.ActionLocalFormId);
    auto* target = payload.ActionTargetLocalFormId != 0 ?
                       RE::TESForm::LookupByID<RE::TESObjectREFR>(payload.ActionTargetLocalFormId) : nullptr;
    auto* idle = payload.IdleLocalFormId != 0 ? RE::TESForm::LookupByID<RE::TESIdleForm>(payload.IdleLocalFormId) : nullptr;
    if (!action || (payload.ActionTargetLocalFormId != 0 && !target) || (payload.IdleLocalFormId != 0 && !idle))
        return CommandStatus::MissingForm;

    auto* actorState = actor->AsActorState();
    if (!actorState)
        return CommandStatus::EngineRejected;
    actorState->actorState1 = std::bit_cast<RE::ActorState::ActorState1>(payload.State1);
    actorState->actorState2 = std::bit_cast<RE::ActorState::ActorState2>(payload.State2);
    const auto graphResult = AvatarManager::Get().ApplyAnimationSnapshotToActor(*actor, pending.Graph);
    if (graphResult != CommandStatus::Success)
        return graphResult;

    VerifiedVrActorAction::ReplayActionData actionData;
    auto* data = actionData.Construct();
    if (!data)
        return CommandStatus::EngineRejected;
    data->source = RE::NiPointer<RE::TESObjectREFR>(actor.get());
    data->target = RE::NiPointer<RE::TESObjectREFR>(target);
    data->action = action;
    data->priority = static_cast<RE::ActionInput::Priority>(payload.Type & 0x3u);
    data->animEvent = text.data();
    data->targetAnimEvent = text.data() + separator + 1;
    data->animObjIdle = idle;
    data->flags = (payload.Type & 0x4u) != 0 ? 1u : 0u;

    const ScopedRemoteAction remoteAction;
    const auto result = VerifiedVrActorAction::ForceAction(mediator, data);
    return result != 0 ? CommandStatus::Success : CommandStatus::EngineRejected;
}
} // namespace

bool Install() noexcept
{
    BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, false);
    if (g_installed.load(std::memory_order_acquire) && VerifiedVrActorAction::IsReady()) {
        BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, true);
        return true;
    }
    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;
    const auto finish = [](const bool a_result) noexcept {
        g_installing.store(false, std::memory_order_release);
        return a_result;
    };
    const auto failDetached = [&finish]() noexcept {
        ForgetDetachedPerformActionHook();
        g_installed.store(false, std::memory_order_release);
        ClearPending();
        g_nextActionId.store(0, std::memory_order_release);
        VerifiedVrActorAction::Reset();
        BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, false);
        return finish(false);
    };
    const auto retainDegraded = [&finish](const char* a_operation) noexcept {
        LogRetainedPerformActionHook(a_operation);
        g_installed.store(true, std::memory_order_release);
        BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, true);
        return finish(true);
    };
    try {
        if (!VerifiedVrActorAction::Initialize()) {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: verified VR ActorMediator action targets failed validation");
            return failDetached();
        }
        const auto target = VerifiedVrActorAction::GetPerformAction();
        if (!target)
            return failDetached();
        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: MinHook initialization failed for ActorMediator::PerformAction ({})",
                             static_cast<int>(initialize));
            return failDetached();
        }
        g_performActionTarget = reinterpret_cast<void*>(target);
        auto status = MH_CreateHook(g_performActionTarget, reinterpret_cast<void*>(&HookPerformAction),
                                    reinterpret_cast<void**>(&g_originalPerformAction));
        if (status != MH_OK) {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: ActorMediator::PerformAction hook creation failed ({})",
                             static_cast<int>(status));
            return failDetached();
        }
        g_hookState.Created = true;
        // MinHook may fail after beginning to alter the target.  Rollback
        // must therefore prove disablement before the trampoline is cleared.
        g_hookState.Enabled = true;
        status = MH_EnableHook(g_performActionTarget);
        if (status != MH_OK) {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: ActorMediator::PerformAction hook enable failed ({})",
                             static_cast<int>(status));
            if (DetachPerformActionHook())
                return failDetached();
            return retainDegraded("install rollback");
        }
        g_installed.store(true, std::memory_order_release);
        BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, true);
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: installed verified ActorMediator action capture and ForceAction-equivalent replay");
        return finish(true);
    } catch (...) {
        if (g_hookState.Created && !DetachPerformActionHook())
            return retainDegraded("exception rollback");
        return failDetached();
    }
}

bool Uninstall() noexcept
{
    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;
    if (g_hookState.Created && !DetachPerformActionHook()) {
        LogRetainedPerformActionHook("uninstall");
        g_installed.store(true, std::memory_order_release);
        BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, true);
        g_installing.store(false, std::memory_order_release);
        return false;
    }
    ForgetDetachedPerformActionHook();
    g_installed.store(false, std::memory_order_release);
    ClearPending();
    g_nextActionId.store(0, std::memory_order_release);
    VerifiedVrActorAction::Reset();
    BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, false);
    g_installing.store(false, std::memory_order_release);
    return true;
}

void Reset() noexcept
{
    ClearPending();
    g_nextActionId.store(0, std::memory_order_release);
}

CommandStatus Execute(const CommandRecord& a_command) noexcept
{
    try {
        if (!g_installed.load(std::memory_order_acquire) || !g_originalPerformAction ||
            !VerifiedVrActorAction::IsReady())
            return CommandStatus::Inactive;
        switch (static_cast<CommandKind>(a_command.Header.Kind)) {
        case CommandKind::StageActorActionGraphChunk:
            return StageGraph(a_command);
        case CommandKind::StageActorActionTextChunk:
            return StageText(a_command);
        case CommandKind::ApplyActorAction:
            return ApplyAction(a_command);
        default:
            return CommandStatus::Malformed;
        }
    } catch (...) {
        return CommandStatus::EngineRejected;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::ActorActionHooks
