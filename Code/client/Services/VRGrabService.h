#pragma once

#include <cstddef>
#include <filesystem>

#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Structs/VRGrabEvent.h>
#include <TiltedCore/Stl.hpp>

struct DisconnectedEvent;
struct NotifyPlayerLeft;
struct NotifyVRGrabEvent;
struct TESGrabReleaseEvent;
struct TransportService;
struct UpdateEvent;
struct World;

struct VRGrabService final : BSTEventSink<TESGrabReleaseEvent>
{
    VRGrabService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRGrabService() noexcept = default;

    TP_NOCOPYMOVE(VRGrabService);

    [[nodiscard]] bool HasLocalGrab() const noexcept { return m_hasLocalGrab; }
    [[nodiscard]] size_t GetRemoteGrabCount() const noexcept { return m_remoteGrabs.size(); }
    [[nodiscard]] const TiltedPhoques::Map<uint32_t, VRGrabEvent>& GetRemoteGrabs() const noexcept
    {
        return m_remoteGrabs;
    }

private:
    BSTEventResult OnEvent(const TESGrabReleaseEvent* apEvent, const EventDispatcher<TESGrabReleaseEvent>* apSender) override;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnVRGrabEvent(const NotifyVRGrabEvent& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void PruneRemoteGrabs(double aDelta) noexcept;
    [[nodiscard]] bool CaptureGrab(const TESGrabReleaseEvent& acEvent, VRGrabEvent& aGrab) noexcept;
    void SendGrabEvent(const VRGrabEvent& acGrab) noexcept;
    void WriteGrabStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_grabStatusPath;
    TiltedPhoques::Map<uint32_t, VRGrabEvent> m_remoteGrabs{};
    TiltedPhoques::Map<uint32_t, double> m_remoteGrabAges{};
    VRGrabEvent m_lastLocalGrab{};
    bool m_hasLocalGrab{false};
    double m_statusTimer{0.0};
    bool m_statusDirty{true};
    uint32_t m_sequence{0};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_vrGrabEventConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_disconnectedConnection;
};
