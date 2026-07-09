#pragma once

#include <cstddef>
#include <filesystem>

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
    void SendHiggsState() noexcept;
    void WriteHiggsNetworkStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_bridgeStatusPath;
    std::filesystem::path m_networkStatusPath;
    TiltedPhoques::Map<uint32_t, VRHiggsState> m_remoteStates{};
    TiltedPhoques::Map<uint32_t, double> m_remoteStateAges{};
    VRHiggsState m_lastLocalState{};
    bool m_hasLocalState{false};
    bool m_bridgeReadInitialized{false};
    double m_sendTimer{0.0};
    double m_statusTimer{0.0};
    double m_bridgeReadTimer{0.0};
    bool m_statusDirty{true};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_vrHiggsStateConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_disconnectedConnection;
};
