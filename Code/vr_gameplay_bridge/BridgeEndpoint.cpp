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
    static_cast<CapabilityMask>(Capability::WorldState) |
    static_cast<CapabilityMask>(Capability::VrBodyPose) |
    static_cast<CapabilityMask>(Capability::HiggsInteraction) |
    static_cast<CapabilityMask>(Capability::NpcOwnership);

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
} // namespace

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
    return state == EndpointState::Prepared || state == EndpointState::Ready;
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
    if (!IsOperational() || !TryPush(_mapping->Events, a_record))
        return false;

    _mapping->Header.ProducedEventCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool BridgeEndpoint::TryPushEvents(const EventRecord* ap_records, const std::size_t a_count) noexcept
{
    if (!IsOperational() || !ap_records || a_count == 0 ||
        !TryPushBatch(_mapping->Events, ap_records, a_count))
        return false;

    _mapping->Header.ProducedEventCount.fetch_add(a_count, std::memory_order_relaxed);
    return true;
}

BridgeIdentity BridgeEndpoint::SnapshotIdentity(const std::uint64_t a_sequenceId) const noexcept
{
    BridgeIdentity identity{};
    if (!_mapping)
        return identity;

    const auto& header = _mapping->Header;
    identity.ServerInstanceNonce = header.ServerInstanceNonce.load(std::memory_order_acquire);
    identity.ConnectionGeneration = header.ConnectionGeneration.load(std::memory_order_acquire);
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
        a_header.CapabilityRevision != kCapabilityRevision || a_header.Reserved0 != 0)
        return false;

    if (state == EndpointState::Ready) {
        const auto available = a_header.AvailableCapabilities.load(std::memory_order_acquire);
        const auto requested = a_header.RequestedCapabilities.load(std::memory_order_acquire);
        const auto expectedAvailable = kAvailableCapabilities | _optionalCapabilities.load(std::memory_order_acquire);
        return a_header.EventConsumerThreadId.load(std::memory_order_acquire) != 0 &&
               a_header.CommandExecutionThreadId.load(std::memory_order_acquire) != 0 &&
               available == expectedAvailable &&
               a_header.ActiveCapabilities.load(std::memory_order_acquire) == (requested & available);
    }

    return true;
}

bool BridgeEndpoint::IsSupportedState(const EndpointState a_state) noexcept
{
    return a_state == EndpointState::Prepared || a_state == EndpointState::Ready || a_state == EndpointState::Retiring ||
           a_state == EndpointState::Retired || a_state == EndpointState::Faulted;
}
} // namespace SkyrimTogetherVR::GameplayAdapter
