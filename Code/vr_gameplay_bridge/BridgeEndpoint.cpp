#include "BridgeEndpoint.h"

#include <cerrno>
#include <cstdlib>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr CapabilityMask kAvailableCapabilities =
    static_cast<CapabilityMask>(Capability::Lifecycle) |
    static_cast<CapabilityMask>(Capability::LocalPlayerDiscovery) |
    static_cast<CapabilityMask>(Capability::LocalPlayerSnapshot) |
    static_cast<CapabilityMask>(Capability::RemoteAvatarLifecycle) |
    static_cast<CapabilityMask>(Capability::RemoteRootTransform) |
    static_cast<CapabilityMask>(Capability::RemoteSpatialTransfer) |
    static_cast<CapabilityMask>(Capability::LocalAnimationGraphSnapshot) |
    static_cast<CapabilityMask>(Capability::RemoteAnimationGraphSnapshot) |
    static_cast<CapabilityMask>(Capability::AnimationEvents) |
    static_cast<CapabilityMask>(Capability::Appearance) |
    static_cast<CapabilityMask>(Capability::EquipmentAndInventory) |
    static_cast<CapabilityMask>(Capability::ActorState) |
    static_cast<CapabilityMask>(Capability::WorldReferences) |
    static_cast<CapabilityMask>(Capability::CombatAndMagic) |
    static_cast<CapabilityMask>(Capability::QuestAndDialogue) |
    static_cast<CapabilityMask>(Capability::QuestMutation) |
    static_cast<CapabilityMask>(Capability::WorldState) |
    static_cast<CapabilityMask>(Capability::VrBodyPose) |
    static_cast<CapabilityMask>(Capability::HiggsInteraction) |
    static_cast<CapabilityMask>(Capability::NpcOwnership) |
    static_cast<CapabilityMask>(Capability::AssignmentBootstrap) |
    static_cast<CapabilityMask>(Capability::InventoryStackTransactions);
constexpr CapabilityMask kOptionalCapabilities =
    static_cast<CapabilityMask>(Capability::ExactAnimationActions) |
    static_cast<CapabilityMask>(Capability::LocalEventSinks) |
    static_cast<CapabilityMask>(Capability::LocalCaptureSinks);
constexpr CapabilityMask kAllowedCapabilities = kAvailableCapabilities | kOptionalCapabilities;

[[nodiscard]] bool ParseMappingHandle(HANDLE& a_handle) noexcept
{
    wchar_t text[2 + sizeof(std::uintptr_t) * 2 + 1]{};
    constexpr auto textCount = sizeof(text) / sizeof(text[0]);
    const auto length = GetEnvironmentVariableW(kMappingHandleEnvironment, text, textCount);
    if (length < 3 || length >= textCount || text[0] != L'0' || (text[1] != L'x' && text[1] != L'X'))
        return false;

    errno = 0;
    wchar_t* end{};
    const auto value = std::wcstoull(text + 2, &end, 16);
    if (errno != 0 || !end || *end != L'\0' || value == 0 || value > std::numeric_limits<std::uintptr_t>::max())
        return false;

    a_handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
    return true;
}

void LogError(const char* a_message) noexcept
{
    try {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: {}", a_message);
    } catch (...) {
    }
}

void LogRejectedEventBatch(const char* a_reason, const std::size_t a_count, const std::uint64_t a_total) noexcept
{
    try {
        SKSE::log::warn(
            "SkyrimTogetherVRGameplayBridge: rejected complete event batch ({} records, aggregate {}): {}",
            a_count, a_total, a_reason);
    } catch (...) {
    }
}

class EventPublicationScope
{
public:
    explicit EventPublicationScope(bool& ar_active) noexcept
        : _active(ar_active)
    {
        if (!_active) {
            _active = true;
            _entered = true;
        }
    }

    ~EventPublicationScope() noexcept
    {
        if (_entered)
            _active = false;
    }

    [[nodiscard]] bool Entered() const noexcept { return _entered; }

private:
    bool& _active;
    bool _entered{};
};
} // namespace

BridgeEndpoint::CommandResultReservation::~CommandResultReservation() noexcept
{
    if (_endpoint)
        _endpoint->ReleaseCommandResultReservation(*this);
}

bool BridgeEndpoint::CommandResultReservation::Commit(const EventRecord& a_record) noexcept
{
    return _endpoint && _endpoint->CommitCommandResultEvent(*this, a_record);
}

BridgeEndpoint& BridgeEndpoint::Get() noexcept
{
    static BridgeEndpoint endpoint;
    return endpoint;
}

bool BridgeEndpoint::Attach() noexcept
{
    bool expected = false;
    if (!_attachAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return _mapping != nullptr && ValidateMapping();

    HANDLE mappingHandle{};
    if (!ParseMappingHandle(mappingHandle)) {
        LogError("missing or malformed endpoint handle");
        return false;
    }

    auto* mapping = static_cast<GameplayBridgeMapping*>(
        MapViewOfFile(mappingHandle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(GameplayBridgeMapping)));
    if (!mapping) {
        LogError("endpoint map failed");
        return false;
    }

    _mapping = mapping;
    if (!ValidateMapping()) {
        Fault("endpoint ABI validation failed");
        return false;
    }

    PublishCapabilities();
    return true;
}

bool BridgeEndpoint::IsAttached() const noexcept
{
    return _mapping != nullptr;
}

GameplayBridgeMapping* BridgeEndpoint::Mapping() const noexcept
{
    return _mapping;
}

bool BridgeEndpoint::IsOperational() const noexcept
{
    if (!ValidateMapping())
        return false;

    const auto state = static_cast<EndpointState>(_mapping->Header.State.load(std::memory_order_acquire));
    return IsOperationalEndpointState(state);
}

CommandPumpResult BridgeEndpoint::ValidateCommandPump(
    const std::uint32_t a_callerProcessId,
    const std::uint32_t a_callerThreadId,
    const std::uint64_t a_lifecycleEpoch) noexcept
{
    if (!IsAttached())
        return CommandPumpResult::Inactive;
    if (a_callerProcessId != GetCurrentProcessId())
        return CommandPumpResult::WrongProcess;
    if (a_callerThreadId == 0 || a_callerThreadId != GetCurrentThreadId())
        return CommandPumpResult::WrongThread;
    if (!ValidateMapping()) {
        Fault("endpoint ABI validation failed while pumping commands");
        return CommandPumpResult::AbiMismatch;
    }

    auto& header = _mapping->Header;
    const auto state = static_cast<EndpointState>(header.State.load(std::memory_order_acquire));
    if (state == EndpointState::Faulted)
        return CommandPumpResult::Faulted;
    if (state == EndpointState::Retiring || state == EndpointState::Retired || state == EndpointState::Uninitialized)
        return CommandPumpResult::Inactive;
    if (a_lifecycleEpoch != header.LifecycleEpoch.load(std::memory_order_acquire))
        return CommandPumpResult::StaleEpoch;

    std::uint64_t expectedThread = 0;
    if (!header.CommandExecutionThreadId.compare_exchange_strong(
            expectedThread,
            a_callerThreadId,
            std::memory_order_release,
            std::memory_order_acquire) &&
        expectedThread != a_callerThreadId)
        return CommandPumpResult::WrongThread;

    PublishCapabilities();
    if (header.EventConsumerThreadId.load(std::memory_order_acquire) != 0)
        header.State.store(static_cast<std::uint32_t>(EndpointState::Ready), std::memory_order_release);
    return CommandPumpResult::Success;
}

bool BridgeEndpoint::TryPushEvent(const EventRecord& a_record) noexcept
{
    return TryPushEvents(&a_record, 1);
}

bool BridgeEndpoint::TryPushEvents(const EventRecord* ap_records, const std::size_t a_count) noexcept
{
    if (!IsOperational() || !ap_records || a_count == 0 || a_count > kDefaultEventRingCapacity)
        return false;

    const std::scoped_lock lock{_eventPublicationLock};
    EventPublicationScope publication{_eventPublicationActive};
    if (!publication.Entered()) {
        Fault("reentrant event batch publication");
        return false;
    }
    if (!IsOperational())
        return false;

    WorkAttribution attribution{};
    if (!TryClassifyEventBatchIdentity(ap_records, a_count, attribution)) {
        RecordRejectedEventBatch("unstable or mixed lifecycle identity", a_count);
        return false;
    }
    // Producers can still be called during session teardown. Suppress stale
    // work only as a complete logical batch; never make a transaction look
    // partially accepted by filtering individual records.
    if (attribution != WorkAttribution::Current)
        return true;

    return TryPublishEventsLocked(ap_records, a_count);
}

bool BridgeEndpoint::TryPublishEventsLocked(
    const EventRecord* ap_records,
    const std::size_t a_count) noexcept
{
    if (!FlushPendingEventsLocked() && !IsOperational())
        return false;
    if (_pendingEventCount == 0) {
        const auto enqueue = _mapping->Events.EnqueuePosition.load(std::memory_order_acquire);
        const auto dequeue = _mapping->Events.DequeuePosition.load(std::memory_order_acquire);
        if (enqueue - dequeue <= kDefaultEventRingCapacity - a_count) {
            const auto pushed = a_count == 1 ?
                TryPush(_mapping->Events, ap_records[0]) :
                TryPushBatch(_mapping->Events, ap_records, a_count);
            if (pushed) {
                _mapping->Header.ProducedEventCount.fetch_add(a_count, std::memory_order_relaxed);
                return true;
            }
            Fault("event ring publication failed after capacity admission");
            return false;
        }
    }

    if (!QueuePendingEventsLocked(ap_records, a_count)) {
        _pendingEventBacklogOverflowCount.fetch_add(1, std::memory_order_relaxed);
        RecordRejectedEventBatch("event backlog lacks space for the complete transaction", a_count);
        return false;
    }
    return true;
}

bool BridgeEndpoint::FlushPendingEvents() noexcept
{
    if (!IsOperational())
        return false;
    const std::scoped_lock lock{_eventPublicationLock};
    EventPublicationScope publication{_eventPublicationActive};
    if (!publication.Entered()) {
        Fault("reentrant pending-event flush");
        return false;
    }
    return FlushPendingEventsLocked();
}

bool BridgeEndpoint::TryClassifyEventBatchIdentity(
    const EventRecord* ap_records,
    const std::size_t a_count,
    WorkAttribution& ar_attribution) const noexcept
{
    if (!BridgeBatchPolicy::HasSingleBatchIdentity(ap_records, a_count) || !_mapping)
        return false;
    SessionIdentitySnapshot session{};
    if (!TrySnapshotSessionIdentity(_mapping->Header, session))
        return false;
    const auto state = static_cast<EndpointState>(_mapping->Header.State.load(std::memory_order_acquire));
    const auto epoch = _mapping->Header.LifecycleEpoch.load(std::memory_order_acquire);
    const auto attribution = ClassifyWorkAttribution(state, session, epoch, ap_records[0].Header.Identity);
    for (std::size_t index = 1; index < a_count; ++index) {
        if (ClassifyWorkAttribution(state, session, epoch, ap_records[index].Header.Identity) != attribution)
            return false;
    }
    ar_attribution = attribution;
    return true;
}

bool BridgeEndpoint::FlushPendingEventsLocked() noexcept
{
    if (!_mapping)
        return false;
    while (_pendingEventCount != 0) {
        const auto& front = PendingEventAt(0);
        const auto batchSize = static_cast<std::size_t>(front.BatchSize);
        if (batchSize == 0 || batchSize > _pendingEventCount || batchSize > kDefaultEventRingCapacity) {
            Fault("pending event batch metadata corruption");
            return false;
        }
        for (std::size_t index = 0; index < batchSize; ++index) {
            const auto& entry = PendingEventAt(index);
            if (entry.BatchId != front.BatchId || entry.BatchSize != front.BatchSize) {
                Fault("pending event batch boundary corruption");
                return false;
            }
            _eventBatchScratch[index] = entry.Record;
        }

        if (!BridgeBatchPolicy::HasSingleBatchIdentity(_eventBatchScratch.data(), batchSize)) {
            RecordRejectedEventBatch("retained batch has mixed lifecycle identity", batchSize);
            PopPendingEventBatch(batchSize);
            continue;
        }
        WorkAttribution attribution{};
        if (!TryClassifyEventBatchIdentity(_eventBatchScratch.data(), batchSize, attribution))
            return false;
        if (attribution != WorkAttribution::Current) {
            RecordRejectedEventBatch("retained batch belongs to a retired lifecycle", batchSize);
            PopPendingEventBatch(batchSize);
            continue;
        }

        const auto enqueue = _mapping->Events.EnqueuePosition.load(std::memory_order_acquire);
        const auto dequeue = _mapping->Events.DequeuePosition.load(std::memory_order_acquire);
        if (enqueue - dequeue > kDefaultEventRingCapacity - batchSize)
            return false;
        if (!TryPushBatch(_mapping->Events, _eventBatchScratch.data(), batchSize))
            return false;
        PopPendingEventBatch(batchSize);
        _mapping->Header.ProducedEventCount.fetch_add(batchSize, std::memory_order_relaxed);
    }
    return true;
}

bool BridgeEndpoint::QueuePendingEventsLocked(const EventRecord* ap_records, const std::size_t a_count) noexcept
{
    // Coalescing is deliberately restricted to one-record batches. Removing a
    // member from any larger batch would silently split a destructive protocol
    // transaction while it waits for ring capacity.
    if (a_count == 1)
        EraseSubsumedPendingEvents(ap_records[0]);
    if (a_count > kDefaultEventRingCapacity - _pendingEventCount)
        return false;

    auto batchId = _nextPendingEventBatchId++;
    if (batchId == 0)
        batchId = _nextPendingEventBatchId++;
    const auto batchSize = static_cast<std::uint16_t>(a_count);
    for (std::size_t index = 0; index < a_count; ++index) {
        auto& pending = PendingEventAt(_pendingEventCount + index);
        pending = {ap_records[index], batchId, batchSize};
    }
    _pendingEventCount += a_count;
    return true;
}

BridgeEndpoint::PendingEventRecord& BridgeEndpoint::PendingEventAt(const std::size_t a_offset) noexcept
{
    return _pendingEvents[(_pendingEventHead + a_offset) % kDefaultEventRingCapacity];
}

const BridgeEndpoint::PendingEventRecord& BridgeEndpoint::PendingEventAt(const std::size_t a_offset) const noexcept
{
    return _pendingEvents[(_pendingEventHead + a_offset) % kDefaultEventRingCapacity];
}

void BridgeEndpoint::PopPendingEventBatch(const std::size_t a_count) noexcept
{
    _pendingEventHead = (_pendingEventHead + a_count) % kDefaultEventRingCapacity;
    _pendingEventCount -= a_count;
}

void BridgeEndpoint::EraseSubsumedPendingEvents(const EventRecord& ac_incoming) noexcept
{
    std::size_t retained{};
    for (std::size_t index = 0; index < _pendingEventCount; ++index) {
        const auto& pending = PendingEventAt(index);
        const auto subsumed = BridgeBatchPolicy::CanCoalescePendingBatch(pending.BatchSize, 1) &&
            IsSubsumedByIncoming(pending.Record, &ac_incoming, 1);
        if (subsumed)
            continue;
        if (retained != index)
            PendingEventAt(retained) = pending;
        ++retained;
    }
    _pendingEventCount = retained;
}

void BridgeEndpoint::RecordRejectedEventBatch(const char* a_reason, const std::size_t a_count) noexcept
{
    const auto total = _rejectedEventBatchCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (BridgeBatchPolicy::ShouldLogAggregate(total))
        LogRejectedEventBatch(a_reason, a_count, total);
}

bool BridgeEndpoint::IsSubsumedByIncoming(
    const EventRecord& a_pending, const EventRecord* ap_incoming, const std::size_t a_incomingCount) noexcept
{
    for (std::size_t index = 0; index < a_incomingCount; ++index) {
        const auto& incoming = ap_incoming[index];
        const auto& pendingIdentity = a_pending.Header.Identity;
        const auto& incomingIdentity = incoming.Header.Identity;
        if (pendingIdentity.ServerInstanceNonce != incomingIdentity.ServerInstanceNonce ||
            pendingIdentity.ConnectionGeneration != incomingIdentity.ConnectionGeneration ||
            pendingIdentity.LifecycleEpoch != incomingIdentity.LifecycleEpoch)
            continue;

        const auto pendingKind = static_cast<EventKind>(a_pending.Header.Kind);
        const auto incomingKind = static_cast<EventKind>(incoming.Header.Kind);
        if (pendingKind == EventKind::LocalPlayerState && incomingKind == EventKind::LocalPlayerState)
            return true;
        if (pendingKind != EventKind::LocalAnimationGraphChunk || incomingKind != EventKind::LocalAnimationGraphChunk)
            continue;
        const auto& pending = a_pending.Payload.LocalAnimationGraphChunk;
        const auto& next = incoming.Payload.LocalAnimationGraphChunk;
        if (pending.AvatarHandle.Value == next.AvatarHandle.Value && pending.SnapshotId < next.SnapshotId)
            return true;
    }
    return false;
}

bool BridgeEndpoint::TryReserveCommandResultEvents(
    const std::size_t a_count, CommandResultReservation& ar_reservation) noexcept
{
    if (a_count == 0 || ar_reservation._endpoint || !IsOperational() || !_mapping ||
        _mapping->Header.CommandExecutionThreadId.load(std::memory_order_acquire) != GetCurrentThreadId())
        return false;

    const std::scoped_lock lock{_commandResultLock};
    if (!_commandResultEvents.TryReserve(a_count))
        return false;
    ar_reservation._endpoint = this;
    ar_reservation._remaining = a_count;
    return true;
}

bool BridgeEndpoint::FlushCommandResultEvents() noexcept
{
    if (!IsOperational())
        return false;

    const std::scoped_lock publicationLock{_eventPublicationLock};
    EventPublicationScope publication{_eventPublicationActive};
    if (!publication.Entered()) {
        Fault("reentrant command-result flush");
        return false;
    }

    const std::scoped_lock resultLock{_commandResultLock};
    const auto before = _commandResultEvents.Size();
    const auto drained = _commandResultEvents.FlushTo(_mapping->Events);
    const auto flushed = before - _commandResultEvents.Size();
    if (flushed != 0)
        _mapping->Header.ProducedEventCount.fetch_add(flushed, std::memory_order_relaxed);
    return drained;
}

std::uint64_t BridgeEndpoint::PendingEventBacklogOverflowCount() const noexcept
{
    return _pendingEventBacklogOverflowCount.load(std::memory_order_relaxed);
}

bool BridgeEndpoint::CommitCommandResultEvent(
    CommandResultReservation& ar_reservation, const EventRecord& a_record) noexcept
{
    const std::scoped_lock lock{_commandResultLock};
    if (ar_reservation._endpoint != this || ar_reservation._remaining == 0 ||
        !_commandResultEvents.TryCommitReserved(a_record))
        return false;
    --ar_reservation._remaining;
    if (ar_reservation._remaining == 0)
        ar_reservation._endpoint = nullptr;
    return true;
}

void BridgeEndpoint::ReleaseCommandResultReservation(CommandResultReservation& ar_reservation) noexcept
{
    const std::scoped_lock lock{_commandResultLock};
    if (ar_reservation._endpoint != this)
        return;
    if (!_commandResultEvents.ReleaseReserved(ar_reservation._remaining))
        Fault("command-result reservation accounting failure");
    ar_reservation._remaining = 0;
    ar_reservation._endpoint = nullptr;
}

BridgeIdentity BridgeEndpoint::SnapshotIdentity(const std::uint64_t a_sequenceId) const noexcept
{
    BridgeIdentity identity{};
    if (!_mapping)
        return identity;

    const auto& header = _mapping->Header;
    SessionIdentitySnapshot session{};
    if (!TrySnapshotSessionIdentity(header, session))
        return identity;
    identity.ServerInstanceNonce = session.ServerInstanceNonce;
    identity.ConnectionGeneration = session.ConnectionGeneration;
    identity.LifecycleEpoch = header.LifecycleEpoch.load(std::memory_order_acquire);
    identity.SequenceId = a_sequenceId;
    return identity;
}

std::uint64_t BridgeEndpoint::NextEventSequence() noexcept
{
    return _eventSequence.fetch_add(1, std::memory_order_relaxed) + 1;
}

void BridgeEndpoint::PublishCapabilities() noexcept
{
    if (!ValidateMapping())
        return;

    auto& header = _mapping->Header;
    const auto available = kAvailableCapabilities | _optionalCapabilities.load(std::memory_order_acquire);
    header.AvailableCapabilities.store(available, std::memory_order_release);
    const auto requested = header.RequestedCapabilities.load(std::memory_order_acquire);
    header.ActiveCapabilities.store(requested & available, std::memory_order_release);
}

void BridgeEndpoint::SetOptionalCapability(const Capability a_capability, const bool a_available) noexcept
{
    const auto mask = static_cast<CapabilityMask>(a_capability);
    if (a_available)
        _optionalCapabilities.fetch_or(mask, std::memory_order_acq_rel);
    else
        _optionalCapabilities.fetch_and(~mask, std::memory_order_acq_rel);
    if (_mapping)
        PublishCapabilities();
}

void BridgeEndpoint::Fault(const char* a_reason) noexcept
{
    if (_mapping)
        _mapping->Header.State.store(static_cast<std::uint32_t>(EndpointState::Faulted), std::memory_order_release);
    LogError(a_reason);
}

bool BridgeEndpoint::ValidateMapping() const noexcept
{
    return _mapping && ValidateHeader(_mapping->Header);
}

bool BridgeEndpoint::ValidateHeader(const MappingHeader& a_header) const noexcept
{
    // State is the publication barrier for the immutable ABI fields written by
    // the mapping creator.
    const auto state = static_cast<EndpointState>(a_header.State.load(std::memory_order_acquire));
    if (!IsSupportedState(state))
        return false;

    if (a_header.Magic != kMappingMagic || a_header.AbiVersion != kMappingAbiVersion ||
        a_header.HeaderSize != sizeof(MappingHeader) || a_header.MappingSize != sizeof(GameplayBridgeMapping) ||
        a_header.PublisherProcessId != GetCurrentProcessId() || a_header.RuntimeVersion != kSkyrimVrRuntimeVersion ||
        a_header.CapabilityRevision != kCapabilityRevision)
        return false;

    if (state == EndpointState::Ready) {
        const auto available = a_header.AvailableCapabilities.load(std::memory_order_acquire);
        const auto requested = a_header.RequestedCapabilities.load(std::memory_order_acquire);
        const auto active = a_header.ActiveCapabilities.load(std::memory_order_acquire);
        return a_header.EventConsumerThreadId.load(std::memory_order_acquire) != 0 &&
               a_header.CommandExecutionThreadId.load(std::memory_order_acquire) != 0 &&
               (available & kAvailableCapabilities) == kAvailableCapabilities &&
               (available & ~kAllowedCapabilities) == 0 &&
               (active & ~kAllowedCapabilities) == 0 &&
               (active & ~requested) == 0;
    }

    return true;
}

bool BridgeEndpoint::IsSupportedState(const EndpointState a_state) noexcept
{
    return a_state == EndpointState::Prepared || a_state == EndpointState::Ready || a_state == EndpointState::Retiring ||
           a_state == EndpointState::Retired || a_state == EndpointState::Faulted;
}
} // namespace SkyrimTogetherVR::GameplayAdapter
