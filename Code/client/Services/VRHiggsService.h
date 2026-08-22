#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <Structs/VRHiggsRelayState.h>
#include <Structs/VRHiggsState.h>
#include <TiltedCore/Stl.hpp>

struct DisconnectedEvent;
struct NotifyPlayerLeft;
struct NotifyVRHiggsState;
struct TransportService;
struct UpdateEvent;
struct World;

struct VRHiggsService
{
    VRHiggsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRHiggsService() noexcept = default;

    TP_NOCOPYMOVE(VRHiggsService);

    [[nodiscard]] bool HasLocalHiggsState() const noexcept { return m_hasLocalState; }
    // Authentication reads the already authoritative HIGGS bridge snapshot.
    // It fails closed until HIGGS exposed its interface, callbacks, and a
    // nonzero snapshot sequence for this process.
    [[nodiscard]] bool RefreshLocalHiggsStateForAuthentication() noexcept;
    [[nodiscard]] bool IsLocalHiggsRelayOperational() const noexcept
    {
        return m_hasLocalState && IsVRHiggsRelayOperational(m_lastLocalState);
    }
    [[nodiscard]] const VRHiggsState& GetLocalHiggsState() const noexcept { return m_lastLocalState; }
    [[nodiscard]] size_t GetRemoteHiggsStateCount() const noexcept { return m_remoteStates.size(); }
    [[nodiscard]] const TiltedPhoques::Map<uint32_t, VRHiggsState>& GetRemoteHiggsStates() const noexcept { return m_remoteStates; }

private:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnVRHiggsState(const NotifyVRHiggsState& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void PruneRemoteStates(double aDelta) noexcept;
    [[nodiscard]] bool CaptureLocalHiggsState(VRHiggsState& aState) noexcept;
    void ReportHiggsBridgeReadoutRejection(std::uint8_t aRejection, const char* apReason) noexcept;
    void ReportHiggsBridgeReadoutAccepted() noexcept;
    void MergeMutationReplayWindow(VRHiggsState& arState, const std::string& acBridgeEpoch,
                                   bool aOnline) noexcept;
    void RebaseMutationReplayFloor(const std::string& acBridgeEpoch,
                                   uint32_t aMutationSequence) noexcept;
    void ClearLocalStateAtConnectionBoundary() noexcept;
    void SendHiggsState() noexcept;
    void WriteHiggsNetworkStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_bridgeStatusPath;
    std::filesystem::path m_networkStatusPath;
    TiltedPhoques::Map<uint32_t, VRHiggsState> m_remoteStates{};
    TiltedPhoques::Map<uint32_t, uint64_t> m_remoteProducerEpochs{};
    TiltedPhoques::Map<uint32_t, double> m_remoteStateAges{};
    VRHiggsState m_lastLocalState{};
    std::uint64_t m_networkProducerEpoch{0};
    std::array<VRHiggsEventSnapshot, kMaximumHiggsMutationEvents> m_mutationReplayWindow{};
    std::size_t m_mutationReplayEventCount{0};
    std::uint32_t m_lastCapturedMutationSequence{0};
    std::string m_bridgeEpoch{};
    std::string m_mutationReplayFloorEpoch{};
    std::uint32_t m_mutationReplayFloorSequence{0};
    bool m_hasCapturedMutationSequence{false};
    bool m_hasMutationReplayFloor{false};
    bool m_mutationReplayRebasePending{false};
    bool m_hasLocalState{false};
    bool m_wasOnline{false};
    bool m_bridgeReadInitialized{false};
    double m_sendTimer{0.0};
    double m_statusTimer{0.0};
    double m_bridgeReadTimer{0.0};
    std::uint8_t m_lastBridgeReadoutRejection{0};
    std::chrono::steady_clock::time_point m_lastBridgeReadoutLogTime{};
    bool m_statusDirty{true};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_vrHiggsStateConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_disconnectedConnection;
};
