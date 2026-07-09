#pragma once

#include <cstddef>
#include <filesystem>

#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Structs/VRMagicEffectEvent.h>
#include <TiltedCore/Stl.hpp>

struct DisconnectedEvent;
struct NotifyPlayerLeft;
struct NotifyVRMagicEffectEvent;
struct TESMagicEffectApplyEvent;
struct TransportService;
struct UpdateEvent;
struct World;

struct VRMagicService final : BSTEventSink<TESMagicEffectApplyEvent>
{
    VRMagicService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRMagicService() noexcept = default;

    TP_NOCOPYMOVE(VRMagicService);

    [[nodiscard]] bool HasLocalMagicEffect() const noexcept { return m_hasLocalMagicEffect; }
    [[nodiscard]] size_t GetRemoteMagicEffectCount() const noexcept { return m_remoteMagicEffects.size(); }
    [[nodiscard]] const TiltedPhoques::Map<uint32_t, VRMagicEffectEvent>& GetRemoteMagicEffects() const noexcept
    {
        return m_remoteMagicEffects;
    }

private:
    BSTEventResult OnEvent(const TESMagicEffectApplyEvent* apEvent, const EventDispatcher<TESMagicEffectApplyEvent>* apSender) override;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnVRMagicEffectEvent(const NotifyVRMagicEffectEvent& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void PruneRemoteMagicEffects(double aDelta) noexcept;
    [[nodiscard]] bool CaptureMagicEffect(const TESMagicEffectApplyEvent& acEvent, VRMagicEffectEvent& aMagicEffect) noexcept;
    void SendMagicEffectEvent(const VRMagicEffectEvent& acMagicEffect) noexcept;
    void WriteMagicStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_magicStatusPath;
    TiltedPhoques::Map<uint32_t, VRMagicEffectEvent> m_remoteMagicEffects{};
    TiltedPhoques::Map<uint32_t, double> m_remoteMagicEffectAges{};
    VRMagicEffectEvent m_lastLocalMagicEffect{};
    bool m_hasLocalMagicEffect{false};
    double m_statusTimer{0.0};
    bool m_statusDirty{true};
    uint32_t m_sequence{0};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_vrMagicEffectEventConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_disconnectedConnection;
};
