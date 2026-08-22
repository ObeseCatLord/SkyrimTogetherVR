#include <TiltedOnlinePCH.h>

#include <Services/Generic/VRGameplayDiagnosticsService.h>

#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/LocalGameplayBridgeEvent.h>
#include <Events/RemoteGameplayBridgeResultEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/TransportService.h>
#include <Services/VRConnectionService.h>
#include <Structs/GameplayCapabilities.h>
#include <VRCompatibilityStatus.h>
#include <VRGameplayBridge.h>
#include <vr_common/VRHandoffPath.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <string_view>

namespace GameplayBridge = SkyrimTogetherVR::GameplayBridge;

namespace
{
constexpr char kGameplayStatusFileName[] = "SkyrimTogetherVR.gameplay";
constexpr std::uint32_t kGameplayStatusSchemaVersion = 1;
constexpr double kStatusWriteInterval = 1.0;
constexpr double kSummaryLogInterval = 30.0;
std::atomic_bool s_liveServiceRegistered{false};

enum class DomainPath : std::uint8_t
{
    Canonical,
    Direct,
    Degraded,
    Unsupported,
};

struct DomainDescriptor
{
    GameplayBridge::GameplayDomain Domain;
    const char* Name;
    DomainPath Path;
    const char* FixedReason;
    bool Mandatory;
};

constexpr std::array kDomains{
    DomainDescriptor{GameplayBridge::GameplayDomain::Animation, "animation", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::Appearance, "appearance", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::Equipment, "equipment", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::Inventory, "inventory", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::ActorState, "actor_state", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::Object, "object", DomainPath::Canonical, "", true},
    // Desktop Skyrim Together owns damage through target-owner actor-value
    // replication. The absence of an attacker-provided melee damage command is
    // therefore canonical, not a degraded VR implementation.
    DomainDescriptor{GameplayBridge::GameplayDomain::Combat, "combat", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::Projectile, "projectile", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::Magic, "magic", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::Quest, "quest", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::Dialogue, "dialogue", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::Party, "party", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::WorldState, "world_state", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::VrBodyPose, "vr_body_pose", DomainPath::Canonical, "", true},
    DomainDescriptor{GameplayBridge::GameplayDomain::Higgs, "higgs", DomainPath::Direct,
                     "optional_external_bridge", false},
    DomainDescriptor{GameplayBridge::GameplayDomain::Planck, "planck", DomainPath::Direct,
                     "optional_negotiated_interface002", false},
    DomainDescriptor{GameplayBridge::GameplayDomain::NpcOwnership, "npc_ownership", DomainPath::Canonical, "", true},
};

static_assert(kDomains.size() == VRGameplayDiagnosticsService::kDomainCount);

constexpr bool DomainDescriptorsMatchCounterIndices() noexcept
{
    for (std::size_t index = 0; index < kDomains.size(); ++index)
    {
        if (VRGameplayDiagnosticsService::DomainIndex(kDomains[index].Domain) != index)
            return false;
    }

    return true;
}

static_assert(DomainDescriptorsMatchCounterIndices());

const char* ToString(const DomainPath aPath) noexcept
{
    switch (aPath)
    {
    case DomainPath::Canonical: return "canonical";
    case DomainPath::Direct: return "direct";
    case DomainPath::Degraded: return "degraded";
    case DomainPath::Unsupported: return "unsupported";
    }

    return "unknown";
}

bool HasActiveCapability(
    const SkyrimTogetherVR::GameplayBridgeClient::Diagnostics& acDiagnostics,
    const GameplayBridge::GameplayDomain aDomain) noexcept
{
    const auto capability = GameplayBridge::CapabilityForDomain(aDomain);
    return capability != static_cast<GameplayBridge::Capability>(0) &&
           GameplayBridge::HasCapability(acDiagnostics.ActiveCapabilities, capability);
}

const char* DomainState(
    const DomainDescriptor& acDescriptor,
    const SkyrimTogetherVR::GameplayBridgeClient::Diagnostics& acDiagnostics,
    const bool aOnline,
    const bool aPlanckInterface002Operational) noexcept
{
    if (acDescriptor.Path == DomainPath::Unsupported)
        return "unsupported";
    if (acDescriptor.Path == DomainPath::Degraded)
        return "degraded";
    if (!acDiagnostics.Ready)
        return "waiting_bridge";
    const bool capabilityActive = acDescriptor.Domain == GameplayBridge::GameplayDomain::Planck ?
        aPlanckInterface002Operational : HasActiveCapability(acDiagnostics, acDescriptor.Domain);
    if (!capabilityActive)
        return "blocked_capability";
    if (!aOnline)
        return "waiting_transport";
    return "active";
}

bool IsMandatoryDomainReady(
    const DomainDescriptor& acDescriptor,
    const SkyrimTogetherVR::GameplayBridgeClient::Diagnostics& acDiagnostics,
    const bool aOnline,
    const bool aPlanckInterface002Operational) noexcept
{
    return !acDescriptor.Mandatory ||
           std::string_view(DomainState(
               acDescriptor, acDiagnostics, aOnline, aPlanckInterface002Operational)) == "active";
}

const char* EvidenceState(const VRGameplayDiagnosticsService::DomainCounters& acCounters) noexcept
{
    if (acCounters.Captured != 0 && acCounters.Sent != 0 && acCounters.Applied != 0)
        return "observed";
    if (acCounters.Captured != 0 || acCounters.Sent != 0 || acCounters.Applied != 0 || acCounters.Rejected != 0)
        return "partial";
    return "unobserved";
}

bool HasObservedEvidence(const VRGameplayDiagnosticsService::DomainCounters& acCounters) noexcept
{
    return std::string_view(EvidenceState(acCounters)) == "observed";
}

const char* LifecycleRehydrationEvidenceState(
    const VRGameplayDiagnosticsService::DomainCounters& acCounters) noexcept
{
    if (acCounters.Captured != 0 && acCounters.Applied != 0)
        return "rehydrated";
    if (acCounters.Captured != 0)
        return "pending_rehydration";
    return "unobserved";
}

bool HasObservedLifecycleRehydration(
    const VRGameplayDiagnosticsService::DomainCounters& acCounters) noexcept
{
    return std::string_view(LifecycleRehydrationEvidenceState(acCounters)) == "rehydrated";
}

std::filesystem::path GetStatusPath(const std::filesystem::path& acGamePath)
{
    return acGamePath / L"Data" / L"SkyrimTogetherReborn" / kGameplayStatusFileName;
}

const char* EndpointStateName(const std::uint32_t aState) noexcept
{
    switch (static_cast<GameplayBridge::EndpointState>(aState))
    {
    case GameplayBridge::EndpointState::Uninitialized: return "uninitialized";
    case GameplayBridge::EndpointState::Prepared: return "prepared";
    case GameplayBridge::EndpointState::Ready: return "ready";
    case GameplayBridge::EndpointState::Retiring: return "retiring";
    case GameplayBridge::EndpointState::Retired: return "retired";
    case GameplayBridge::EndpointState::Faulted: return "faulted";
    }

    return "unknown";
}

bool WriteGameplayStatusSnapshot(
    const std::filesystem::path& acPath,
    const VRCompatibilityStatus* apCompatibility,
    const SkyrimTogetherVR::GameplayBridgeClient::Diagnostics& acDiagnostics,
    const TransportService* apTransport,
    const VRConnectionService* apConnection,
    const std::array<VRGameplayDiagnosticsService::DomainCounters, VRGameplayDiagnosticsService::kDomainCount>& acCounters,
    const VRGameplayDiagnosticsService::DomainCounters& acMovementCounters,
    const VRGameplayDiagnosticsService::DomainCounters& acSaveLoadCounters,
    const bool aRegistered,
    const char* apState,
    const char* apReason) noexcept
{
    const bool transportOnline = apTransport && apTransport->IsOnline();
    const bool online = apConnection && apConnection->IsReadyForGameplay();
    const bool planckInterface002Operational = apTransport &&
        SkyrimTogether::Protocol::HasCapability(
            apTransport->GetNegotiatedGameplayCapabilities(),
            SkyrimTogether::Protocol::GameplayCapability::PlanckPhysicsInterface002);
    const bool localEventSinksActive = GameplayBridge::HasCapability(
        acDiagnostics.ActiveCapabilities, GameplayBridge::Capability::LocalEventSinks);
    const bool localCaptureSinksActive = GameplayBridge::HasCapability(
        acDiagnostics.ActiveCapabilities, GameplayBridge::Capability::LocalCaptureSinks);
    const bool nativeGameplayCoreContractActive =
        (acDiagnostics.ActiveCapabilities & GameplayBridge::kMandatoryNativeGameplayCoreCapabilities) ==
        GameplayBridge::kMandatoryNativeGameplayCoreCapabilities;
    bool mandatoryReady = aRegistered && acDiagnostics.Ready && online && localCaptureSinksActive &&
                          nativeGameplayCoreContractActive;
    for (const auto& domain : kDomains)
        mandatoryReady = mandatoryReady && IsMandatoryDomainReady(
            domain, acDiagnostics, online, planckInterface002Operational);

    std::size_t observedDomainCount{};
    std::size_t partialDomainCount{};
    const auto countEvidence = [&observedDomainCount, &partialDomainCount](
                                   const VRGameplayDiagnosticsService::DomainCounters& acCounters) noexcept
    {
        if (HasObservedEvidence(acCounters))
            ++observedDomainCount;
        else if (std::string_view(EvidenceState(acCounters)) == "partial")
            ++partialDomainCount;
    };
    for (const auto& counters : acCounters)
        countEvidence(counters);
    countEvidence(acMovementCounters);
    if (HasObservedLifecycleRehydration(acSaveLoadCounters))
        ++observedDomainCount;
    else if (std::string_view(LifecycleRehydrationEvidenceState(acSaveLoadCounters)) != "unobserved")
        ++partialDomainCount;

    return SkyrimTogetherVR::Handoff::WriteFileAtomically(
        acPath,
        [&](std::ofstream& file)
        {
            SkyrimTogetherVR::Handoff::WriteLaunchIdentity(file);
            file << "schemaVersion=" << kGameplayStatusSchemaVersion << "\n";
            file << "ready=" << (mandatoryReady ? "1" : "0") << "\n";
            file << "operationalReady=" << (mandatoryReady ? "1" : "0") << "\n";
            file << "readinessScope=operational_availability\n";
            // A snapshot belongs to one process. Even complete local counters
            // cannot prove that a second client received the matching work.
            // The evidence collector establishes that only by correlating two
            // distinct snapshots with one session identity.
            file << "evidence.scope=local_process_counters\n";
            file << "evidence.twoClientProof=0\n";
            file << "evidence.twoClientState=unproven_requires_paired_snapshots\n";
            file << "evidence.observedDomainCount=" << observedDomainCount << "\n";
            file << "evidence.partialDomainCount=" << partialDomainCount << "\n";
            file << "state=" << apState << "\n";
            file << "reason=" << apReason << "\n";
            file << "registration=" << (aRegistered ? "registered" : "unregistered") << "\n";
            file << "canonical.path=commonlib_bridge\n";
            file << "direct.path=separate_extension_relays\n";
            file << "direct.state=not_a_core_readiness_signal\n";
            file << "direct.planckInterface002Operational=" <<
                (planckInterface002Operational ? "1" : "0") << "\n";
            file << "bridge.initialized=" << (acDiagnostics.Initialized ? "1" : "0") << "\n";
            file << "bridge.ready=" << (acDiagnostics.Ready ? "1" : "0") << "\n";
            file << "bridge.retired=" << (acDiagnostics.Retired ? "1" : "0") << "\n";
            file << "bridge.endpointState=" << EndpointStateName(acDiagnostics.EndpointState) << "\n";
            file << "bridge.requestedCapabilities=" << acDiagnostics.RequestedCapabilities << "\n";
            file << "bridge.availableCapabilities=" << acDiagnostics.AvailableCapabilities << "\n";
            file << "bridge.activeCapabilities=" << acDiagnostics.ActiveCapabilities << "\n";
            file << "bridge.localEventSinksActive=" << (localEventSinksActive ? "1" : "0") << "\n";
            file << "bridge.localCaptureSinksActive=" << (localCaptureSinksActive ? "1" : "0") << "\n";
            file << "bridge.nativeGameplayCoreContractActive=" << (nativeGameplayCoreContractActive ? "1" : "0") << "\n";
            file << "bridge.producedEvents=" << acDiagnostics.ProducedEventCount << "\n";
            file << "bridge.consumedEvents=" << acDiagnostics.ConsumedEventCount << "\n";
            file << "bridge.submittedCommands=" << acDiagnostics.SubmittedCommandCount << "\n";
            file << "bridge.executedCommands=" << acDiagnostics.ExecutedCommandCount << "\n";
            file << "bridge.rejectedCommands=" << acDiagnostics.RejectedCommandCount << "\n";
            file << "bridge.staleCommands=" << acDiagnostics.StaleCommandCount << "\n";
            file << "bridge.discardedEvents=" << acDiagnostics.DiscardedEventCount << "\n";
            file << "bridge.discardedEvents.preReady=" << acDiagnostics.DiscardedEventPreReadyCount << "\n";
            file << "bridge.discardedEvents.lifecycleRetired=" << acDiagnostics.DiscardedEventLifecycleRetiredCount << "\n";
            file << "bridge.discardedEvents.other=" << acDiagnostics.DiscardedEventOtherCount << "\n";
            file << "bridge.rejectedSubmissions=" << acDiagnostics.RejectedSubmissionCount << "\n";
            file << "bridge.rejectedSubmissions.preReady=" << acDiagnostics.RejectedSubmissionPreReadyCount << "\n";
            file << "bridge.rejectedSubmissions.lifecycleRetired=" << acDiagnostics.RejectedSubmissionLifecycleRetiredCount << "\n";
            file << "bridge.rejectedSubmissions.other=" << acDiagnostics.RejectedSubmissionOtherCount << "\n";
            file << "bridge.eventRingDroppedPushes=" << acDiagnostics.EventRingDroppedPushCount << "\n";
            file << "bridge.commandRingDroppedPushes=" << acDiagnostics.CommandRingDroppedPushCount << "\n";
            file << "bridge.authority.suppressedDamage=" << acDiagnostics.ActorAuthority.SuppressedDamageCount << "\n";
            file << "bridge.authority.suppressedDeathItems=" << acDiagnostics.ActorAuthority.SuppressedDeathItemsCount << "\n";
            file << "bridge.authority.suppressedPositiveActiveEffectHealth=" <<
                acDiagnostics.ActorAuthority.SuppressedPositiveActiveEffectHealthCount << "\n";
            file << "bridge.authority.suppressedRestoreHealth=" << acDiagnostics.ActorAuthority.SuppressedRestoreHealthCount << "\n";
            file << "bridge.authority.suppressedReferenceSetPosition=" <<
                acDiagnostics.ActorAuthority.SuppressedReferenceSetPositionCount << "\n";
            file << "bridge.authority.suppressedActorSetPosition=" << acDiagnostics.ActorAuthority.SuppressedActorSetPositionCount << "\n";
            file << "bridge.authority.suppressedMoveTo=" << acDiagnostics.ActorAuthority.SuppressedMoveToCount << "\n";
            file << "bridge.authority.suppressedActorProcess=" << acDiagnostics.ActorAuthority.SuppressedActorProcessCount << "\n";
            file << "bridge.authority.publishedRemoteNpcHealthDelta=" <<
                acDiagnostics.ActorAuthority.PublishedRemoteNpcHealthDeltaCount << "\n";
            file << "bridge.authority.failedRemoteNpcHealthDeltaPublication=" <<
                acDiagnostics.ActorAuthority.FailedRemoteNpcHealthDeltaPublicationCount << "\n";
            file << "bridge.authority.leaseFailures=" << acDiagnostics.ActorAuthority.LeaseFailureCount << "\n";
            file << "bridge.authority.retirementFailures=" << acDiagnostics.ActorAuthority.RetirementFailureCount << "\n";
            file << "bridge.authority.retirementTimeouts=" << acDiagnostics.ActorAuthority.RetirementTimeoutCount << "\n";
            file << "bridge.authority.registryInconsistencies=" << acDiagnostics.ActorAuthority.RegistryInconsistencyCount << "\n";
            file << "session.online=" << (online ? "1" : "0") << "\n";
            file << "session.transportOnline=" << (transportOnline ? "1" : "0") << "\n";
            file << "session.id=" << (apTransport ? apTransport->GetSessionId() : 0) << "\n";
            file << "session.serverInstanceNonce=" << (apTransport ? apTransport->GetServerInstanceNonce() : 0) << "\n";
            file << "session.connectionGeneration=" << (apTransport ? apTransport->GetConnectionGeneration() : 0) << "\n";
            file << "session.lifecycleEpoch=" << acDiagnostics.LifecycleEpoch << "\n";
            file << "senderInstrumentation=" << (aRegistered ? "transport_queue_acceptance" : "not_registered") << "\n";
            if (apCompatibility)
            {
                file << "configuration.nativeCanonicalGameplay=" <<
                    (apCompatibility->NativeCanonicalGameplay ? "1" : "0") << "\n";
                file << "configuration.connectionOnly=" << (apCompatibility->ConnectionOnly ? "1" : "0") << "\n";
            }

            for (std::size_t index = 0; index < kDomains.size(); ++index)
            {
                const auto& domain = kDomains[index];
                const auto& counters = acCounters[index];
                file << "domain." << domain.Name << ".path=" << ToString(domain.Path) << "\n";
                file << "domain." << domain.Name << ".availability=" <<
                    DomainState(domain, acDiagnostics, online, planckInterface002Operational) << "\n";
                file << "domain." << domain.Name << ".state=" <<
                    DomainState(domain, acDiagnostics, online, planckInterface002Operational) << "\n";
                file << "domain." << domain.Name << ".evidenceState=" << EvidenceState(counters) << "\n";
                if (domain.FixedReason[0] != '\0')
                    file << "domain." << domain.Name << ".reason=" << domain.FixedReason << "\n";
                file << "domain." << domain.Name << ".captured=" << counters.Captured << "\n";
                file << "domain." << domain.Name << ".sent=" << counters.Sent << "\n";
                file << "domain." << domain.Name << ".applied=" << counters.Applied << "\n";
                file << "domain." << domain.Name << ".rejected=" << counters.Rejected << "\n";
            }
            const auto movementState = mandatoryReady ? "active" : (acDiagnostics.Ready ? "waiting_session" : "waiting_bridge");
            file << "domain.movement.path=canonical\n";
            file << "domain.movement.availability=" << movementState << "\n";
            file << "domain.movement.state=" << movementState << "\n";
            file << "domain.movement.evidenceState=" << EvidenceState(acMovementCounters) << "\n";
            file << "domain.movement.captured=" << acMovementCounters.Captured << "\n";
            file << "domain.movement.sent=" << acMovementCounters.Sent << "\n";
            file << "domain.movement.applied=" << acMovementCounters.Applied << "\n";
            file << "domain.movement.rejected=" << acMovementCounters.Rejected << "\n";
            file << "domain.save_load.path=canonical_lifecycle_rehydration\n";
            file << "domain.save_load.state=" << (online ? "active" : "waiting_session") << "\n";
            file << "domain.save_load.availability=" << (online ? "active" : "waiting_session") << "\n";
            file << "domain.save_load.evidenceType=lifecycle_rehydration\n";
            file << "domain.save_load.networkTraffic=not_applicable\n";
            file << "domain.save_load.evidenceState=" << LifecycleRehydrationEvidenceState(acSaveLoadCounters) << "\n";
            file << "domain.save_load.captured=" << acSaveLoadCounters.Captured << "\n";
            file << "domain.save_load.sent=" << acSaveLoadCounters.Sent << "\n";
            file << "domain.save_load.applied=" << acSaveLoadCounters.Applied << "\n";
            file << "domain.save_load.rejected=" << acSaveLoadCounters.Rejected << "\n";
        });
}
} // namespace

VRGameplayDiagnosticsService::VRGameplayDiagnosticsService(
    entt::dispatcher& aDispatcher, TransportService& aTransport, VRConnectionService& aConnection) noexcept
    : m_transport(aTransport)
    , m_connection(aConnection)
    , m_statusPath(SkyrimTogetherVR::Handoff::GetFile(kGameplayStatusFileName))
{
    s_liveServiceRegistered.store(true, std::memory_order_release);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRGameplayDiagnosticsService::OnUpdate>(this);
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&VRGameplayDiagnosticsService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRGameplayDiagnosticsService::OnDisconnected>(this);
    m_connectionErrorConnection = aDispatcher.sink<ConnectionErrorEvent>().connect<&VRGameplayDiagnosticsService::OnConnectionError>(this);
    m_localGameplayConnection =
        aDispatcher.sink<SkyrimTogetherVR::LocalGameplayBridgeEvent>().connect<&VRGameplayDiagnosticsService::OnLocalGameplay>(this);
    m_gameplayResultConnection = aDispatcher.sink<SkyrimTogetherVR::RemoteGameplayBridgeResultEvent>()
        .connect<&VRGameplayDiagnosticsService::OnGameplayResult>(this);

    RefreshSessionIdentity();
    WriteSnapshot(true);
    spdlog::info("SkyrimTogetherVR gameplay diagnostics registered: {}", m_statusPath.string());
}

VRGameplayDiagnosticsService::~VRGameplayDiagnosticsService() noexcept
{
    s_liveServiceRegistered.store(false, std::memory_order_release);
}

void VRGameplayDiagnosticsService::PublishBootstrapSnapshot(
    const std::filesystem::path& acGamePath,
    const VRCompatibilityStatus& acStatus) noexcept
{
    if (s_liveServiceRegistered.load(std::memory_order_acquire))
        return;

    const std::array<DomainCounters, kDomainCount> counters{};
    if (!WriteGameplayStatusSnapshot(GetStatusPath(acGamePath), &acStatus, {}, nullptr, nullptr, counters, {}, {}, false,
                                     "bootstrap", "diagnostics_service_not_registered"))
        spdlog::warn("SkyrimTogetherVR gameplay diagnostics could not publish bootstrap snapshot");
}

void VRGameplayDiagnosticsService::RecordOutboundAccepted(const GameplayBridge::GameplayDomain aDomain) noexcept
{
    const auto index = DomainIndex(aDomain);
    if (index == kDomainCount)
        return;
    if (m_counters[index].Sent != std::numeric_limits<std::uint64_t>::max())
        ++m_counters[index].Sent;
}

void VRGameplayDiagnosticsService::RecordOutboundRejected(const GameplayBridge::GameplayDomain aDomain) noexcept
{
    const auto index = DomainIndex(aDomain);
    if (index == kDomainCount)
        return;
    if (m_counters[index].Rejected != std::numeric_limits<std::uint64_t>::max())
        ++m_counters[index].Rejected;
}

void VRGameplayDiagnosticsService::RecordMovementAccepted() noexcept
{
    if (m_movementCounters.Sent != std::numeric_limits<std::uint64_t>::max())
        ++m_movementCounters.Sent;
}

void VRGameplayDiagnosticsService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    const auto delta = std::clamp(acEvent.Delta, 0.0, 1.0);
    m_statusTimer += delta;
    m_summaryTimer += delta;
    RefreshSessionIdentity();
    LogRateLimitedSummary();
    if (m_statusDirty || m_statusTimer >= kStatusWriteInterval)
        WriteSnapshot();
}

void VRGameplayDiagnosticsService::OnConnected(const ConnectedEvent&) noexcept
{
    RefreshSessionIdentity();
    m_statusDirty = true;
}

void VRGameplayDiagnosticsService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    RefreshSessionIdentity();
    m_statusDirty = true;
}

void VRGameplayDiagnosticsService::OnConnectionError(const ConnectionErrorEvent&) noexcept
{
    m_statusDirty = true;
    LogStateTransition("connection_error", "transport_error");
}

void VRGameplayDiagnosticsService::OnLocalGameplay(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept
{
    if (acEvent.Record.Header.Kind == static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalPlayerState))
    {
        if (m_movementCounters.Captured != std::numeric_limits<std::uint64_t>::max())
            ++m_movementCounters.Captured;
        return;
    }
    const auto index = DomainIndex(DomainForEvent(acEvent.Record));
    if (index == kDomainCount)
        return;
    if (m_counters[index].Captured != std::numeric_limits<std::uint64_t>::max())
        ++m_counters[index].Captured;
}

void VRGameplayDiagnosticsService::OnGameplayResult(const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept
{
    const auto& record = acEvent.Record;
    const auto kind = static_cast<GameplayBridge::EventKind>(record.Header.Kind);
    if (kind == GameplayBridge::EventKind::RemoteAvatarState)
    {
        const auto& result = record.Payload.RemoteAvatarState;
        // Avatar create/destroy actions use ActionId and carry SequenceId ==
        // 0. Root-transform commands require a nonzero sequence, so this
        // isolates their result without widening the established ABI enum.
        if (record.Header.Identity.SequenceId == 0)
            return;
        auto& target = static_cast<GameplayBridge::CommandStatus>(result.Status) == GameplayBridge::CommandStatus::Success ?
            m_movementCounters.Applied : m_movementCounters.Rejected;
        if (target != std::numeric_limits<std::uint64_t>::max())
            ++target;
        return;
    }
    if (record.Header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::RemoteGameplayActionState))
        return;

    const auto& result = record.Payload.RemoteGameplayActionState;
    const auto index = DomainIndex(static_cast<GameplayBridge::GameplayDomain>(result.Domain));
    if (index == kDomainCount)
        return;

    const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
    auto& counters = m_counters[index];
    auto& target = GameplayBridge::IsSuccessfulCommandResult(
        status, static_cast<GameplayBridge::GameplayDomain>(result.Domain),
        static_cast<GameplayBridge::GameplayAction>(result.Action)) ? counters.Applied : counters.Rejected;
    if (target != std::numeric_limits<std::uint64_t>::max())
        ++target;
}

void VRGameplayDiagnosticsService::RefreshSessionIdentity() noexcept
{
    const auto bridge = SkyrimTogetherVR::GameplayBridgeClient::GetDiagnostics();
    const auto sessionId = m_transport.GetSessionId();
    const auto serverNonce = m_transport.GetServerInstanceNonce();
    const auto generation = m_transport.GetConnectionGeneration();
    const bool sessionBindingChanged = sessionId != m_sessionId || serverNonce != m_serverInstanceNonce ||
                                       generation != m_connectionGeneration;
    const bool lifecycleChanged = bridge.LifecycleEpoch != m_lifecycleEpoch;
    if (m_sessionIdentityInitialized && !sessionBindingChanged && !lifecycleChanged)
        return;

    m_sessionId = sessionId;
    m_serverInstanceNonce = serverNonce;
    m_connectionGeneration = generation;
    m_lifecycleEpoch = bridge.LifecycleEpoch;
    if (!m_sessionIdentityInitialized) {
        m_sessionIdentityInitialized = true;
    } else if (sessionBindingChanged) {
        // A counter set never crosses server/session bindings. In particular,
        // a prior load cannot become evidence for a later connection.
        ResetCountersForSession();
    } else if (lifecycleChanged) {
        // This is local lifecycle evidence, not a fabricated network send.
        // Keep only the deliberate rehydration record while rebuilding normal
        // gameplay evidence for the new bridge epoch.
        ResetGameplayCounters();
        if (m_saveLoadCounters.Captured != std::numeric_limits<std::uint64_t>::max())
            ++m_saveLoadCounters.Captured;
        m_saveLoadRehydrationPending = true;
    }
    m_statusDirty = true;
}

void VRGameplayDiagnosticsService::ResetCountersForSession() noexcept
{
    ResetGameplayCounters();
    m_saveLoadCounters = {};
    m_saveLoadRehydrationPending = false;
    spdlog::info("SkyrimTogetherVR gameplay diagnostics bound counters to session={}, generation={}, epoch={}",
                 m_sessionId, m_connectionGeneration, m_lifecycleEpoch);
}

void VRGameplayDiagnosticsService::ResetGameplayCounters() noexcept
{
    m_counters = {};
    m_movementCounters = {};
    m_lastRejectedCommandCount = 0;
    m_lastStaleCommandCount = 0;
    m_lastDroppedEventCount = 0;
    m_lastDroppedCommandCount = 0;
    m_lastActorAuthorityDiagnostics = {};
}

void VRGameplayDiagnosticsService::WriteSnapshot(const bool aForce) noexcept
{
    const auto diagnostics = SkyrimTogetherVR::GameplayBridgeClient::GetDiagnostics();
    const bool online = m_connection.IsReadyForGameplay();
    if (online && m_saveLoadRehydrationPending &&
        m_saveLoadCounters.Applied != std::numeric_limits<std::uint64_t>::max())
    {
        ++m_saveLoadCounters.Applied;
        m_saveLoadRehydrationPending = false;
    }
    const bool localEventSinksActive = GameplayBridge::HasCapability(
        diagnostics.ActiveCapabilities, GameplayBridge::Capability::LocalEventSinks);
    const bool localCaptureSinksActive = GameplayBridge::HasCapability(
        diagnostics.ActiveCapabilities, GameplayBridge::Capability::LocalCaptureSinks);
    const bool nativeGameplayCoreContractActive =
        (diagnostics.ActiveCapabilities & GameplayBridge::kMandatoryNativeGameplayCoreCapabilities) ==
        GameplayBridge::kMandatoryNativeGameplayCoreCapabilities;
    const bool planckInterface002Operational = SkyrimTogether::Protocol::HasCapability(
        m_transport.GetNegotiatedGameplayCapabilities(),
        SkyrimTogether::Protocol::GameplayCapability::PlanckPhysicsInterface002);
    bool mandatoryReady = diagnostics.Ready && online && localCaptureSinksActive && nativeGameplayCoreContractActive;
    const char* reason = "ready";
    for (const auto& domain : kDomains)
    {
        if (!IsMandatoryDomainReady(domain, diagnostics, online, planckInterface002Operational))
        {
            mandatoryReady = false;
            reason = domain.FixedReason[0] != '\0' ? domain.FixedReason :
                DomainState(domain, diagnostics, online, planckInterface002Operational);
            break;
        }
    }
    const char* state = "ready";
    if (!mandatoryReady)
    {
        if (!diagnostics.Ready)
        {
            if (diagnostics.Retired)
            {
                state = "retired";
                reason = "bridge_retired";
            }
            else if (diagnostics.EndpointState == static_cast<std::uint32_t>(GameplayBridge::EndpointState::Faulted))
            {
                state = "faulted";
                reason = "bridge_faulted";
            }
            else
            {
                state = "waiting_bridge";
                reason = "bridge_not_ready";
            }
        }
        else if (!localCaptureSinksActive)
        {
            state = "degraded";
            reason = "local_capture_sinks_inactive";
        }
        else if (!nativeGameplayCoreContractActive)
        {
            state = "degraded";
            reason = "native_gameplay_core_contract_inactive";
        }
        else if (!online)
        {
            state = "waiting_transport";
            reason = "transport_offline";
        }
        else
            state = "degraded";
    }
    LogStateTransition(state, reason);

    // Keep a failed publish dirty, but do not retry filesystem I/O on every
    // game update. Status transitions remain logged immediately above.
    if (!aForce && m_statusTimer < kStatusWriteInterval)
        return;

    const bool published = WriteGameplayStatusSnapshot(
        m_statusPath, nullptr, diagnostics, &m_transport, &m_connection,
        m_counters, m_movementCounters, m_saveLoadCounters, true, state, reason);
    if (published)
    {
        if (m_snapshotWriteFailureLogged)
            spdlog::info("SkyrimTogetherVR gameplay diagnostics snapshot publishing restored");
        m_snapshotWriteFailureLogged = false;
        m_statusDirty = false;
    }
    else
    {
        if (!m_snapshotWriteFailureLogged)
            spdlog::warn("SkyrimTogetherVR gameplay diagnostics could not publish snapshot; retrying");
        m_snapshotWriteFailureLogged = true;
        m_statusDirty = true;
    }
    m_statusTimer = 0.0;
}

void VRGameplayDiagnosticsService::LogStateTransition(const char* apState, const char* apReason) noexcept
{
    if (m_lastState == apState && m_lastReason == apReason)
        return;
    m_lastState = apState;
    m_lastReason = apReason;
    if (std::string_view(apState) == "degraded" || std::string_view(apState) == "connection_error" ||
        std::string_view(apState) == "faulted")
        spdlog::warn("SkyrimTogetherVR gameplay diagnostics state={} reason={}", apState, apReason);
    else
        spdlog::info("SkyrimTogetherVR gameplay diagnostics state={} reason={}", apState, apReason);
}

void VRGameplayDiagnosticsService::LogRateLimitedSummary() noexcept
{
    if (m_summaryTimer < kSummaryLogInterval)
        return;
    m_summaryTimer = 0.0;

    const auto diagnostics = SkyrimTogetherVR::GameplayBridgeClient::GetDiagnostics();
    const bool changed = diagnostics.RejectedCommandCount != m_lastRejectedCommandCount ||
                         diagnostics.StaleCommandCount != m_lastStaleCommandCount ||
                         diagnostics.EventRingDroppedPushCount != m_lastDroppedEventCount ||
                         diagnostics.CommandRingDroppedPushCount != m_lastDroppedCommandCount ||
                         diagnostics.ActorAuthority != m_lastActorAuthorityDiagnostics;
    m_lastRejectedCommandCount = diagnostics.RejectedCommandCount;
    m_lastStaleCommandCount = diagnostics.StaleCommandCount;
    m_lastDroppedEventCount = diagnostics.EventRingDroppedPushCount;
    m_lastDroppedCommandCount = diagnostics.CommandRingDroppedPushCount;
    m_lastActorAuthorityDiagnostics = diagnostics.ActorAuthority;
    const auto& authority = diagnostics.ActorAuthority;
    const bool authorityFailure = authority.FailedRemoteNpcHealthDeltaPublicationCount != 0 || authority.LeaseFailureCount != 0 ||
                                  authority.RetirementFailureCount != 0 || authority.RetirementTimeoutCount != 0 ||
                                  authority.RegistryInconsistencyCount != 0;
    if (!changed || (diagnostics.RejectedCommandCount == 0 && diagnostics.StaleCommandCount == 0 &&
                     diagnostics.EventRingDroppedPushCount == 0 && diagnostics.CommandRingDroppedPushCount == 0 &&
                     authority.SuppressedDamageCount == 0 && authority.SuppressedDeathItemsCount == 0 &&
                     authority.SuppressedPositiveActiveEffectHealthCount == 0 && authority.SuppressedRestoreHealthCount == 0 &&
                     authority.SuppressedReferenceSetPositionCount == 0 && authority.SuppressedActorSetPositionCount == 0 &&
                     authority.SuppressedMoveToCount == 0 && authority.SuppressedActorProcessCount == 0 &&
                     authority.PublishedRemoteNpcHealthDeltaCount == 0 && !authorityFailure))
        return;

    if (authorityFailure || diagnostics.RejectedCommandCount != 0 || diagnostics.StaleCommandCount != 0 ||
        diagnostics.EventRingDroppedPushCount != 0 || diagnostics.CommandRingDroppedPushCount != 0)
        spdlog::warn(
            "SkyrimTogetherVR gameplay diagnostics summary: rejectedCommands={}, staleCommands={}, eventDrops={}, commandDrops={}, "
            "authority(suppressedDamage={}, deathItems={}, activeEffectHealth={}, restoreHealth={}, referenceSetPosition={}, actorSetPosition={}, moveTo={}, actorProcess={}, "
            "publishedNpcHealth={}, failedNpcHealth={}, leaseFailures={}, retirementFailures={}, retirementTimeouts={}, registryInconsistencies={})",
            diagnostics.RejectedCommandCount, diagnostics.StaleCommandCount, diagnostics.EventRingDroppedPushCount,
            diagnostics.CommandRingDroppedPushCount, authority.SuppressedDamageCount, authority.SuppressedDeathItemsCount,
            authority.SuppressedPositiveActiveEffectHealthCount, authority.SuppressedRestoreHealthCount,
            authority.SuppressedReferenceSetPositionCount, authority.SuppressedActorSetPositionCount, authority.SuppressedMoveToCount,
            authority.SuppressedActorProcessCount, authority.PublishedRemoteNpcHealthDeltaCount,
            authority.FailedRemoteNpcHealthDeltaPublicationCount, authority.LeaseFailureCount, authority.RetirementFailureCount,
            authority.RetirementTimeoutCount, authority.RegistryInconsistencyCount);
    else
        spdlog::info(
            "SkyrimTogetherVR gameplay authority summary: suppressedDamage={}, deathItems={}, activeEffectHealth={}, restoreHealth={}, "
            "referenceSetPosition={}, actorSetPosition={}, moveTo={}, actorProcess={}, publishedNpcHealth={}",
            authority.SuppressedDamageCount, authority.SuppressedDeathItemsCount,
            authority.SuppressedPositiveActiveEffectHealthCount, authority.SuppressedRestoreHealthCount,
            authority.SuppressedReferenceSetPositionCount, authority.SuppressedActorSetPositionCount,
            authority.SuppressedMoveToCount, authority.SuppressedActorProcessCount,
            authority.PublishedRemoteNpcHealthDeltaCount);
}

GameplayBridge::GameplayDomain VRGameplayDiagnosticsService::DomainForEvent(
    const GameplayBridge::EventRecord& acRecord) noexcept
{
    switch (static_cast<GameplayBridge::EventKind>(acRecord.Header.Kind))
    {
    case GameplayBridge::EventKind::LocalGameplayAction:
        return static_cast<GameplayBridge::GameplayDomain>(acRecord.Payload.LocalGameplayAction.Domain);
    case GameplayBridge::EventKind::LocalGameplayTextChunk:
        return static_cast<GameplayBridge::GameplayDomain>(acRecord.Payload.LocalGameplayTextChunk.Domain);
    case GameplayBridge::EventKind::LocalProjectileLaunch:
        return GameplayBridge::GameplayDomain::Projectile;
    case GameplayBridge::EventKind::LocalActorActionMetadata:
    case GameplayBridge::EventKind::LocalActorActionGraphChunk:
    case GameplayBridge::EventKind::LocalActorActionTextChunk:
        return GameplayBridge::GameplayDomain::Animation;
    case GameplayBridge::EventKind::LocalAnimationGraphChunk:
        return GameplayBridge::GameplayDomain::Animation;
    default:
        return static_cast<GameplayBridge::GameplayDomain>(0);
    }
}
