#pragma once

#include <cstddef>
#include <filesystem>

#include <Structs/VRMovementUpdate.h>
#include <TiltedCore/Stl.hpp>

struct DisconnectedEvent;
struct NotifyPlayerLeft;
struct NotifyVRMovementUpdate;
struct TransportService;
struct UpdateEvent;
struct World;

struct VRMovementService
{
    VRMovementService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRMovementService() noexcept = default;

    TP_NOCOPYMOVE(VRMovementService);

    [[nodiscard]] bool HasLocalMovement() const noexcept { return m_hasMovement; }
    [[nodiscard]] const VRMovementUpdate& GetLocalMovement() const noexcept { return m_lastMovement; }
    [[nodiscard]] size_t GetRemoteMovementCount() const noexcept { return m_remoteMovements.size(); }
    [[nodiscard]] const TiltedPhoques::Map<uint32_t, VRMovementUpdate>& GetRemoteMovements() const noexcept { return m_remoteMovements; }

private:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnVRMovementUpdate(const NotifyVRMovementUpdate& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void PruneRemoteMovements(double aDelta) noexcept;
    void SendMovementUpdate() noexcept;
    [[nodiscard]] bool CaptureLocalMovement(VRMovementUpdate& aUpdate) noexcept;
    void WriteMovementStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_movementStatusPath;
    TiltedPhoques::Map<uint32_t, VRMovementUpdate> m_remoteMovements{};
    TiltedPhoques::Map<uint32_t, double> m_remoteMovementAges{};
    VRMovementUpdate m_lastMovement{};
    bool m_hasMovement{false};
    double m_sendTimer{0.0};
    double m_statusTimer{0.0};
    bool m_statusDirty{true};
    uint32_t m_sequence{0};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_vrMovementUpdateConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_disconnectedConnection;
};
