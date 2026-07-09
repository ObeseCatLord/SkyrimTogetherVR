#pragma once

#include <cstddef>
#include <filesystem>

#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Structs/VRActivationEvent.h>
#include <TiltedCore/Stl.hpp>

struct DisconnectedEvent;
struct NotifyPlayerLeft;
struct NotifyVRActivationEvent;
struct TESActivateEvent;
struct TransportService;
struct UpdateEvent;
struct World;

struct VRActivationService final : BSTEventSink<TESActivateEvent>
{
    VRActivationService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRActivationService() noexcept = default;

    TP_NOCOPYMOVE(VRActivationService);

    [[nodiscard]] bool HasLocalActivation() const noexcept { return m_hasLocalActivation; }
    [[nodiscard]] size_t GetRemoteActivationCount() const noexcept { return m_remoteActivations.size(); }
    [[nodiscard]] const TiltedPhoques::Map<uint32_t, VRActivationEvent>& GetRemoteActivations() const noexcept
    {
        return m_remoteActivations;
    }

private:
    BSTEventResult OnEvent(const TESActivateEvent* apEvent, const EventDispatcher<TESActivateEvent>* apSender) override;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnVRActivationEvent(const NotifyVRActivationEvent& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void PruneRemoteActivations(double aDelta) noexcept;
    [[nodiscard]] bool CaptureActivation(const TESActivateEvent& acEvent, VRActivationEvent& aActivation) noexcept;
    void SendActivationEvent(const VRActivationEvent& acActivation) noexcept;
    void WriteActivationStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_activationStatusPath;
    TiltedPhoques::Map<uint32_t, VRActivationEvent> m_remoteActivations{};
    TiltedPhoques::Map<uint32_t, double> m_remoteActivationAges{};
    VRActivationEvent m_lastLocalActivation{};
    bool m_hasLocalActivation{false};
    double m_statusTimer{0.0};
    bool m_statusDirty{true};
    uint32_t m_sequence{0};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_vrActivationEventConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_disconnectedConnection;
};
