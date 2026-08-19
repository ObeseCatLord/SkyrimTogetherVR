#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <entt/entt.hpp>

#include <VRGameplayBridge.h>
#include <vr_common/VRGameplayBridge.h>

struct ConnectedEvent;
struct ConnectionErrorEvent;
struct DisconnectedEvent;
struct TransportService;
struct VRConnectionService;
struct UpdateEvent;
struct VRCompatibilityStatus;

namespace SkyrimTogetherVR
{
struct LocalGameplayBridgeEvent;
struct RemoteGameplayBridgeResultEvent;
}

// Produces one versioned, atomic gameplay-readiness snapshot.  It observes the
// canonical CommonLib bridge only; legacy mapped-client observers are not a
// readiness signal for the VR gameplay target.
struct VRGameplayDiagnosticsService final
{
    static constexpr std::size_t kDomainCount = 17;

    struct DomainCounters
    {
        std::uint64_t Captured{};
        std::uint64_t Sent{};
        std::uint64_t Applied{};
        std::uint64_t Rejected{};
    };

    VRGameplayDiagnosticsService(entt::dispatcher& aDispatcher, TransportService& aTransport,
                                 VRConnectionService& aConnection) noexcept;
    ~VRGameplayDiagnosticsService() noexcept;

    TP_NOCOPYMOVE(VRGameplayDiagnosticsService);

    // The canonical local sender accepts requests asynchronously.  Its two
    // call sites can report final queue acceptance without adding a new event
    // type or changing the gameplay wire protocol.
    void RecordOutboundAccepted(
        SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain) noexcept;
    void RecordOutboundRejected(
        SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain) noexcept;
    void RecordMovementAccepted() noexcept;

    // Keep the counter array coupled to the explicitly described wire-domain
    // order. GameplayDomain values are protocol values, not array indices.
    [[nodiscard]] static constexpr std::size_t DomainIndex(
        const SkyrimTogetherVR::GameplayBridge::GameplayDomain aDomain) noexcept
    {
        using Domain = SkyrimTogetherVR::GameplayBridge::GameplayDomain;
        switch (aDomain)
        {
        case Domain::Animation: return 0;
        case Domain::Appearance: return 1;
        case Domain::Equipment: return 2;
        case Domain::Inventory: return 3;
        case Domain::ActorState: return 4;
        case Domain::Object: return 5;
        case Domain::Combat: return 6;
        case Domain::Projectile: return 7;
        case Domain::Magic: return 8;
        case Domain::Quest: return 9;
        case Domain::Dialogue: return 10;
        case Domain::Party: return 11;
        case Domain::WorldState: return 12;
        case Domain::VrBodyPose: return 13;
        case Domain::Higgs: return 14;
        case Domain::Planck: return 15;
        case Domain::NpcOwnership: return 16;
        }

        return kDomainCount;
    }

    // Called before World exists so launch tooling never mistakes a static
    // compatibility report for live gameplay readiness.
    static void PublishBootstrapSnapshot(
        const std::filesystem::path& acGamePath,
        const VRCompatibilityStatus& acStatus) noexcept;

private:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnConnectionError(const ConnectionErrorEvent& acEvent) noexcept;
    void OnLocalGameplay(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept;
    void OnGameplayResult(const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept;

    void RefreshSessionIdentity() noexcept;
    void ResetCountersForSession() noexcept;
    void ResetGameplayCounters() noexcept;
    void WriteSnapshot(bool aForce = false) noexcept;
    void LogStateTransition(const char* apState, const char* apReason) noexcept;
    void LogRateLimitedSummary() noexcept;

    [[nodiscard]] static SkyrimTogetherVR::GameplayBridge::GameplayDomain DomainForEvent(
        const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;

    TransportService& m_transport;
    VRConnectionService& m_connection;
    std::filesystem::path m_statusPath;
    std::array<DomainCounters, kDomainCount> m_counters{};
    DomainCounters m_movementCounters{};
    DomainCounters m_saveLoadCounters{};
    std::uint64_t m_sessionId{};
    std::uint64_t m_serverInstanceNonce{};
    std::uint64_t m_connectionGeneration{};
    std::uint64_t m_lifecycleEpoch{};
    std::uint64_t m_lastRejectedCommandCount{};
    std::uint64_t m_lastStaleCommandCount{};
    std::uint64_t m_lastDroppedEventCount{};
    std::uint64_t m_lastDroppedCommandCount{};
    SkyrimTogetherVR::GameplayBridgeClient::ActorAuthorityDiagnostics m_lastActorAuthorityDiagnostics{};
    std::string m_lastState{"bootstrap"};
    std::string m_lastReason{"service_registered"};
    double m_statusTimer{1.0};
    double m_summaryTimer{};
    bool m_statusDirty{true};
    bool m_sessionIdentityInitialized{};
    bool m_saveLoadRehydrationPending{};
    bool m_snapshotWriteFailureLogged{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_connectionErrorConnection;
    entt::scoped_connection m_localGameplayConnection;
    entt::scoped_connection m_gameplayResultConnection;
};
