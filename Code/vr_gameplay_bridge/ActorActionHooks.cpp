#include "ActorActionHooks.h"

#include "AvatarManager.h"
#include "AnimationGraphDescriptors.h"

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <bitset>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>

namespace SkyrimTogetherVR::GameplayAdapter::ActorActionHooks
{
namespace
{
using PerformAction = std::uint8_t (*)(void*, RE::TESActionData*);
using TesActionDataCtor = RE::TESActionData* (__fastcall*)(RE::TESActionData*);
using DeletingDestructor = void(__fastcall*)(RE::TESActionData*, std::uint32_t) noexcept;

constexpr std::uintptr_t kSkyrimVrPerformActionRva = 0x0643F20;
constexpr std::uintptr_t kSkyrimVrTesActionDataCtorRva = 0x1FE070;
// TESActionData::Ctor first installs the BGSActionData vtable at 0x15BF5A0;
// CommonLib then installs the derived TESActionData vtable used by live
// action objects and its deleting destructor.
constexpr std::uintptr_t kSkyrimVrTesActionDataVtableRva = 0x15BF5D8;
constexpr std::array<std::uint8_t, 16> kSkyrimVrPerformActionPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0xDA,
};
constexpr std::array<std::uint8_t, 16> kSkyrimVrTesActionDataCtorPrologue{
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x57,
    0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x44, 0x24,
};
constexpr std::size_t kMaximumPendingActions = 64;
constexpr std::size_t kMaximumActionStringBytes = 127;
constexpr std::size_t kMaximumActionGraphChunks =
    1 + (AnimationGraphProtocol::kMaximumFloatCount + AnimationGraphProtocol::kValuesPerChunk - 1) /
            AnimationGraphProtocol::kValuesPerChunk +
    (AnimationGraphProtocol::kMaximumIntegerCount + AnimationGraphProtocol::kValuesPerChunk - 1) /
            AnimationGraphProtocol::kValuesPerChunk;
constexpr std::size_t kMaximumActionEventRecords =
    kMaximumActionGraphChunks + kMaximumGameplayTextChunks + 1;

PerformAction g_originalPerformAction{};
std::atomic<TesActionDataCtor> g_tesActionDataCtor{};
std::atomic<DeletingDestructor> g_tesActionDataDeletingDestructor{};
void* g_performActionTarget{};
std::atomic<void*> g_actorMediator{};
std::atomic<bool> g_installing{};
std::atomic<bool> g_installed{};
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

std::unordered_map<std::uint64_t, PendingAction> g_pendingActions;

struct LocalActionCapture
{
    RE::Actor* Actor{};
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

[[nodiscard]] PendingAction& GetOrCreatePending(const std::uint64_t a_transactionId)
{
    if (!g_pendingActions.contains(a_transactionId) && g_pendingActions.size() >= kMaximumPendingActions) {
        const auto oldest = std::min_element(
            g_pendingActions.begin(), g_pendingActions.end(),
            [](const auto& a_left, const auto& a_right) { return a_left.first < a_right.first; });
        if (oldest != g_pendingActions.end())
            g_pendingActions.erase(oldest);
    }
    return g_pendingActions[a_transactionId];
}

void DiscardPending(const std::uint64_t a_transactionId) noexcept
{
    g_pendingActions.erase(a_transactionId);
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

[[nodiscard]] bool IsExecutableTarget(const std::uintptr_t a_address) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (a_address < text.address() || a_address >= text.address() + text.size())
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & executable) != 0;
}

[[nodiscard]] bool IsActorMediator(const void* a_candidate) noexcept
{
    if (!a_candidate)
        return false;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(a_candidate, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    const auto candidate = reinterpret_cast<std::uintptr_t>(a_candidate);
    const auto regionEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    if (candidate > regionEnd || regionEnd - candidate < sizeof(std::uintptr_t))
        return false;
    const REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_ActorMediator[0]};
    return *static_cast<const std::uintptr_t*>(a_candidate) == vtable.address();
}

[[nodiscard]] bool IsReadableObject(const void* a_candidate, const std::size_t a_size) noexcept
{
    if (!a_candidate || a_size == 0)
        return false;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(a_candidate, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    const auto candidate = reinterpret_cast<std::uintptr_t>(a_candidate);
    const auto regionEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    return candidate <= regionEnd && regionEnd - candidate >= a_size;
}

[[nodiscard]] bool ValidateActionDataFactory()
{
    const auto expectedCtorAddress = REL::Module::get().base() + kSkyrimVrTesActionDataCtorRva;
    const REL::Relocation<TesActionDataCtor> constructor{
        REL::RelocationID(15916, 41558, 15916)};
    if (constructor.address() != expectedCtorAddress || !IsExecutableTarget(expectedCtorAddress) ||
        std::memcmp(reinterpret_cast<const void*>(expectedCtorAddress),
                    kSkyrimVrTesActionDataCtorPrologue.data(),
                    kSkyrimVrTesActionDataCtorPrologue.size()) != 0)
        return false;

    const REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_TESActionData[0]};
    if (vtable.offset() != kSkyrimVrTesActionDataVtableRva ||
        !IsReadableObject(reinterpret_cast<const void*>(vtable.address()), sizeof(std::uintptr_t)))
        return false;

    const auto deletingDestructor = *reinterpret_cast<const DeletingDestructor*>(vtable.address());
    if (!deletingDestructor || !IsExecutableTarget(reinterpret_cast<std::uintptr_t>(deletingDestructor)))
        return false;

    g_tesActionDataCtor.store(constructor.get(), std::memory_order_release);
    g_tesActionDataDeletingDestructor.store(deletingDestructor, std::memory_order_release);
    return true;
}

[[nodiscard]] bool IsTesActionData(const RE::TESActionData* a_data) noexcept
{
    if (!IsReadableObject(a_data, sizeof(RE::TESActionData)))
        return false;
    const REL::Relocation<std::uintptr_t> expectedVtable{RE::VTABLE_TESActionData[0]};
    if (expectedVtable.offset() != kSkyrimVrTesActionDataVtableRva ||
        *reinterpret_cast<const std::uintptr_t*>(a_data) != expectedVtable.address())
        return false;

    auto* source = a_data->source.get();
    auto* action = a_data->action;
    if (!IsReadableObject(source, sizeof(std::uintptr_t)) || !IsReadableObject(action, sizeof(std::uintptr_t)) ||
        RE::TESForm::LookupByID<RE::Actor>(source->GetFormID()) != source ||
        RE::TESForm::LookupByID<RE::BGSAction>(action->GetFormID()) != action)
        return false;
    if (auto* target = a_data->target.get();
        target && (!IsReadableObject(target, sizeof(std::uintptr_t)) ||
                   RE::TESForm::LookupByID<RE::TESObjectREFR>(target->GetFormID()) != target))
        return false;
    if (const auto* idle = a_data->animObjIdle;
        idle && (!IsReadableObject(idle, sizeof(std::uintptr_t)) ||
                 RE::TESForm::LookupByID<RE::TESIdleForm>(idle->GetFormID()) != idle))
        return false;
    return true;
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
    const std::string& a_text) noexcept
{
    if (a_text.empty() || a_text.size() >
                              static_cast<std::size_t>(kGameplayTextBytesPerChunk) * kMaximumGameplayTextChunks)
        return false;
    const auto chunkCount = static_cast<std::uint16_t>(
        (a_text.size() + kGameplayTextBytesPerChunk - 1) / kGameplayTextBytesPerChunk);
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
            std::min<std::size_t>(kGameplayTextBytesPerChunk, a_text.size() - offset));
        std::memcpy(payload.Utf8Bytes, a_text.data() + offset, payload.ByteCount);
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

    ar_capture.Actor = std::addressof(a_actor);
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
    if (!a_capture.Valid || !a_capture.Actor || !endpoint.IsOperational() ||
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
    std::string text{eventName};
    text.push_back('\0');
    text.append(targetEventName);

    const auto actionId = NextActionId();
    std::array<EventRecord, kMaximumActionEventRecords> records{};
    std::size_t recordCount{};
    if (!AppendGraphRecords(records, recordCount, a_capture.Handle, a_capture.ActorFormId, actionId, a_capture.Graph) ||
        !AppendTextRecords(records, recordCount, a_capture.Handle, a_capture.ActorFormId, actionId, text))
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
        if (IsActorMediator(a_mediator) && IsTesActionData(a_data)) {
            actor = a_data->source->As<RE::Actor>();
            managedRemote = actor && AvatarManager::Get().IsManagedRemoteActor(actor);
            if (managedRemote && g_remoteActionDepth == 0)
                return 0;

            locallyOwned = actor && !managedRemote && g_remoteActionDepth == 0;
            if (locallyOwned) {
                // This mediates the engine action lifecycle, not its success result.
                // A valid local invocation is enough to establish the live singleton.
                g_actorMediator.store(a_mediator, std::memory_order_release);
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

void ReleaseActionData(RE::TESActionData* a_data) noexcept
{
    if (!a_data)
        return;
    const auto validatedDestructor = g_tesActionDataDeletingDestructor.load(std::memory_order_acquire);
    const REL::Relocation<std::uintptr_t> expectedVtable{RE::VTABLE_TESActionData[0]};
    const auto liveVtable = *reinterpret_cast<const std::uintptr_t*>(a_data);
    if (!validatedDestructor || expectedVtable.offset() != kSkyrimVrTesActionDataVtableRva ||
        liveVtable != expectedVtable.address())
        return;
    const auto liveDestructor = *reinterpret_cast<const DeletingDestructor*>(liveVtable);
    if (liveDestructor != validatedDestructor)
        return;
    // The live derived vtable's scalar deleting destructor owns both the
    // TESActionData members and the engine heap allocation. Never free again.
    liveDestructor(a_data, 1);
}

[[nodiscard]] RE::TESActionData* CreateActionData()
{
    const auto constructor = g_tesActionDataCtor.load(std::memory_order_acquire);
    const auto deletingDestructor = g_tesActionDataDeletingDestructor.load(std::memory_order_acquire);
    const REL::Relocation<std::uintptr_t> expectedVtable{RE::VTABLE_TESActionData[0]};
    if (!constructor || !deletingDestructor || expectedVtable.offset() != kSkyrimVrTesActionDataVtableRva)
        return nullptr;

    auto* data = RE::malloc<RE::TESActionData>();
    if (!data)
        return nullptr;
    std::memset(data, 0, sizeof(*data));

    const auto* constructed = constructor(data);
    if (constructed != data) {
        // This verified constructor must be in-place. If that contract is
        // violated, its return provides no proof that `data` is a constructed
        // TESActionData. Do not synthesize a vtable or call a destructor on
        // potentially unconstructed storage; release only the raw engine
        // allocation and fail the replay closed.
        RE::free(data);
        return nullptr;
    }

    *reinterpret_cast<std::uintptr_t*>(data) = expectedVtable.address();
    return data;
}

[[nodiscard]] CommandStatus ApplyAction(const CommandRecord& a_command)
{
    if (!g_installed.load(std::memory_order_acquire) || !AvatarManager::Get().IsOnCommandPumpThread())
        return CommandStatus::Inactive;

    const auto payload = a_command.Payload.ApplyActorAction;
    if (payload.TargetHandle.Value == 0 || payload.ActorLocalFormId != 0 || payload.ActionLocalFormId == 0 ||
        payload.SnapshotId == 0 || payload.TextId != payload.SnapshotId ||
        payload.ActionFlags != 0 || (payload.Type & ~0x7u) != 0 || (payload.Type & 0x3u) == 0x3u ||
        (payload.Type & 0x4u) != 0 ||
        !IsZero(payload.Reserved, sizeof(payload.Reserved))) {
        if (payload.SnapshotId != 0)
            DiscardPending(payload.SnapshotId);
        return CommandStatus::Malformed;
    }

    const auto mediator = g_actorMediator.load(std::memory_order_acquire);
    if (!IsActorMediator(mediator) || !g_originalPerformAction) {
        if (mediator) {
            auto expected = mediator;
            g_actorMediator.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        }
        return CommandStatus::Inactive;
    }

    const auto pendingIt = g_pendingActions.find(payload.SnapshotId);
    if (pendingIt == g_pendingActions.end() || !pendingIt->second.Graph.IsComplete() ||
        pendingIt->second.TextChunkCount == 0 ||
        pendingIt->second.TextReceived.count() != pendingIt->second.TextChunkCount)
        return CommandStatus::Inactive;
    auto pending = std::move(pendingIt->second);
    g_pendingActions.erase(pendingIt);
    if (pending.TargetHandle.Value != payload.TargetHandle.Value)
        return CommandStatus::Malformed;

    std::string text;
    for (std::uint16_t index = 0; index < pending.TextChunkCount; ++index)
        text.append(pending.TextChunks[index].data(), pending.TextLengths[index]);
    const auto separator = text.find('\0');
    if (separator == std::string::npos || separator > kMaximumActionStringBytes ||
        text.size() - separator - 1 > kMaximumActionStringBytes)
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

    auto* action = RE::TESForm::LookupByID<RE::BGSAction>(payload.ActionLocalFormId);
    auto* target = payload.ActionTargetLocalFormId != 0 ?
                       RE::TESForm::LookupByID<RE::TESObjectREFR>(payload.ActionTargetLocalFormId) : nullptr;
    auto* idle = payload.IdleLocalFormId != 0 ? RE::TESForm::LookupByID<RE::TESIdleForm>(payload.IdleLocalFormId) : nullptr;
    if (!action || (payload.ActionTargetLocalFormId != 0 && !target) || (payload.IdleLocalFormId != 0 && !idle))
        return CommandStatus::MissingForm;

    auto* data = CreateActionData();
    if (!data)
        return CommandStatus::EngineRejected;
    std::unique_ptr<RE::TESActionData, decltype(&ReleaseActionData)> dataGuard{data, &ReleaseActionData};
    data->source = RE::NiPointer<RE::TESObjectREFR>(actor.get());
    data->target = RE::NiPointer<RE::TESObjectREFR>(target);
    data->action = action;
    data->priority = static_cast<RE::ActionInput::Priority>(payload.Type & 0x3u);
    data->animEvent = text.substr(0, separator).c_str();
    data->targetAnimEvent = text.substr(separator + 1).c_str();
    data->animObjIdle = idle;
    data->flags = 0;

    auto* actorState = actor->AsActorState();
    if (!actorState)
        return CommandStatus::EngineRejected;
    const auto previousState1 = actorState->actorState1;
    const auto previousState2 = actorState->actorState2;
    AnimationGraphProtocol::SnapshotBuffer previousGraph{};
    if (!AnimationGraphs::Capture(*actor, previousGraph))
        return CommandStatus::EngineRejected;
    previousGraph.SnapshotId = 1;
    previousGraph.BooleanChunkMask = AnimationGraphProtocol::ExpectedChunkMask(
        AnimationGraphProtocol::ValueType::BooleanBits, previousGraph.BooleanCount);
    previousGraph.FloatChunkMask = AnimationGraphProtocol::ExpectedChunkMask(
        AnimationGraphProtocol::ValueType::Float, previousGraph.FloatCount);
    previousGraph.IntegerChunkMask = AnimationGraphProtocol::ExpectedChunkMask(
        AnimationGraphProtocol::ValueType::Integer, previousGraph.IntegerCount);
    const auto rollback = [&]() noexcept {
        actorState->actorState1 = previousState1;
        actorState->actorState2 = previousState2;
        static_cast<void>(AvatarManager::Get().ApplyAnimationSnapshotToActor(*actor, previousGraph));
    };
    const auto rollbackOnFailure = [&rollback](void*) noexcept { rollback(); };
    std::unique_ptr<void, decltype(rollbackOnFailure)> rollbackGuard{
        reinterpret_cast<void*>(1), rollbackOnFailure};
    actorState->actorState1 = std::bit_cast<RE::ActorState::ActorState1>(payload.State1);
    actorState->actorState2 = std::bit_cast<RE::ActorState::ActorState2>(payload.State2);
    const auto graphResult = AvatarManager::Get().ApplyAnimationSnapshotToActor(*actor, pending.Graph);
    if (graphResult != CommandStatus::Success)
        return graphResult;

    const ScopedRemoteAction remoteAction;
    const auto result = g_originalPerformAction(mediator, data);
    if (result != 0)
        rollbackGuard.release();
    return result != 0 ? CommandStatus::Success : CommandStatus::EngineRejected;
}
} // namespace

bool Install() noexcept
{
    // Normal VR PerformAction is not a substitute for desktop ForceAction.
    // Keep the implementation available for future runtime validation, but
    // never negotiate the wire lane until that replay path is proven.
    BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, false);
    if (g_installed.load(std::memory_order_acquire))
        return true;
    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;
    const auto finish = [](const bool a_result) noexcept {
        g_installing.store(false, std::memory_order_release);
        return a_result;
    };
    const auto fail = [&finish]() noexcept {
        g_performActionTarget = nullptr;
        g_originalPerformAction = nullptr;
        g_tesActionDataCtor.store(nullptr, std::memory_order_release);
        g_tesActionDataDeletingDestructor.store(nullptr, std::memory_order_release);
        g_actorMediator.store(nullptr, std::memory_order_release);
        g_installed.store(false, std::memory_order_release);
        g_pendingActions.clear();
        g_nextActionId.store(0, std::memory_order_release);
        BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, false);
        return finish(false);
    };
    try {
        const auto target = REL::Module::get().base() + kSkyrimVrPerformActionRva;
        if (!IsExecutableTarget(target) ||
            std::memcmp(reinterpret_cast<const void*>(target),
                        kSkyrimVrPerformActionPrologue.data(),
                        kSkyrimVrPerformActionPrologue.size()) != 0) {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: ActorMediator::PerformAction VR target validation failed at RVA 0x{:X}",
                kSkyrimVrPerformActionRva);
            return fail();
        }
        if (!ValidateActionDataFactory()) {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: TESActionData factory validation failed for ID 15916 at VR RVA 0x{:X}",
                kSkyrimVrTesActionDataCtorRva);
            return fail();
        }
        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: MinHook initialization failed for ActorMediator::PerformAction ({})",
                             static_cast<int>(initialize));
            return fail();
        }
        g_performActionTarget = reinterpret_cast<void*>(target);
        auto status = MH_CreateHook(g_performActionTarget, reinterpret_cast<void*>(&HookPerformAction),
                                    reinterpret_cast<void**>(&g_originalPerformAction));
        if (status != MH_OK) {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: ActorMediator::PerformAction hook creation failed ({})",
                             static_cast<int>(status));
            return fail();
        }
        status = MH_EnableHook(g_performActionTarget);
        if (status != MH_OK) {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: ActorMediator::PerformAction hook enable failed ({})",
                             static_cast<int>(status));
            MH_RemoveHook(g_performActionTarget);
            return fail();
        }
        g_installed.store(true, std::memory_order_release);
        BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, false);
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: installed ActorMediator::PerformAction capture hook at VR RVA 0x{:X}; "
            "exact action replay remains disabled pending VR ForceAction validation",
            kSkyrimVrPerformActionRva);
        return finish(true);
    } catch (...) {
        if (g_performActionTarget) {
            MH_DisableHook(g_performActionTarget);
            MH_RemoveHook(g_performActionTarget);
        }
        return fail();
    }
}

void Uninstall() noexcept
{
    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;
    if (g_performActionTarget) {
        MH_DisableHook(g_performActionTarget);
        MH_RemoveHook(g_performActionTarget);
    }
    g_performActionTarget = nullptr;
    g_originalPerformAction = nullptr;
    g_tesActionDataCtor.store(nullptr, std::memory_order_release);
    g_tesActionDataDeletingDestructor.store(nullptr, std::memory_order_release);
    g_actorMediator.store(nullptr, std::memory_order_release);
    g_installed.store(false, std::memory_order_release);
    Reset();
    g_installing.store(false, std::memory_order_release);
}

void Reset() noexcept
{
    g_pendingActions.clear();
    g_nextActionId.store(0, std::memory_order_release);
    // ActorMediator is process-lifetime. A reconnect only invalidates staged
    // network work, so retain the observed pointer and revalidate it at use.
    BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, false);
}

CommandStatus Execute(const CommandRecord& a_command) noexcept
{
    try {
        if (!g_installed.load(std::memory_order_acquire) || !g_originalPerformAction)
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
