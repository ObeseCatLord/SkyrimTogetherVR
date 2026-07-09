#pragma once

#include <cstddef>
#include <filesystem>

#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Structs/VRProjectileEvent.h>
#include <TiltedCore/Stl.hpp>

struct DisconnectedEvent;
struct NotifyPlayerLeft;
struct NotifyVRProjectileEvent;
struct TESPlayerBowShotEvent;
struct TESSpellCastEvent;
struct TransportService;
struct UpdateEvent;
struct World;

struct VRProjectileService final : BSTEventSink<TESPlayerBowShotEvent>, BSTEventSink<TESSpellCastEvent>
{
    VRProjectileService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRProjectileService() noexcept = default;

    TP_NOCOPYMOVE(VRProjectileService);

    [[nodiscard]] bool HasLocalProjectileEvent() const noexcept { return m_hasLocalProjectileEvent; }
    [[nodiscard]] size_t GetRemoteProjectileEventCount() const noexcept { return m_remoteProjectileEvents.size(); }
    [[nodiscard]] const TiltedPhoques::Map<uint32_t, VRProjectileEvent>& GetRemoteProjectileEvents() const noexcept
    {
        return m_remoteProjectileEvents;
    }

private:
    BSTEventResult OnEvent(const TESPlayerBowShotEvent* apEvent, const EventDispatcher<TESPlayerBowShotEvent>* apSender) override;
    BSTEventResult OnEvent(const TESSpellCastEvent* apEvent, const EventDispatcher<TESSpellCastEvent>* apSender) override;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnVRProjectileEvent(const NotifyVRProjectileEvent& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void PruneRemoteProjectileEvents(double aDelta) noexcept;
    [[nodiscard]] bool CaptureBowShot(const TESPlayerBowShotEvent& acEvent, VRProjectileEvent& aProjectile) noexcept;
    [[nodiscard]] bool CaptureSpellCast(const TESSpellCastEvent& acEvent, VRProjectileEvent& aProjectile) noexcept;
    void SendProjectileEvent(const VRProjectileEvent& acProjectile) noexcept;
    void WriteProjectileStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_projectileStatusPath;
    TiltedPhoques::Map<uint32_t, VRProjectileEvent> m_remoteProjectileEvents{};
    TiltedPhoques::Map<uint32_t, double> m_remoteProjectileEventAges{};
    VRProjectileEvent m_lastLocalProjectileEvent{};
    bool m_hasLocalProjectileEvent{false};
    double m_statusTimer{0.0};
    bool m_statusDirty{true};
    uint32_t m_sequence{0};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_vrProjectileEventConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_disconnectedConnection;
};
