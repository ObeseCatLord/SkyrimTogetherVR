#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Fixed-size, versioned POD-only boundary between the client and PLANCK bridge.
namespace SkyrimTogetherVR::PlanckBridge
{
inline constexpr std::uint32_t kAbiRevision = 3;
inline constexpr std::uint32_t kPlanckInterfaceRevision = 2;
inline constexpr std::uint32_t kNodeNameCapacity = 64;
inline constexpr std::uint32_t kCapabilitiesSize = 32;
inline constexpr std::uint32_t kLocalEventSize = 184;
inline constexpr std::uint32_t kRemoteEventSize = 192;
inline constexpr std::uint64_t kRequiredFeatures =
    (1ull << 0) | (1ull << 1) | (1ull << 2) | (1ull << 3) | (1ull << 4);

inline constexpr char kGetCapabilitiesExport[] = "SkyrimTogetherVR_Planck002_GetCapabilities";
inline constexpr char kDequeueLocalEventExport[] = "SkyrimTogetherVR_Planck002_DequeueLocalEvent";
inline constexpr char kSubmitRemoteEventExport[] = "SkyrimTogetherVR_Planck002_SubmitRemoteEvent";
inline constexpr char kClearRemoteSessionExport[] = "SkyrimTogetherVR_Planck002_ClearRemoteSession";
inline constexpr char kDiscardLocalEventsExport[] = "SkyrimTogetherVR_Planck002_DiscardLocalEvents";

enum class Result : std::uint32_t
{
    Accepted,
    Empty,
    Rejected,
    Busy,
    Unavailable,
};

// PLANCK interface002 result ordinals. The bridge owns the client-visible
// result contract, so queue pressure can be retryable without making every
// non-accepted submission terminal.
inline constexpr std::uint32_t kPlanckResultAccepted = 0;
inline constexpr std::uint32_t kPlanckResultEmpty = 1;
inline constexpr std::uint32_t kPlanckResultInvalidRequest = 2;
inline constexpr std::uint32_t kPlanckResultDuplicate = 3;
inline constexpr std::uint32_t kPlanckResultQueueFull = 4;
inline constexpr std::uint32_t kPlanckResultUnsupported = 5;

[[nodiscard]] constexpr bool IsPlanckQueuePressure(const std::uint32_t aCode) noexcept
{
    return aCode == kPlanckResultQueueFull;
}

[[nodiscard]] constexpr bool IsTerminalPlanckSubmission(const std::uint32_t aCode) noexcept
{
    return !IsPlanckQueuePressure(aCode) &&
           (aCode == kPlanckResultInvalidRequest || aCode == kPlanckResultDuplicate ||
               aCode == kPlanckResultUnsupported);
}

[[nodiscard]] constexpr Result MapPlanckSubmitResult(const std::uint32_t aCode) noexcept
{
    if (aCode == kPlanckResultAccepted)
        return Result::Accepted;
    if (aCode == kPlanckResultEmpty)
        return Result::Empty;
    if (IsPlanckQueuePressure(aCode))
        return Result::Busy;
    return Result::Rejected;
}

enum class EventKind : std::uint8_t
{
    HitImpulse = 1,
    RagdollEnter,
    RagdollExit,
    GripBegin,
    GripUpdate,
    GripEnd,
};

struct Vector3
{
    float X{0.0F};
    float Y{0.0F};
    float Z{0.0F};
};

struct Quaternion
{
    float X{0.0F};
    float Y{0.0F};
    float Z{0.0F};
    float W{1.0F};
};

struct Capabilities
{
    std::uint32_t Size{kCapabilitiesSize};
    std::uint32_t AbiRevision{0};
    std::uint32_t InterfaceRevision{0};
    std::uint32_t Reserved{0};
    std::uint64_t Features{0};
    std::uint64_t BridgeEpoch{0};
};

struct LocalEvent
{
    std::uint32_t Size{kLocalEventSize};
    std::uint32_t TargetFormId{0};
    std::uint64_t EventId{0};
    Vector3 Position{};
    Vector3 Velocity{};
    std::uint32_t Flags{0};
    char NodeName[kNodeNameCapacity]{};
    EventKind Kind{};
    std::uint8_t Reserved[3]{};
    std::uint64_t GripId{0};
    Quaternion Rotation{};
    Vector3 LinearVelocity{};
    Vector3 AngularVelocity{};
    Vector3 SourcePosition{};
    float ImpulseMultiplier{0.0F};
    float TtlSeconds{0.0F};
};

struct RemoteEvent
{
    std::uint32_t Size{kRemoteEventSize};
    EventKind Kind{};
    std::uint8_t Reserved0[3]{};
    std::uint64_t SourceSession{0};
    std::uint64_t EventId{0};
    std::uint64_t GripId{0};
    std::uint32_t TargetFormId{0};
    std::uint32_t NodeNameLength{0};
    char NodeName[kNodeNameCapacity]{};
    Vector3 Position{};
    Vector3 Velocity{};
    Vector3 SourcePosition{};
    Vector3 LinearVelocity{};
    Vector3 AngularVelocity{};
    Quaternion Rotation{};
    float ImpulseMultiplier{0.0F};
    float TtlSeconds{0.0F};
};

using GetCapabilitiesFn = Result (*)(Capabilities*) noexcept;
using DequeueLocalEventFn = Result (*)(LocalEvent*) noexcept;
using SubmitRemoteEventFn = Result (*)(const RemoteEvent*) noexcept;
using ClearRemoteSessionFn = Result (*)(std::uint64_t, std::uint64_t) noexcept;
using DiscardLocalEventsFn = Result (*)(std::uint64_t) noexcept;

[[nodiscard]] constexpr bool IsCompatibleCapabilities(const Capabilities& acCapabilities) noexcept
{
    return acCapabilities.Size == kCapabilitiesSize && acCapabilities.AbiRevision == kAbiRevision &&
           acCapabilities.InterfaceRevision == kPlanckInterfaceRevision && acCapabilities.Reserved == 0 &&
           acCapabilities.BridgeEpoch != 0 &&
           (acCapabilities.Features & kRequiredFeatures) == kRequiredFeatures;
}

[[nodiscard]] constexpr bool ShouldResetWireProducerIdentity(
    const std::uint64_t aObservedServerNonce, const std::uint64_t aObservedConnectionGeneration,
    const std::uint64_t aServerNonce, const std::uint64_t aConnectionGeneration) noexcept
{
    return aObservedServerNonce != aServerNonce ||
           aObservedConnectionGeneration != aConnectionGeneration;
}

[[nodiscard]] constexpr bool CanTransmitLifecycleFencedEvent(
    const bool aAdmissionFailClosed, const std::uint64_t aAdmittedLifecycleGeneration,
    const std::uint64_t aCurrentLifecycleGeneration, const std::uint64_t aWireProducerEpoch) noexcept
{
    return !aAdmissionFailClosed && aCurrentLifecycleGeneration != 0 &&
           aAdmittedLifecycleGeneration == aCurrentLifecycleGeneration && aWireProducerEpoch != 0;
}

[[nodiscard]] constexpr bool CanRetainPendingClear(
    const std::size_t aPendingCount, const std::size_t aMaximumPendingCount) noexcept
{
    return aPendingCount < aMaximumPendingCount;
}

// A replacement producer must wait until the old token is accepted for
// clearing.  Local gameplay generations deliberately do not participate in
// this authenticated replay/token identity.
[[nodiscard]] constexpr bool CanAdmitReplacementProducer(
    const bool aOldTokenClearPending, const std::uint64_t aServerNonce,
    const std::uint64_t aConnectionGeneration, const std::uint32_t aPlayerId,
    const std::uint64_t aProducerEpoch) noexcept
{
    return !aOldTokenClearPending && aServerNonce != 0 &&
           aConnectionGeneration != 0 && aPlayerId != 0 && aProducerEpoch != 0;
}

static_assert(std::is_standard_layout_v<Vector3>);
static_assert(std::is_trivially_copyable_v<Vector3>);
static_assert(std::is_standard_layout_v<Quaternion>);
static_assert(std::is_trivially_copyable_v<Quaternion>);
static_assert(std::is_standard_layout_v<Capabilities>);
static_assert(std::is_trivially_copyable_v<Capabilities>);
static_assert(std::is_standard_layout_v<LocalEvent>);
static_assert(std::is_trivially_copyable_v<LocalEvent>);
static_assert(std::is_standard_layout_v<RemoteEvent>);
static_assert(std::is_trivially_copyable_v<RemoteEvent>);
static_assert(sizeof(Vector3) == 12);
static_assert(sizeof(Quaternion) == 16);
static_assert(sizeof(Capabilities) == kCapabilitiesSize);
static_assert(offsetof(Capabilities, Features) == 0x10);
static_assert(offsetof(Capabilities, BridgeEpoch) == 0x18);
static_assert(sizeof(LocalEvent) == kLocalEventSize);
static_assert(offsetof(LocalEvent, EventId) == 0x08);
static_assert(offsetof(LocalEvent, NodeName) == 0x2C);
static_assert(offsetof(LocalEvent, Kind) == 0x6C);
static_assert(offsetof(LocalEvent, GripId) == 0x70);
static_assert(offsetof(LocalEvent, Rotation) == 0x78);
static_assert(offsetof(LocalEvent, SourcePosition) == 0xA0);
static_assert(offsetof(LocalEvent, TtlSeconds) == 0xB0);
static_assert(sizeof(RemoteEvent) == kRemoteEventSize);
static_assert(offsetof(RemoteEvent, SourceSession) == 0x08);
static_assert(offsetof(RemoteEvent, NodeName) == 0x28);
static_assert(offsetof(RemoteEvent, Position) == 0x68);
static_assert(offsetof(RemoteEvent, Rotation) == 0xA4);
static_assert(offsetof(RemoteEvent, TtlSeconds) == 0xB8);
static_assert(std::is_same_v<GetCapabilitiesFn, Result (*)(Capabilities*) noexcept>);
static_assert(std::is_same_v<DequeueLocalEventFn, Result (*)(LocalEvent*) noexcept>);
static_assert(std::is_same_v<SubmitRemoteEventFn, Result (*)(const RemoteEvent*) noexcept>);
static_assert(std::is_same_v<ClearRemoteSessionFn, Result (*)(std::uint64_t, std::uint64_t) noexcept>);
static_assert(std::is_same_v<DiscardLocalEventsFn, Result (*)(std::uint64_t) noexcept>);
} // namespace SkyrimTogetherVR::PlanckBridge
