#include <catch2/catch.hpp>

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
