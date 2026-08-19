#include <catch2/catch.hpp>

#include "../vr_common/VRGameplayBridge.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
std::string ReadRepositorySource(const std::filesystem::path& aRelativePath)
{
    auto directory = std::filesystem::current_path();
    while (true) {
        const auto candidate = directory / aRelativePath;
        if (std::filesystem::is_regular_file(candidate)) {
            std::ifstream file(candidate, std::ios::binary);
            return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        }

        const auto parent = directory.parent_path();
        if (parent == directory)
            return {};
        directory = parent;
    }
}
} // namespace

TEST_CASE("VR gameplay diagnostics expose operational readiness without claiming two-client proof", "[skyrim-vr][telemetry]")
{
    const auto diagnostics = ReadRepositorySource("Code/client/Services/Generic/VRGameplayDiagnosticsService.cpp");
    const auto compatibility = ReadRepositorySource("Code/client/VRCompatibilityStatus.cpp");

    REQUIRE_FALSE(diagnostics.empty());
    REQUIRE_FALSE(compatibility.empty());
    REQUIRE(diagnostics.find("SkyrimTogetherVR.gameplay") != std::string::npos);
    REQUIRE(diagnostics.find("WriteFileAtomically") != std::string::npos);
    REQUIRE(diagnostics.find("schemaVersion=") != std::string::npos);
    REQUIRE(diagnostics.find("LocalGameplayBridgeEvent") != std::string::npos);
    REQUIRE(diagnostics.find("RemoteGameplayBridgeResultEvent") != std::string::npos);
    REQUIRE(diagnostics.find("RecordOutboundAccepted") != std::string::npos);
    REQUIRE(diagnostics.find("transport_queue_acceptance") != std::string::npos);
    REQUIRE(diagnostics.find("operationalReady=") != std::string::npos);
    REQUIRE(diagnostics.find("bridge.localCaptureSinksActive=") != std::string::npos);
    REQUIRE(diagnostics.find("Capability::LocalCaptureSinks") != std::string::npos);
    REQUIRE(diagnostics.find("readinessScope=operational_availability") != std::string::npos);
    REQUIRE(diagnostics.find("evidence.scope=local_process_counters") != std::string::npos);
    REQUIRE(diagnostics.find("evidence.twoClientProof=0") != std::string::npos);
    REQUIRE(diagnostics.find("unproven_requires_paired_snapshots") != std::string::npos);
    REQUIRE(diagnostics.find(".evidenceState=") != std::string::npos);
    REQUIRE(diagnostics.find("GameplayDomain::Quest, \"quest\", DomainPath::Canonical") != std::string::npos);
    REQUIRE(diagnostics.find("GameplayDomain::Combat, \"combat\", DomainPath::Canonical") != std::string::npos);
    REQUIRE(diagnostics.find("no_remote_physical_replay") != std::string::npos);
    REQUIRE(diagnostics.find("kSummaryLogInterval = 30.0") != std::string::npos);
    REQUIRE(diagnostics.find("domain.save_load.evidenceType=lifecycle_rehydration") != std::string::npos);
    REQUIRE(diagnostics.find("domain.save_load.networkTraffic=not_applicable") != std::string::npos);
    REQUIRE(compatibility.find("ready=0") != std::string::npos);
    REQUIRE(compatibility.find("readinessSource=SkyrimTogetherVR.gameplay") != std::string::npos);
    REQUIRE(compatibility.find("file << \"ready=1") == std::string::npos);
    REQUIRE(compatibility.find("GetGameplayServicePolicy") == std::string::npos);
}

TEST_CASE("VR gameplay diagnostics use an explicit domain descriptor map", "[skyrim-vr][telemetry]")
{
    const auto diagnostics = ReadRepositorySource("Code/client/Services/Generic/VRGameplayDiagnosticsService.cpp");
    const auto header = ReadRepositorySource("Code/client/Services/Generic/VRGameplayDiagnosticsService.h");

    REQUIRE_FALSE(diagnostics.empty());
    REQUIRE_FALSE(header.empty());
    REQUIRE(header.find("return value >= 1") == std::string::npos);
    REQUIRE(header.find("value - 1") == std::string::npos);
    REQUIRE(header.find("case Domain::Animation: return 0;") != std::string::npos);
    REQUIRE(header.find("case Domain::NpcOwnership: return 16;") != std::string::npos);
    REQUIRE(diagnostics.find("DomainDescriptorsMatchCounterIndices") != std::string::npos);
    REQUIRE(diagnostics.find("static_assert(DomainDescriptorsMatchCounterIndices())") != std::string::npos);
    REQUIRE(diagnostics.find("LifecycleRehydrationEvidenceState") != std::string::npos);
    REQUIRE(diagnostics.find("HasObservedLifecycleRehydration") != std::string::npos);
    REQUIRE(diagnostics.find("m_saveLoadCounters = {}") != std::string::npos);
    REQUIRE(diagnostics.find("m_saveLoadRehydrationPending") != std::string::npos);
    REQUIRE(diagnostics.find("sessionBindingChanged") != std::string::npos);
    REQUIRE(diagnostics.find("ResetGameplayCounters") != std::string::npos);
    REQUIRE(diagnostics.find("if (published)") != std::string::npos);
    REQUIRE(diagnostics.find("m_statusDirty = true;") != std::string::npos);
}

TEST_CASE("VR gameplay diagnostics surface bounded actor-authority aggregates", "[skyrim-vr][telemetry]")
{
    const auto diagnostics = ReadRepositorySource("Code/client/Services/Generic/VRGameplayDiagnosticsService.cpp");
    const auto bridge = ReadRepositorySource("Code/client/VRGameplayBridge.cpp");
    const auto shared = ReadRepositorySource("Code/vr_common/VRGameplayBridge.h");

    REQUIRE_FALSE(diagnostics.empty());
    REQUIRE_FALSE(bridge.empty());
    REQUIRE_FALSE(shared.empty());
    REQUIRE(shared.find("kMappingAbiVersion = 23") != std::string::npos);
    REQUIRE(shared.find("AuthoritySuppressedDamageCount") != std::string::npos);
    REQUIRE(shared.find("AuthorityPublishedRemoteNpcHealthDeltaCount") != std::string::npos);
    REQUIRE(shared.find("AuthorityLeaseFailureCount") != std::string::npos);
    REQUIRE(shared.find("AuthorityRetirementTimeoutCount") != std::string::npos);
    REQUIRE(shared.find("AuthorityRegistryInconsistencyCount") != std::string::npos);
    REQUIRE(bridge.find("diagnostics.ActorAuthority") != std::string::npos);
    REQUIRE(diagnostics.find("bridge.authority.suppressedDamage=") != std::string::npos);
    REQUIRE(diagnostics.find("bridge.authority.retirementFailures=") != std::string::npos);
    REQUIRE(diagnostics.find("bridge.authority.registryInconsistencies=") != std::string::npos);
    REQUIRE(diagnostics.find("m_lastActorAuthorityDiagnostics") != std::string::npos);
    REQUIRE(diagnostics.find("kSummaryLogInterval") != std::string::npos);
}

TEST_CASE("VR gameplay snapshot emits reconciled bridge loss attribution", "[skyrim-vr][telemetry]")
{
    using namespace SkyrimTogetherVR::GameplayBridge;

    const auto diagnostics = ReadRepositorySource("Code/client/Services/Generic/VRGameplayDiagnosticsService.cpp");
    const auto bridge = ReadRepositorySource("Code/client/VRGameplayBridge.cpp");
    REQUIRE_FALSE(diagnostics.empty());
    REQUIRE_FALSE(bridge.empty());

    const auto writerBegin = diagnostics.find("bool WriteGameplayStatusSnapshot(");
    const auto writerEnd = diagnostics.find(
        "VRGameplayDiagnosticsService::VRGameplayDiagnosticsService(", writerBegin);
    REQUIRE(writerBegin != std::string::npos);
    REQUIRE(writerEnd != std::string::npos);
    const auto writer = diagnostics.substr(writerBegin, writerEnd - writerBegin);

    REQUIRE(writer.find("bridge.discardedEvents=") != std::string::npos);
    REQUIRE(writer.find("bridge.discardedEvents.preReady=") != std::string::npos);
    REQUIRE(writer.find("bridge.discardedEvents.lifecycleRetired=") != std::string::npos);
    REQUIRE(writer.find("bridge.discardedEvents.other=") != std::string::npos);
    REQUIRE(writer.find("bridge.rejectedSubmissions=") != std::string::npos);
    REQUIRE(writer.find("bridge.rejectedSubmissions.preReady=") != std::string::npos);
    REQUIRE(writer.find("bridge.rejectedSubmissions.lifecycleRetired=") != std::string::npos);
    REQUIRE(writer.find("bridge.rejectedSubmissions.other=") != std::string::npos);
    REQUIRE(writer.find("bridge.eventRingDroppedPushes=") != std::string::npos);

    REQUIRE(bridge.find("diagnostics.DiscardedEventCount = ReconciledAttributionTotal(") != std::string::npos);
    REQUIRE(bridge.find("diagnostics.RejectedSubmissionCount = ReconciledAttributionTotal(") != std::string::npos);
    const auto total = ReconciledAttributionTotal(3, 4, 5);
    REQUIRE(AreAttributionCountersReconciled(total, 3, 4, 5));
}
