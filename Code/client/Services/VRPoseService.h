#pragma once

#include <filesystem>

#include <Structs/VRPoseUpdate.h>
#include <TiltedCore/Stl.hpp>
#include <VR/VRPlayerPose.h>

struct NotifyVRPoseUpdate;
struct NotifyPlayerLeft;
struct DisconnectedEvent;
struct TransportService;
struct UpdateEvent;

struct VRPoseService
{
    VRPoseService(entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRPoseService() noexcept = default;

    TP_NOCOPYMOVE(VRPoseService);

    [[nodiscard]] const VRPlayerPoseSnapshot& GetLastSnapshot() const noexcept { return m_lastSnapshot; }
    [[nodiscard]] bool HasSnapshot() const noexcept { return m_hasSnapshot; }
    [[nodiscard]] const VRVrikData& GetLocalVrikData() const noexcept { return m_localVrik; }
    [[nodiscard]] const TiltedPhoques::Map<uint32_t, VRPoseUpdate>& GetRemotePoses() const noexcept { return m_remotePoses; }

private:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnVRPoseUpdate(const NotifyVRPoseUpdate& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void LogSnapshot() const noexcept;
    void PruneRemotePoses(double aDelta) noexcept;
    void WritePoseStatusFile() noexcept;
    void SendPoseUpdate() noexcept;

    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_poseStatusPath;
    VRPlayerPoseSnapshot m_lastSnapshot{};
    VRVrikData m_localVrik{};
    TiltedPhoques::Map<uint32_t, VRPoseUpdate> m_remotePoses{};
    TiltedPhoques::Map<uint32_t, double> m_remotePoseAges{};
    bool m_hasSnapshot{false};
    bool m_vrikBridgeInitialized{false};
    double m_logTimer{0.0};
    double m_sendTimer{0.0};
    double m_poseStatusTimer{0.0};
    double m_vrikBridgeReadTimer{0.0};
    bool m_poseStatusDirty{true};
    uint32_t m_sequence{0};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_vrPoseUpdateConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_disconnectedConnection;
};
