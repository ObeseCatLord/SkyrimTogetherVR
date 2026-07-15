#include <TiltedOnlinePCH.h>

#include "World.h"

#include <Services/DiscoveryService.h>
#include <Services/InputService.h>
#include <Services/TransportService.h>
#include <Services/RunnerService.h>
#include <Services/ImguiService.h>
#include <Services/PapyrusService.h>
#include <Services/DiscordService.h>
#include <Services/ObjectService.h>
#include <Services/QuestService.h>
#include <Services/ActorValueService.h>
#include <Services/InventoryService.h>
#include <Services/MagicService.h>
#include <Services/CommandService.h>
#include <Services/CalendarService.h>
#include <Services/StringCacheService.h>
#include <Services/PlayerService.h>
#include <Services/CombatService.h>
#include <Services/WeatherService.h>
#include <Services/MapService.h>
#include <Services/VRPoseService.h>
#include <Services/VRConnectionService.h>
#include <Services/VRMovementService.h>
#include <Services/VRInventoryService.h>
#include <Services/VRRemotePlayerService.h>
#include <Services/VRActivationService.h>
#include <Services/VRMagicService.h>
#include <Services/VRCombatService.h>
#include <Services/VRProjectileService.h>
#include <Services/VRGrabService.h>
#include <Services/VRHiggsService.h>
#include <Services/VRSaveLoadService.h>
#include <Services/VRLifecycleService.h>

#include <Events/PreUpdateEvent.h>
#include <Events/UpdateEvent.h>

#include <ModCompat/BehaviorVar.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY
#define TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC
#define TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE
#define TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE
#define TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE
#define TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_POSE_SERVICE
#define TP_SKYRIM_VR_ENABLE_POSE_SERVICE 0
#endif

namespace
{
#if TP_SKYRIM_VR
void EmplaceSkyrimVRServices(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
{
#if TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR movement service is enabled in observation-only mode");
    aWorld.ctx().emplace<VRMovementService>(aWorld, aDispatcher, aTransport);
#endif
#if TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR inventory service is enabled in observation-only mode");
    aWorld.ctx().emplace<VRInventoryService>(aWorld, aDispatcher, aTransport);
#endif
#if TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR activation service is enabled in observation-only mode");
    aWorld.ctx().emplace<VRActivationService>(aWorld, aDispatcher, aTransport);
#endif
#if TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR magic service is enabled in observation-only mode");
    aWorld.ctx().emplace<VRMagicService>(aWorld, aDispatcher, aTransport);
#endif
#if TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR combat service is enabled in observation-only mode");
    aWorld.ctx().emplace<VRCombatService>(aWorld, aDispatcher, aTransport);
#endif
#if TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR projectile service is enabled in observation-only mode");
    aWorld.ctx().emplace<VRProjectileService>(aWorld, aDispatcher, aTransport);
#endif
#if TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR grab service is enabled in observation-only mode");
    aWorld.ctx().emplace<VRGrabService>(aWorld, aDispatcher, aTransport);
#endif
#if TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR HIGGS service is enabled in observation-only mode");
    aWorld.ctx().emplace<VRHiggsService>(aWorld, aDispatcher, aTransport);
#endif
#if TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR save/load service is enabled in observation-only mode");
    aWorld.ctx().emplace<VRSaveLoadService>(aWorld, aDispatcher, aTransport);
#endif
    aWorld.ctx().emplace<VRConnectionService>(aWorld, aDispatcher, aTransport);
#if TP_SKYRIM_VR_ENABLE_POSE_SERVICE
    aWorld.ctx().emplace<VRPoseService>(aDispatcher, aTransport);
#endif
#if TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE
    spdlog::warn("SkyrimTogetherVR remote-player proxy service is enabled in readout-only mode");
    aWorld.ctx().emplace<VRRemotePlayerService>(aWorld, aDispatcher, aTransport);
#endif
}
#endif
} // namespace

World::World()
    : m_runner(m_dispatcher)
    , m_transport(*this, m_dispatcher)
    , m_modSystem(m_dispatcher)
    , m_lastFrameTime{std::chrono::high_resolution_clock::now()}
{
    ctx().emplace<ImguiService>();
#if TP_SKYRIM_VR
    ctx().emplace<VRLifecycleService>(*this);
#endif
#if TP_SKYRIM_VR && TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY
#if TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC
    spdlog::warn("SkyrimTogetherVR remote avatar validation mode: CharacterService is enabled while full gameplay services stay disabled");
#else
    spdlog::warn("SkyrimTogetherVR connection-only mode: gameplay sync services are disabled");
#endif
#if TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE
    spdlog::warn("SkyrimTogetherVR discovery service is enabled in observation-only mode");
    ctx().emplace<DiscoveryService>(*this, m_dispatcher);
#endif
#if TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE
    spdlog::warn("SkyrimTogetherVR player cell service is enabled in network-only mode");
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport);
#endif
#if TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR movement service is enabled in observation-only mode");
    ctx().emplace<VRMovementService>(*this, m_dispatcher, m_transport);
#endif
#if TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR inventory service is enabled in observation-only mode");
    ctx().emplace<VRInventoryService>(*this, m_dispatcher, m_transport);
#endif
#if TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR activation service is enabled in observation-only mode");
    ctx().emplace<VRActivationService>(*this, m_dispatcher, m_transport);
#endif
#if TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR magic service is enabled in observation-only mode");
    ctx().emplace<VRMagicService>(*this, m_dispatcher, m_transport);
#endif
#if TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR combat service is enabled in observation-only mode");
    ctx().emplace<VRCombatService>(*this, m_dispatcher, m_transport);
#endif
#if TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR projectile service is enabled in observation-only mode");
    ctx().emplace<VRProjectileService>(*this, m_dispatcher, m_transport);
#endif
#if TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR grab service is enabled in observation-only mode");
    ctx().emplace<VRGrabService>(*this, m_dispatcher, m_transport);
#endif
#if TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR HIGGS service is enabled in observation-only mode");
    ctx().emplace<VRHiggsService>(*this, m_dispatcher, m_transport);
#endif
#if TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE
    spdlog::warn("SkyrimTogetherVR save/load service is enabled in observation-only mode");
    ctx().emplace<VRSaveLoadService>(*this, m_dispatcher, m_transport);
#endif
    ctx().emplace<DiscordService>(m_dispatcher);
    ctx().emplace<StringCacheService>(m_dispatcher);
    ctx().emplace<VRConnectionService>(*this, m_dispatcher, m_transport);
#if TP_SKYRIM_VR_ENABLE_POSE_SERVICE
    ctx().emplace<VRPoseService>(m_dispatcher, m_transport);
#endif
#if TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE
    spdlog::warn("SkyrimTogetherVR remote-player proxy service is enabled in readout-only mode");
    ctx().emplace<VRRemotePlayerService>(*this, m_dispatcher, m_transport);
#endif
#if TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC
    ctx().emplace<PartyService>(*this, m_dispatcher, m_transport);
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport);
#endif
#else
#if TP_SKYRIM_VR
    spdlog::warn("SkyrimTogetherVR gameplay mode: normal gameplay services are enabled with VR relay services and flat overlay disabled by default");
#endif
    ctx().emplace<DiscoveryService>(*this, m_dispatcher);
    ctx().emplace<OverlayService>(*this, m_transport, m_dispatcher);
    ctx().emplace<InputService>(ctx().at<OverlayService>());
#if TP_SKYRIM_VR
    EmplaceSkyrimVRServices(*this, m_dispatcher, m_transport);
#endif
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport);
    ctx().emplace<DebugService>(m_dispatcher, *this, m_transport, ctx().at<ImguiService>());
    ctx().emplace<PapyrusService>(m_dispatcher);
    ctx().emplace<DiscordService>(m_dispatcher);
    ctx().emplace<ObjectService>(*this, m_dispatcher, m_transport);
    ctx().emplace<CalendarService>(*this, m_dispatcher, m_transport);
    ctx().emplace<QuestService>(*this, m_dispatcher);
    ctx().emplace<PartyService>(*this, m_dispatcher, m_transport);
    ctx().emplace<ActorValueService>(*this, m_dispatcher, m_transport);
    ctx().emplace<InventoryService>(*this, m_dispatcher, m_transport);
    ctx().emplace<MagicService>(*this, m_dispatcher, m_transport);
    ctx().emplace<CommandService>(*this, m_transport, m_dispatcher);
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport);
    ctx().emplace<StringCacheService>(m_dispatcher);
    ctx().emplace<CombatService>(*this, m_transport, m_dispatcher);
    ctx().emplace<WeatherService>(*this, m_transport, m_dispatcher);
    ctx().emplace<MapService>(*this, m_dispatcher, m_transport);

    BehaviorVar::Get()->Init();
#endif
}

World::~World() = default;

void World::Update() noexcept
{
    const auto cNow = std::chrono::high_resolution_clock::now();
    const auto cDelta = cNow - m_lastFrameTime;
    m_lastFrameTime = cNow;

    auto cDeltaSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(cDelta).count();

#if TP_SKYRIM_VR
    constexpr double kMaximumUpdateDeltaSeconds = 0.25;
    cDeltaSeconds = std::clamp(cDeltaSeconds, 0.0, kMaximumUpdateDeltaSeconds);
    auto& lifecycle = ctx().at<VRLifecycleService>();
    lifecycle.Update(cDeltaSeconds);
    ctx().at<VRConnectionService>().HandleLifecycleBoundary();
    if (!lifecycle.IsReady())
        return;
#endif

    m_dispatcher.trigger(PreUpdateEvent(cDeltaSeconds));

    // Force run this before so we get the tasks scheduled to run
    m_runner.OnUpdate(UpdateEvent(cDeltaSeconds));
    m_dispatcher.trigger(UpdateEvent(cDeltaSeconds));
}

RunnerService& World::GetRunner() noexcept
{
    return m_runner;
}

TransportService& World::GetTransport() noexcept
{
    return m_transport;
}

ModSystem& World::GetModSystem() noexcept
{
    return m_modSystem;
}

uint64_t World::GetTick() const noexcept
{
    return m_transport.GetClock().GetCurrentTick();
}

bool World::Create() noexcept
{
    if (entt::locator<World>::has_value())
        return true;

    try
    {
        entt::locator<World>::emplace();
        return true;
    }
    catch (const std::exception& error)
    {
        spdlog::critical("SkyrimTogetherVR could not construct the client world: {}", error.what());
    }
    catch (...)
    {
        spdlog::critical("SkyrimTogetherVR could not construct the client world: unknown exception");
    }

    return false;
}

World& World::Get() noexcept
{
    return entt::locator<World>::value();
}
