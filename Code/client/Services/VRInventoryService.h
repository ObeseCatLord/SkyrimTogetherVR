#pragma once

#include <cstddef>
#include <filesystem>

#include <Structs/VREquipmentUpdate.h>
#include <TiltedCore/Stl.hpp>

struct DisconnectedEvent;
struct NotifyPlayerLeft;
struct NotifyVREquipmentUpdate;
struct TransportService;
struct UpdateEvent;
struct World;

struct VRInventoryService
{
    VRInventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRInventoryService() noexcept = default;

    TP_NOCOPYMOVE(VRInventoryService);

    [[nodiscard]] bool HasLocalEquipment() const noexcept { return m_hasEquipment; }
    [[nodiscard]] const VREquipmentUpdate& GetLocalEquipment() const noexcept { return m_lastEquipment; }
    [[nodiscard]] size_t GetRemoteEquipmentCount() const noexcept { return m_remoteEquipment.size(); }
    [[nodiscard]] const TiltedPhoques::Map<uint32_t, VREquipmentUpdate>& GetRemoteEquipment() const noexcept { return m_remoteEquipment; }

private:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnVREquipmentUpdate(const NotifyVREquipmentUpdate& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void SendEquipmentUpdate() noexcept;
    [[nodiscard]] bool CaptureLocalEquipment(VREquipmentUpdate& aUpdate) noexcept;
    void WriteInventoryStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_inventoryStatusPath;
    TiltedPhoques::Map<uint32_t, VREquipmentUpdate> m_remoteEquipment{};
    VREquipmentUpdate m_lastEquipment{};
    bool m_hasEquipment{false};
    double m_sendTimer{0.0};
    double m_statusTimer{0.0};
    bool m_statusDirty{true};
    uint32_t m_sequence{0};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_vrEquipmentUpdateConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_disconnectedConnection;
};
