#pragma once

#include "BridgeBatchPolicy.h"
#include "pch.h"

#include <mutex>

namespace SkyrimTogetherVR::GameplayAdapter
{
using namespace SkyrimTogetherVR::GameplayBridge;

class BridgeEndpoint
{
public:
    class CommandResultReservation
    {
    public:
        CommandResultReservation() noexcept = default;
        ~CommandResultReservation() noexcept;
        CommandResultReservation(const CommandResultReservation&) = delete;
        CommandResultReservation& operator=(const CommandResultReservation&) = delete;
        CommandResultReservation(CommandResultReservation&&) = delete;
        CommandResultReservation& operator=(CommandResultReservation&&) = delete;

        [[nodiscard]] bool Commit(const EventRecord& a_record) noexcept;
        [[nodiscard]] std::size_t Remaining() const noexcept { return _remaining; }

    private:
        friend class BridgeEndpoint;
        BridgeEndpoint* _endpoint{};
        std::size_t _remaining{};
    };

    static BridgeEndpoint& Get() noexcept;

    [[nodiscard]] bool Attach() noexcept;
    [[nodiscard]] bool IsAttached() const noexcept;
    [[nodiscard]] GameplayBridgeMapping* Mapping() const noexcept;
    [[nodiscard]] bool IsOperational() const noexcept;
    [[nodiscard]] CommandPumpResult ValidateCommandPump(
        std::uint32_t a_callerProcessId,
        std::uint32_t a_callerThreadId,
        std::uint64_t a_lifecycleEpoch) noexcept;
    [[nodiscard]] bool TryPushEvent(const EventRecord& a_record) noexcept;
    [[nodiscard]] bool TryPushEvents(const EventRecord* ap_records, std::size_t a_count) noexcept;
    // Retries valid producer backlog without changing the shared mapping ABI.
    [[nodiscard]] bool FlushPendingEvents() noexcept;
    [[nodiscard]] bool TryReserveCommandResultEvents(
        std::size_t a_count, CommandResultReservation& ar_reservation) noexcept;
    [[nodiscard]] bool FlushCommandResultEvents() noexcept;
    [[nodiscard]] std::uint64_t PendingEventBacklogOverflowCount() const noexcept;
    [[nodiscard]] BridgeIdentity SnapshotIdentity(std::uint64_t a_sequenceId) const noexcept;
    [[nodiscard]] std::uint64_t NextEventSequence() noexcept;

    void SetOptionalCapability(Capability a_capability, bool a_available) noexcept;
    void PublishCapabilities() noexcept;
    void Fault(const char* a_reason) noexcept;

private:
    struct PendingEventRecord
    {
        EventRecord Record{};
        std::uint64_t BatchId{};
        std::uint16_t BatchSize{};
    };

    [[nodiscard]] bool TryClassifyEventBatchIdentity(
        const EventRecord* ap_records, std::size_t a_count, WorkAttribution& ar_attribution) const noexcept;
    [[nodiscard]] bool TryPublishEventsLocked(
        const EventRecord* ap_records, std::size_t a_count) noexcept;
    [[nodiscard]] bool FlushPendingEventsLocked() noexcept;
    [[nodiscard]] bool QueuePendingEventsLocked(const EventRecord* ap_records, std::size_t a_count) noexcept;
    [[nodiscard]] PendingEventRecord& PendingEventAt(std::size_t a_offset) noexcept;
    [[nodiscard]] const PendingEventRecord& PendingEventAt(std::size_t a_offset) const noexcept;
    void PopPendingEventBatch(std::size_t a_count) noexcept;
    void EraseSubsumedPendingEvents(const EventRecord& ac_incoming) noexcept;
    void RecordRejectedEventBatch(const char* a_reason, std::size_t a_count) noexcept;
    [[nodiscard]] static bool IsSubsumedByIncoming(
        const EventRecord& a_pending, const EventRecord* ap_incoming, std::size_t a_incomingCount) noexcept;
    [[nodiscard]] bool CommitCommandResultEvent(
        CommandResultReservation& ar_reservation, const EventRecord& a_record) noexcept;
    void ReleaseCommandResultReservation(CommandResultReservation& ar_reservation) noexcept;
    [[nodiscard]] bool ValidateMapping() const noexcept;
    [[nodiscard]] bool ValidateHeader(const MappingHeader& a_header) const noexcept;
    [[nodiscard]] static bool IsSupportedState(EndpointState a_state) noexcept;

    GameplayBridgeMapping* _mapping{};
    std::atomic<std::uint64_t> _eventSequence{};
    std::atomic<std::uint64_t> _pendingEventBacklogOverflowCount{};
    std::atomic<std::uint64_t> _rejectedEventBatchCount{};
    std::atomic<CapabilityMask> _optionalCapabilities{};
    std::atomic_bool _attachAttempted{};
    std::recursive_mutex _eventPublicationLock{};
    bool _eventPublicationActive{};
    std::array<EventRecord, kDefaultEventRingCapacity> _eventBatchScratch{};
    std::array<PendingEventRecord, kDefaultEventRingCapacity> _pendingEvents{};
    std::size_t _pendingEventHead{};
    std::size_t _pendingEventCount{};
    std::uint64_t _nextPendingEventBatchId{1};
    std::mutex _commandResultLock{};
    BoundedReservedRecordQueue<EventRecord, kDefaultCommandRingCapacity> _commandResultEvents{};
};
} // namespace SkyrimTogetherVR::GameplayAdapter
