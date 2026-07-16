#include "ActorActionHooks.h"

#include "AvatarManager.h"
#include "HumanoidAnimationGraph.h"

#include <MinHook.h>

#include <algorithm>
#include <array>
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

constexpr std::uintptr_t kSkyrimVrPerformActionRva = 0x0643F20;
constexpr std::array<std::uint8_t, 16> kSkyrimVrPerformActionPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0xDA,
};
constexpr std::size_t kMaximumPendingActions = 64;
constexpr std::size_t kMaximumActionStringBytes = 127;
constexpr std::size_t kMaximumActionGraphChunks =
    1 + (AnimationGraphProtocol::kFloatCount + AnimationGraphProtocol::kValuesPerChunk - 1) /
            AnimationGraphProtocol::kValuesPerChunk +
    (AnimationGraphProtocol::kIntegerCount + AnimationGraphProtocol::kValuesPerChunk - 1) /
            AnimationGraphProtocol::kValuesPerChunk;
constexpr std::size_t kMaximumActionEventRecords =
    kMaximumActionGraphChunks + kMaximumGameplayTextChunks + 1;

PerformAction g_originalPerformAction{};
void* g_performActionTarget{};
std::atomic<void*> g_actorMediator{};
std::atomic<bool> g_installing{};
std::atomic<bool> g_installed{};
std::atomic<bool> g_semanticallyValidated{};
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

[[nodiscard]] bool IsTesActionData(const RE::TESActionData* a_data) noexcept
{
    if (!IsReadableObject(a_data, sizeof(RE::TESActionData)))
        return false;
    const REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_TESActionData[0]};
    if (*reinterpret_cast<const std::uintptr_t*>(a_data) != vtable.address())
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

void PromoteSemanticCapability() noexcept
{
    if (g_semanticallyValidated.exchange(true, std::memory_order_acq_rel))
        return;
    BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, true);
    SKSE::log::info("SkyrimTogetherVRGameplayBridge: ActorMediator action lane passed live semantic validation");
}

[[nodiscard]] void* DiscoverActorMediator() noexcept
{
    if (const auto cached = g_actorMediator.load(std::memory_order_acquire); IsActorMediator(cached))
        return cached;

    const auto data = REL::Module::get().segment(REL::Segment::data);
    const REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_ActorMediator[0]};
    void* match{};
    std::size_t matchCount{};
    const auto end = data.address() + data.size();
    for (auto address = data.address(); address <= end && end - address >= sizeof(std::uintptr_t);
         address += alignof(std::uintptr_t)) {
        if (*reinterpret_cast<const std::uintptr_t*>(address) != vtable.address())
            continue;
        auto* candidate = reinterpret_cast<void*>(address);
        if (!IsActorMediator(candidate))
            continue;
        match = candidate;
        if (++matchCount > 1)
            return nullptr;
    }
    if (matchCount != 1)
        return nullptr;
    g_actorMediator.store(match, std::memory_order_release);
    return match;
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

[[nodiscard]] bool CaptureGraph(
    RE::Actor& a_actor,
    std::array<bool, AnimationGraphProtocol::kBooleanCount>& ar_booleans,
    std::array<float, AnimationGraphProtocol::kFloatCount>& ar_floats,
    std::array<std::int32_t, AnimationGraphProtocol::kIntegerCount>& ar_integers) noexcept
{
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if (!a_actor.GetAnimationGraphManager(manager) || !manager)
        return false;
    for (std::size_t index = 0; index < ar_booleans.size(); ++index) {
        if (!a_actor.GetGraphVariableBool(RE::BSFixedString(HumanoidAnimationGraph::kBooleanNames[index].data()),
                                          ar_booleans[index]))
            return false;
    }
    for (std::size_t index = 0; index < ar_floats.size(); ++index) {
        if (!a_actor.GetGraphVariableFloat(RE::BSFixedString(HumanoidAnimationGraph::kFloatNames[index].data()),
                                           ar_floats[index]) || !std::isfinite(ar_floats[index]))
            return false;
    }
    for (std::size_t index = 0; index < ar_integers.size(); ++index) {
        if (!a_actor.GetGraphVariableInt(RE::BSFixedString(HumanoidAnimationGraph::kIntegerNames[index].data()),
                                         ar_integers[index]))
            return false;
    }
    return true;
}

void RestoreGraph(
    RE::Actor& a_actor,
    const std::array<bool, AnimationGraphProtocol::kBooleanCount>& a_booleans,
    const std::array<float, AnimationGraphProtocol::kFloatCount>& a_floats,
    const std::array<std::int32_t, AnimationGraphProtocol::kIntegerCount>& a_integers) noexcept
{
    for (std::size_t index = 0; index < a_booleans.size(); ++index)
        a_actor.SetGraphVariableBool(
            RE::BSFixedString(HumanoidAnimationGraph::kBooleanNames[index].data()), a_booleans[index]);
    for (std::size_t index = 0; index < a_floats.size(); ++index)
        a_actor.SetGraphVariableFloat(
            RE::BSFixedString(HumanoidAnimationGraph::kFloatNames[index].data()), a_floats[index]);
    for (std::size_t index = 0; index < a_integers.size(); ++index)
        a_actor.SetGraphVariableInt(
            RE::BSFixedString(HumanoidAnimationGraph::kIntegerNames[index].data()), a_integers[index]);
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
    const std::uint16_t a_count,
    const float a_direction) noexcept
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
    payload.TotalCount = AnimationGraphProtocol::ExpectedCount(a_type);
    payload.ChunkFlags = AnimationGraphProtocol::FullSnapshot;
    payload.Direction = a_direction;
}

[[nodiscard]] bool AppendGraphRecords(
    std::array<EventRecord, kMaximumActionEventRecords>& ar_records,
    std::size_t& ar_count,
    const AdapterHandle a_handle,
    const std::uint32_t a_actorFormId,
    const std::uint64_t a_actionId,
    const std::array<bool, AnimationGraphProtocol::kBooleanCount>& a_booleans,
    const std::array<float, AnimationGraphProtocol::kFloatCount>& a_floats,
    const std::array<std::int32_t, AnimationGraphProtocol::kIntegerCount>& a_integers) noexcept
{
    if (ar_records.size() - ar_count < kMaximumActionGraphChunks)
        return false;

    auto& booleanChunk = ar_records[ar_count++];
    PopulateGraphChunk(booleanChunk, a_handle, a_actorFormId, a_actionId,
                       AnimationGraphProtocol::ValueType::BooleanBits, 0,
                       AnimationGraphProtocol::kBooleanCount, a_floats[1]);
    for (std::size_t index = 0; index < a_booleans.size(); ++index) {
        if (a_booleans[index])
            booleanChunk.Payload.LocalActorActionGraphChunk.Values[index / 32] |= 1u << (index % 32);
    }

    for (std::uint16_t start = 0; start < a_floats.size(); start += AnimationGraphProtocol::kValuesPerChunk) {
        const auto count = static_cast<std::uint16_t>(
            std::min<std::size_t>(AnimationGraphProtocol::kValuesPerChunk, a_floats.size() - start));
        auto& chunk = ar_records[ar_count++];
        PopulateGraphChunk(chunk, a_handle, a_actorFormId, a_actionId,
                           AnimationGraphProtocol::ValueType::Float, start, count, a_floats[1]);
        for (std::uint16_t index = 0; index < count; ++index)
            chunk.Payload.LocalActorActionGraphChunk.Values[index] = std::bit_cast<std::uint32_t>(a_floats[start + index]);
    }
    for (std::uint16_t start = 0; start < a_integers.size(); start += AnimationGraphProtocol::kValuesPerChunk) {
        const auto count = static_cast<std::uint16_t>(
            std::min<std::size_t>(AnimationGraphProtocol::kValuesPerChunk, a_integers.size() - start));
        auto& chunk = ar_records[ar_count++];
        PopulateGraphChunk(chunk, a_handle, a_actorFormId, a_actionId,
                           AnimationGraphProtocol::ValueType::Integer, start, count, a_floats[1]);
        for (std::uint16_t index = 0; index < count; ++index)
            chunk.Payload.LocalActorActionGraphChunk.Values[index] = std::bit_cast<std::uint32_t>(a_integers[start + index]);
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

void PublishLocalAction(RE::Actor& a_actor, const RE::TESActionData& a_data) noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational() ||
        !HasCapability(endpoint.Mapping()->Header.ActiveCapabilities.load(std::memory_order_acquire),
                       Capability::ExactAnimationActions))
        return;

    const auto actorFormId = a_actor.GetFormID();
    const auto actionFormId = a_data.action ? a_data.action->GetFormID() : 0;
    if (!IsValidFormId(actorFormId) || !IsValidFormId(actionFormId))
        return;
    const auto targetFormId = a_data.target ? a_data.target->GetFormID() : 0;
    const auto idleFormId = a_data.animObjIdle ? a_data.animObjIdle->GetFormID() : 0;
    if ((targetFormId != 0 && !IsValidFormId(targetFormId)) ||
        (idleFormId != 0 && !IsValidFormId(idleFormId)))
        return;

    const std::string_view eventName{a_data.animEvent.c_str()};
    const std::string_view targetEventName{a_data.targetAnimEvent.c_str()};
    if (eventName.size() > kMaximumActionStringBytes || targetEventName.size() > kMaximumActionStringBytes)
        return;
    std::string text{eventName};
    text.push_back('\0');
    text.append(targetEventName);

    std::array<bool, AnimationGraphProtocol::kBooleanCount> booleans{};
    std::array<float, AnimationGraphProtocol::kFloatCount> floats{};
    std::array<std::int32_t, AnimationGraphProtocol::kIntegerCount> integers{};
    if (!CaptureGraph(a_actor, booleans, floats, integers))
        return;

    const auto actionId = NextActionId();
    const auto handle = &a_actor == RE::PlayerCharacter::GetSingleton() ? kLocalPlayerHandle : AdapterHandle{};
    auto* actorState = a_actor.AsActorState();
    if (!actorState)
        return;
    std::array<EventRecord, kMaximumActionEventRecords> records{};
    std::size_t recordCount{};
    if (!AppendGraphRecords(records, recordCount, handle, actorFormId, actionId, booleans, floats, integers) ||
        !AppendTextRecords(records, recordCount, handle, actorFormId, actionId, text))
        return;

    auto& metadata = records[recordCount++];
    PopulateHeader(metadata, EventKind::LocalActorActionMetadata, actionId);
    auto& payload = metadata.Payload.LocalActorActionMetadata;
    payload.TargetHandle = handle;
    payload.ActorLocalFormId = actorFormId;
    payload.ActionLocalFormId = actionFormId;
    payload.ActionTargetLocalFormId = targetFormId;
    payload.IdleLocalFormId = idleFormId;
    payload.State1 = std::bit_cast<std::uint32_t>(actorState->actorState1);
    payload.State2 = std::bit_cast<std::uint32_t>(actorState->actorState2);
    payload.Type = static_cast<std::uint32_t>(a_data.priority.underlying()) | ((a_data.flags & 1u) != 0 ? 0x4u : 0u);
    payload.SnapshotId = actionId;
    payload.TextId = actionId;
    if (!endpoint.TryPushEvents(records.data(), recordCount))
        return;
}

std::uint8_t HookPerformAction(void* a_mediator, RE::TESActionData* a_data) noexcept
{
    try {
        if (!IsActorMediator(a_mediator) || !IsTesActionData(a_data))
            return g_originalPerformAction ? g_originalPerformAction(a_mediator, a_data) : 0;

        g_actorMediator.store(a_mediator, std::memory_order_release);
        auto* actor = a_data->source->As<RE::Actor>();
        if (actor && AvatarManager::Get().IsManagedRemoteActor(actor) && g_remoteActionDepth == 0)
            return 0;

        const auto result = g_originalPerformAction ? g_originalPerformAction(a_mediator, a_data) : 0;
        if (result != 0)
            PromoteSemanticCapability();
        if (result != 0 && actor && a_data->flags != 1 && g_remoteActionDepth == 0)
            PublishLocalAction(*actor, *a_data);
        return result;
    } catch (...) {
        return 0;
    }
}

[[nodiscard]] CommandStatus StageGraph(const CommandRecord& a_command) noexcept
{
    const auto& payload = a_command.Payload.StageActorActionGraphChunk;
    if (payload.TargetHandle.Value == 0 || payload.ActorLocalFormId != 0 || payload.Reserved0 != 0 ||
        payload.SnapshotId == 0 || payload.Reserved1 != 0 ||
        payload.DescriptorVersion != AnimationGraphProtocol::kDescriptorVersion ||
        payload.ChunkFlags != AnimationGraphProtocol::FullSnapshot ||
        !IsZero(payload.ReservedTail, sizeof(payload.ReservedTail)))
        return CommandStatus::Malformed;
    const auto type = static_cast<AnimationGraphProtocol::ValueType>(payload.ValueType);
    if (!AnimationGraphProtocol::IsValidChunk(type, payload.StartIndex, payload.ValueCount, payload.TotalCount) ||
        !AnimationGraphProtocol::AreChunkValuesValid(type, payload.ValueCount, payload.Values) ||
        !std::isfinite(payload.Direction))
        return CommandStatus::Malformed;
    auto& pending = GetOrCreatePending(payload.SnapshotId);
    if (pending.TargetHandle.Value == 0)
        pending.TargetHandle = payload.TargetHandle;
    if (pending.TargetHandle.Value != payload.TargetHandle.Value)
        return CommandStatus::Malformed;
    const auto accepted = AnimationGraphProtocol::AcceptChunk(
        pending.Graph, payload.SnapshotId, type, payload.StartIndex, payload.ValueCount,
        payload.TotalCount, payload.Direction, payload.Values);
    return accepted == AnimationGraphProtocol::ChunkAcceptResult::Malformed ?
               CommandStatus::Malformed : CommandStatus::Success;
}

[[nodiscard]] CommandStatus StageText(const CommandRecord& a_command) noexcept
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
                kGameplayTextBytesPerChunk - payload.ByteCount))
        return CommandStatus::Malformed;
    auto& pending = GetOrCreatePending(payload.TextId);
    if (pending.TargetHandle.Value == 0)
        pending.TargetHandle = payload.TargetHandle;
    if (pending.TargetHandle.Value != payload.TargetHandle.Value)
        return CommandStatus::Malformed;
    if (pending.TextChunkCount == 0) {
        pending.TextChunkCount = payload.ChunkCount;
        pending.TextId = payload.TextId;
    }
    if (pending.TextChunkCount != payload.ChunkCount || pending.TextId != payload.TextId ||
        pending.TextReceived.test(payload.ChunkIndex))
        return CommandStatus::Malformed;
    std::copy_n(payload.Utf8Bytes, payload.ByteCount, pending.TextChunks[payload.ChunkIndex].begin());
    pending.TextLengths[payload.ChunkIndex] = payload.ByteCount;
    pending.TextReceived.set(payload.ChunkIndex);
    return CommandStatus::Success;
}

void ReleaseActionData(RE::TESActionData* a_data) noexcept
{
    if (!a_data)
        return;
    // CommonLib exposes the engine constructor but not a callable destructor.
    // Release the two owning field families before returning engine memory.
    a_data->source.reset();
    a_data->target.reset();
    a_data->animEvent = "";
    a_data->targetAnimEvent = "";
    RE::free(a_data);
}

[[nodiscard]] CommandStatus ApplyAction(const CommandRecord& a_command) noexcept
{
    const auto payload = a_command.Payload.ApplyActorAction;
    if (payload.TargetHandle.Value == 0 || payload.ActorLocalFormId != 0 || payload.ActionLocalFormId == 0 ||
        payload.SnapshotId == 0 || payload.TextId != payload.SnapshotId ||
        payload.ActionFlags != 0 || (payload.Type & ~0x7u) != 0 || (payload.Type & 0x3u) == 0x3u ||
        !IsZero(payload.Reserved, sizeof(payload.Reserved)))
        return CommandStatus::Malformed;

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

    const auto mediator = DiscoverActorMediator();
    if (!IsActorMediator(mediator) || !g_originalPerformAction)
        return CommandStatus::Inactive;
    auto* data = RE::TESActionData::Create();
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
    data->flags = (payload.Type & 0x4u) != 0 ? 1u : 0u;

    auto* actorState = actor->AsActorState();
    if (!actorState)
        return CommandStatus::EngineRejected;
    const auto previousState1 = actorState->actorState1;
    const auto previousState2 = actorState->actorState2;
    std::array<bool, AnimationGraphProtocol::kBooleanCount> previousBooleans{};
    std::array<float, AnimationGraphProtocol::kFloatCount> previousFloats{};
    std::array<std::int32_t, AnimationGraphProtocol::kIntegerCount> previousIntegers{};
    if (!CaptureGraph(*actor, previousBooleans, previousFloats, previousIntegers))
        return CommandStatus::EngineRejected;

    const auto rollback = [&]() noexcept {
        actorState->actorState1 = previousState1;
        actorState->actorState2 = previousState2;
        RestoreGraph(*actor, previousBooleans, previousFloats, previousIntegers);
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
    if (g_installed.load(std::memory_order_acquire))
        return true;
    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;
    const auto finish = [](const bool a_result) noexcept {
        g_installing.store(false, std::memory_order_release);
        return a_result;
    };
    try {
        const auto target = REL::Module::get().base() + kSkyrimVrPerformActionRva;
        // The public VR row for desktop ID 38949 resolves to 0x68AE90,
        // which is an unrelated actor float setter. 0x643F20 is the exact VR
        // ActorMediator path: its callers load the singleton at 0x2FEBCB8,
        // pass TESActionData in RDX, and consume the boolean result in AL.
        if (!IsExecutableTarget(target) ||
            std::memcmp(reinterpret_cast<const void*>(target),
                        kSkyrimVrPerformActionPrologue.data(),
                        kSkyrimVrPerformActionPrologue.size()) != 0)
            return finish(false);
        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED)
            return finish(false);
        g_performActionTarget = reinterpret_cast<void*>(target);
        auto status = MH_CreateHook(g_performActionTarget, reinterpret_cast<void*>(&HookPerformAction),
                                    reinterpret_cast<void**>(&g_originalPerformAction));
        if (status != MH_OK) {
            g_performActionTarget = nullptr;
            return finish(false);
        }
        status = MH_EnableHook(g_performActionTarget);
        if (status != MH_OK) {
            MH_RemoveHook(g_performActionTarget);
            g_performActionTarget = nullptr;
            g_originalPerformAction = nullptr;
            return finish(false);
        }
        g_installed.store(true, std::memory_order_release);
        g_semanticallyValidated.store(false, std::memory_order_release);
        const auto mediator = DiscoverActorMediator();
        SKSE::log::info("SkyrimTogetherVRGameplayBridge: installed guarded ActorMediator::PerformAction hook at VR RVA 0x{:X}",
                        kSkyrimVrPerformActionRva);
        if (!mediator)
            SKSE::log::info("SkyrimTogetherVRGameplayBridge: ActorMediator is not uniquely discoverable yet; inbound actions will retry");
        return finish(true);
    } catch (...) {
        return finish(false);
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
    g_actorMediator.store(nullptr, std::memory_order_release);
    g_semanticallyValidated.store(false, std::memory_order_release);
    BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, false);
    g_installed.store(false, std::memory_order_release);
    Reset();
    g_installing.store(false, std::memory_order_release);
}

void Reset() noexcept
{
    g_pendingActions.clear();
    g_nextActionId.store(0, std::memory_order_release);
}

CommandStatus Execute(const CommandRecord& a_command) noexcept
{
    try {
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
