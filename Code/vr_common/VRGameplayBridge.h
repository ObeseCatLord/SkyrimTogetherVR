#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace SkyrimTogetherVR::GameplayBridge
{
// This mapping is shared only by modules in one 64-bit Windows process. Its
// records deliberately contain no native pointers, C++ ownership, callbacks,
// game types, or variable-sized data.
inline constexpr wchar_t kMappingHandleEnvironment[] = L"STVR_GAMEPLAY_BRIDGE_HANDLE";
inline constexpr std::uint32_t kMappingMagic = 0x42564753; // SGVB
inline constexpr std::uint16_t kMappingAbiVersion = 1;
inline constexpr std::uint32_t kCapabilityRevision = 1;
// REL::Version(1, 4, 15, 0).pack(). Keep the ABI header independent of
// CommonLib while using the same documented packing contract.
inline constexpr std::uint32_t kSkyrimVrRuntimeVersion = 0x010400F0;
inline constexpr std::uint16_t kFixedPayloadBytes = 64;
inline constexpr std::uint32_t kDefaultEventRingCapacity = 64;
inline constexpr std::uint32_t kDefaultCommandRingCapacity = 64;

enum class EndpointState : std::uint32_t
{
    Uninitialized = 0,
    Prepared = 1,
    Ready = 2,
    Retiring = 3,
    Retired = 4,
    Faulted = 5,
};

enum class Capability : std::uint64_t
{
    Lifecycle = 1ull << 0,
    LocalPlayerDiscovery = 1ull << 1,
    LocalPlayerSnapshot = 1ull << 2,
    RemoteAvatarLifecycle = 1ull << 3,
    RemoteRootTransform = 1ull << 4,
};

using CapabilityMask = std::uint64_t;

inline constexpr CapabilityMask kInitialCapabilities =
    static_cast<CapabilityMask>(Capability::Lifecycle) |
    static_cast<CapabilityMask>(Capability::LocalPlayerDiscovery) |
    static_cast<CapabilityMask>(Capability::LocalPlayerSnapshot) |
    static_cast<CapabilityMask>(Capability::RemoteAvatarLifecycle) |
    static_cast<CapabilityMask>(Capability::RemoteRootTransform);

[[nodiscard]] constexpr bool HasCapability(const CapabilityMask a_mask, const Capability a_capability) noexcept
{
    return (a_mask & static_cast<CapabilityMask>(a_capability)) != 0;
}

// The adapter owns token allocation and validates the token against the
// identity epoch and generation before resolving it to a native actor.
struct AdapterHandle
{
    std::uint64_t Value;
};

// Zero is invalid. One is reserved for the process-lifetime local player token;
// remote avatar handles are allocated by the adapter from a separate range.
inline constexpr AdapterHandle kLocalPlayerHandle{1};
inline constexpr std::uint64_t kFirstRemoteAvatarHandle = 2;

// All state-changing bridge work is bound to these identities. Future
// protocol-specific fields may be appended only in a new ABI version.
struct BridgeIdentity
{
    std::uint64_t ServerInstanceNonce;
    std::uint64_t ConnectionGeneration;
    std::uint64_t LifecycleEpoch;
    std::uint64_t EntityId;
    std::uint32_t EntityGeneration;
    std::uint32_t Reserved0;
    std::uint64_t SequenceId;
    std::uint64_t ActionId;
};

struct MessageHeader
{
    std::uint16_t Kind;
    std::uint16_t PayloadSize;
    std::uint32_t Flags;
    BridgeIdentity Identity;
};

struct RootTransform
{
    float PositionX;
    float PositionY;
    float PositionZ;
    float RotationX;
    float RotationY;
    float RotationZ;
    float RotationW;
    float Scale;
};

enum class EventKind : std::uint16_t
{
    Lifecycle = 1,
    LocalPlayerState = 2,
    RemoteAvatarState = 3,
};

enum class LifecycleState : std::uint32_t
{
    PluginLoaded = 1,
    DataLoaded = 2,
    NewGame = 3,
    PreLoadGame = 4,
    PostLoadGame = 5,
    CellChanged = 6,
    EpochRetired = 7,
};

enum class EpochRetireReason : std::uint32_t
{
    Disconnect = 1,
    LifecycleReset = 2,
    Shutdown = 3,
};

enum class CommandStatus : std::uint32_t
{
    Success = 0,
    Inactive = 1,
    Unsupported = 2,
    Malformed = 3,
    WrongThread = 4,
    StaleSession = 5,
    StaleEpoch = 6,
    StaleEntity = 7,
    InvalidHandle = 8,
    MissingForm = 9,
    MissingCell = 10,
    EngineRejected = 11,
    QueueOverflow = 12,
};

enum class CommandPumpResult : std::uint32_t
{
    Success = 0,
    Inactive = 1,
    AbiMismatch = 2,
    WrongProcess = 3,
    WrongThread = 4,
    StaleEpoch = 5,
    Faulted = 6,
};

enum class RemoteAvatarState : std::uint32_t
{
    Created = 1,
    Destroyed = 2,
    Rejected = 3,
    Faulted = 4,
};

struct LifecycleEventPayload
{
    std::uint32_t ObservedState;
    std::uint32_t Reason;
    std::uint64_t ObservedLifecycleEpoch;
    CapabilityMask AvailableCapabilities;
    std::uint8_t Reserved[40];
};

struct LocalPlayerStatePayload
{
    AdapterHandle LocalPlayerHandle;
    std::uint32_t LocalCellFormId;
    std::uint32_t LocalWorldspaceFormId;
    RootTransform Root;
    std::uint64_t SnapshotFlags;
    std::uint32_t LocalActorBaseFormId;
    std::uint8_t Reserved[4];
};

// This result is how the mapped client receives an adapter-issued handle for
// subsequent remote-avatar update and destroy commands.
struct RemoteAvatarStatePayload
{
    AdapterHandle AvatarHandle;
    std::uint32_t State;
    std::uint32_t Status;
    std::uint32_t LocalCellFormId;
    std::uint32_t LocalWorldspaceFormId;
    RootTransform Root;
    std::uint8_t Reserved[8];
};

union EventPayload
{
    LifecycleEventPayload Lifecycle;
    LocalPlayerStatePayload LocalPlayerState;
    RemoteAvatarStatePayload RemoteAvatarState;
    std::uint8_t Bytes[kFixedPayloadBytes];
};

struct alignas(8) EventRecord
{
    MessageHeader Header;
    EventPayload Payload;
};

enum class CommandKind : std::uint16_t
{
    CreateRemoteAvatar = 1,
    DestroyRemoteAvatar = 2,
    UpdateRemoteRootTransform = 3,
    RetireEpoch = 4,
};

struct CreateRemoteAvatarPayload
{
    // Form IDs are translated into this client's local load order before the
    // command is submitted. The adapter never interprets server mod IDs.
    std::uint32_t LocalActorBaseFormId;
    std::uint32_t CreateFlags;
    std::uint32_t LocalCellFormId;
    std::uint32_t LocalWorldspaceFormId;
    RootTransform InitialRoot;
    std::uint8_t Reserved[16];
};

struct DestroyRemoteAvatarPayload
{
    AdapterHandle AvatarHandle;
    std::uint32_t DestroyReason;
    std::uint32_t DestroyFlags;
    std::uint8_t Reserved[48];
};

struct UpdateRemoteRootTransformPayload
{
    AdapterHandle AvatarHandle;
    RootTransform Root;
    std::uint32_t UpdateFlags;
    std::uint32_t Reserved0;
    std::uint8_t Reserved[16];
};

struct RetireEpochPayload
{
    std::uint64_t RetiredLifecycleEpoch;
    std::uint32_t Reason;
    std::uint32_t RetireFlags;
    std::uint8_t Reserved[48];
};

union CommandPayload
{
    CreateRemoteAvatarPayload CreateRemoteAvatar;
    DestroyRemoteAvatarPayload DestroyRemoteAvatar;
    UpdateRemoteRootTransformPayload UpdateRemoteRootTransform;
    RetireEpochPayload RetireEpoch;
    std::uint8_t Bytes[kFixedPayloadBytes];
};

struct alignas(8) CommandRecord
{
    MessageHeader Header;
    CommandPayload Payload;
};

using AtomicU32 = std::atomic<std::uint32_t>;
using AtomicU64 = std::atomic<std::uint64_t>;

// The creator placement-constructs the mapping, initializes this header and
// both rings, then publishes EndpointState::Prepared. Ready is published only
// after both owner thread IDs and capabilities are valid. Retiring prevents new
// work; Retired makes all remaining records stale through LifecycleEpoch
// validation.
struct alignas(8) MappingHeader
{
    std::uint32_t Magic;
    std::uint16_t AbiVersion;
    std::uint16_t HeaderSize;
    std::uint32_t MappingSize;
    std::uint32_t PublisherProcessId;
    std::uint32_t RuntimeVersion;
    std::uint32_t CapabilityRevision;
    AtomicU32 State;
    std::uint32_t Reserved0;
    AtomicU64 RequestedCapabilities;
    AtomicU64 AvailableCapabilities;
    AtomicU64 ActiveCapabilities;
    AtomicU64 ServerInstanceNonce;
    AtomicU64 ConnectionGeneration;
    AtomicU64 LifecycleEpoch;
    AtomicU64 EventConsumerThreadId;
    AtomicU64 CommandExecutionThreadId;
    AtomicU64 ProducedEventCount;
    AtomicU64 ConsumedEventCount;
    AtomicU64 SubmittedCommandCount;
    AtomicU64 ExecutedCommandCount;
    AtomicU64 RejectedCommandCount;
    AtomicU64 StaleCommandCount;
};

template <class T>
struct alignas(8) RingSlot
{
    AtomicU64 Sequence;
    T Record;
};

// A bounded, lock-free MPMC queue using one monotonically increasing sequence
// per slot. Any callback thread may push events, and any explicitly-authorized
// command owner may push commands. Consumers may also be multiple threads,
// although the gameplay adapter must execute game mutations only on its
// CommandExecutionThreadId. InitializeRing is exclusive: no producer or consumer
// may touch a ring until it returns, nor while an endpoint is retired/reset.
template <class T, std::size_t Capacity>
struct alignas(8) BoundedMpmcRing
{
    static_assert(Capacity >= 2);
    static_assert((Capacity & (Capacity - 1)) == 0);
    static_assert(std::is_standard_layout_v<T>);
    static_assert(std::is_trivially_copyable_v<T>);

    AtomicU64 EnqueuePosition;
    AtomicU64 DequeuePosition;
    AtomicU64 DroppedPushCount;
    AtomicU64 EmptyPopCount;
    RingSlot<T> Slots[Capacity];
};

template <class T, std::size_t Capacity>
inline void InitializeRing(BoundedMpmcRing<T, Capacity>& a_ring) noexcept
{
    a_ring.EnqueuePosition.store(0, std::memory_order_relaxed);
    a_ring.DequeuePosition.store(0, std::memory_order_relaxed);
    a_ring.DroppedPushCount.store(0, std::memory_order_relaxed);
    a_ring.EmptyPopCount.store(0, std::memory_order_relaxed);

    for (std::size_t i = 0; i < Capacity; ++i)
        a_ring.Slots[i].Sequence.store(i, std::memory_order_relaxed);
}

template <class T, std::size_t Capacity>
[[nodiscard]] inline bool TryPush(BoundedMpmcRing<T, Capacity>& a_ring, const T& a_record) noexcept
{
    std::uint64_t position = a_ring.EnqueuePosition.load(std::memory_order_relaxed);
    RingSlot<T>* pSlot{};

    for (;;)
    {
        pSlot = &a_ring.Slots[position & (Capacity - 1)];
        const auto sequence = pSlot->Sequence.load(std::memory_order_acquire);
        const auto difference = static_cast<std::int64_t>(sequence - position);

        if (difference == 0)
        {
            if (a_ring.EnqueuePosition.compare_exchange_weak(
                    position, position + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                break;
        }
        else if (difference < 0)
        {
            a_ring.DroppedPushCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        else
            position = a_ring.EnqueuePosition.load(std::memory_order_relaxed);
    }

    pSlot->Record = a_record;
    pSlot->Sequence.store(position + 1, std::memory_order_release);
    return true;
}

template <class T, std::size_t Capacity>
[[nodiscard]] inline bool TryPop(BoundedMpmcRing<T, Capacity>& a_ring, T& a_record) noexcept
{
    std::uint64_t position = a_ring.DequeuePosition.load(std::memory_order_relaxed);
    RingSlot<T>* pSlot{};

    for (;;)
    {
        pSlot = &a_ring.Slots[position & (Capacity - 1)];
        const auto sequence = pSlot->Sequence.load(std::memory_order_acquire);
        const auto difference = static_cast<std::int64_t>(sequence - (position + 1));

        if (difference == 0)
        {
            if (a_ring.DequeuePosition.compare_exchange_weak(
                    position, position + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                break;
        }
        else if (difference < 0)
        {
            a_ring.EmptyPopCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        else
            position = a_ring.DequeuePosition.load(std::memory_order_relaxed);
    }

    a_record = pSlot->Record;
    pSlot->Sequence.store(position + Capacity, std::memory_order_release);
    return true;
}

using EventRing = BoundedMpmcRing<EventRecord, kDefaultEventRingCapacity>;
using CommandRing = BoundedMpmcRing<CommandRecord, kDefaultCommandRingCapacity>;

struct alignas(8) GameplayBridgeMapping
{
    MappingHeader Header;
    EventRing Events;
    CommandRing Commands;
};

static_assert(sizeof(std::uint64_t) == 8);
static_assert(sizeof(float) == 4);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(sizeof(AtomicU32) == 4);
static_assert(sizeof(AtomicU64) == 8);
static_assert(alignof(AtomicU32) == 4);
static_assert(alignof(AtomicU64) == 8);
static_assert(AtomicU32::is_always_lock_free);
static_assert(AtomicU64::is_always_lock_free);
static_assert(std::is_standard_layout_v<AtomicU32>);
static_assert(std::is_standard_layout_v<AtomicU64>);

// The mapped synchronization objects are placement-constructed once and are
// never copied. Their cross-view contract is deliberately pinned to Windows
// x64/MSVC and guarded by the lock-free, size, alignment, and layout checks
// below; MSVC correctly does not classify std::atomic as trivially copyable.

static_assert(std::is_standard_layout_v<AdapterHandle>);
static_assert(std::is_trivially_copyable_v<AdapterHandle>);
static_assert(sizeof(AdapterHandle) == 0x08);
static_assert(std::is_standard_layout_v<BridgeIdentity>);
static_assert(std::is_trivially_copyable_v<BridgeIdentity>);
static_assert(sizeof(BridgeIdentity) == 0x38);
static_assert(offsetof(BridgeIdentity, LifecycleEpoch) == 0x10);
static_assert(offsetof(BridgeIdentity, EntityId) == 0x18);
static_assert(offsetof(BridgeIdentity, SequenceId) == 0x28);
static_assert(offsetof(BridgeIdentity, ActionId) == 0x30);
static_assert(sizeof(MessageHeader) == 0x40);
static_assert(alignof(MessageHeader) == 8);
static_assert(offsetof(MessageHeader, Identity) == 0x08);
static_assert(sizeof(RootTransform) == 0x20);
static_assert(alignof(RootTransform) == 4);
static_assert(std::is_standard_layout_v<LifecycleEventPayload>);
static_assert(std::is_trivially_copyable_v<LifecycleEventPayload>);
static_assert(std::is_standard_layout_v<LocalPlayerStatePayload>);
static_assert(std::is_trivially_copyable_v<LocalPlayerStatePayload>);
static_assert(std::is_standard_layout_v<RemoteAvatarStatePayload>);
static_assert(std::is_trivially_copyable_v<RemoteAvatarStatePayload>);
static_assert(std::is_standard_layout_v<CreateRemoteAvatarPayload>);
static_assert(std::is_trivially_copyable_v<CreateRemoteAvatarPayload>);
static_assert(std::is_standard_layout_v<DestroyRemoteAvatarPayload>);
static_assert(std::is_trivially_copyable_v<DestroyRemoteAvatarPayload>);
static_assert(std::is_standard_layout_v<UpdateRemoteRootTransformPayload>);
static_assert(std::is_trivially_copyable_v<UpdateRemoteRootTransformPayload>);
static_assert(std::is_standard_layout_v<RetireEpochPayload>);
static_assert(std::is_trivially_copyable_v<RetireEpochPayload>);
static_assert(sizeof(LifecycleEventPayload) == kFixedPayloadBytes);
static_assert(sizeof(LocalPlayerStatePayload) == kFixedPayloadBytes);
static_assert(sizeof(RemoteAvatarStatePayload) == kFixedPayloadBytes);
static_assert(sizeof(CreateRemoteAvatarPayload) == kFixedPayloadBytes);
static_assert(sizeof(DestroyRemoteAvatarPayload) == kFixedPayloadBytes);
static_assert(sizeof(UpdateRemoteRootTransformPayload) == kFixedPayloadBytes);
static_assert(sizeof(RetireEpochPayload) == kFixedPayloadBytes);
static_assert(sizeof(EventRecord) == 0x80);
static_assert(sizeof(CommandRecord) == 0x80);
static_assert(alignof(EventRecord) == 8);
static_assert(alignof(CommandRecord) == 8);
static_assert(offsetof(EventRecord, Header) == 0x00);
static_assert(offsetof(EventRecord, Payload) == 0x40);
static_assert(offsetof(CommandRecord, Header) == 0x00);
static_assert(offsetof(CommandRecord, Payload) == 0x40);
static_assert(std::is_standard_layout_v<EventRecord>);
static_assert(std::is_trivially_copyable_v<EventRecord>);
static_assert(std::is_standard_layout_v<CommandRecord>);
static_assert(std::is_trivially_copyable_v<CommandRecord>);
static_assert(sizeof(MappingHeader) == 0x90);
static_assert(alignof(MappingHeader) == 8);
static_assert(offsetof(MappingHeader, State) == 0x18);
static_assert(offsetof(MappingHeader, RequestedCapabilities) == 0x20);
static_assert(offsetof(MappingHeader, AvailableCapabilities) == 0x28);
static_assert(offsetof(MappingHeader, ActiveCapabilities) == 0x30);
static_assert(offsetof(MappingHeader, LifecycleEpoch) == 0x48);
static_assert(offsetof(MappingHeader, CommandExecutionThreadId) == 0x58);
static_assert(offsetof(MappingHeader, RejectedCommandCount) == 0x80);
static_assert(sizeof(RingSlot<EventRecord>) == 0x88);
static_assert(sizeof(RingSlot<CommandRecord>) == 0x88);
static_assert(offsetof(EventRing, Slots) == 0x20);
static_assert(offsetof(CommandRing, Slots) == 0x20);
static_assert(sizeof(EventRing) == 0x2220);
static_assert(sizeof(CommandRing) == 0x2220);
static_assert(alignof(EventRing) == 8);
static_assert(alignof(CommandRing) == 8);
static_assert(std::is_standard_layout_v<MappingHeader>);
static_assert(std::is_standard_layout_v<EventRing>);
static_assert(std::is_standard_layout_v<CommandRing>);
static_assert(sizeof(GameplayBridgeMapping) == 0x44D0);
static_assert(alignof(GameplayBridgeMapping) == 8);
static_assert(offsetof(GameplayBridgeMapping, Events) == 0x90);
static_assert(offsetof(GameplayBridgeMapping, Commands) == 0x22B0);
static_assert(std::is_standard_layout_v<GameplayBridgeMapping>);
} // namespace SkyrimTogetherVR::GameplayBridge
