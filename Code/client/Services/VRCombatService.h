#pragma once

#include <cstddef>
#include <filesystem>

#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Structs/VRCombatHitEvent.h>
#include <TiltedCore/Stl.hpp>

struct DisconnectedEvent;
struct NotifyPlayerLeft;
struct NotifyVRCombatHitEvent;
struct TESHitEvent;
struct TransportService;
struct UpdateEvent;
struct World;

struct VRCombatService final : BSTEventSink<TESHitEvent>
{
    VRCombatService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRCombatService() noexcept = default;

    TP_NOCOPYMOVE(VRCombatService);

    [[nodiscard]] bool HasLocalCombatHit() const noexcept { return m_hasLocalCombatHit; }
    [[nodiscard]] size_t GetRemoteCombatHitCount() const noexcept { return m_remoteCombatHits.size(); }
    [[nodiscard]] const TiltedPhoques::Map<uint32_t, VRCombatHitEvent>& GetRemoteCombatHits() const noexcept
    {
        return m_remoteCombatHits;
    }

private:
    BSTEventResult OnEvent(const TESHitEvent* apEvent, const EventDispatcher<TESHitEvent>* apSender) override;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnVRCombatHitEvent(const NotifyVRCombatHitEvent& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void PruneRemoteCombatHits(double aDelta) noexcept;
    [[nodiscard]] bool CaptureCombatHit(const TESHitEvent& acEvent, VRCombatHitEvent& aHit) noexcept;
    void SendCombatHitEvent(const VRCombatHitEvent& acHit) noexcept;
    void WriteCombatStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_combatStatusPath;
    TiltedPhoques::Map<uint32_t, VRCombatHitEvent> m_remoteCombatHits{};
    TiltedPhoques::Map<uint32_t, double> m_remoteCombatHitAges{};
    VRCombatHitEvent m_lastLocalCombatHit{};
    bool m_hasLocalCombatHit{false};
    double m_statusTimer{0.0};
    bool m_statusDirty{true};
    uint32_t m_sequence{0};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_vrCombatHitEventConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_disconnectedConnection;
};
