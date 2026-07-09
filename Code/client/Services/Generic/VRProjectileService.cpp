#include <TiltedOnlinePCH.h>

#include <Services/VRProjectileService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyVRProjectileEvent.h>
#include <Messages/RequestVRProjectileEvent.h>
#include <PlayerCharacter.h>
#include <Services/TransportService.h>
#include <VR/VRPlayerPose.h>
#include <World.h>
#include <vr_common/VRHandoffPath.h>

#include <fstream>
#include <string>
#include <vector>

namespace
{
constexpr double kProjectileStatusWriteInterval = 0.25;
constexpr double kRemoteProjectileEventStaleSeconds = 10.0;
constexpr char kProjectileStatusFileName[] = "SkyrimTogetherVR.projectile";

std::filesystem::path GetHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
}

GameId ToServerId(World& aWorld, uint32_t aFormId) noexcept
{
    GameId result{};
    if (aFormId)
        aWorld.GetModSystem().GetServerModId(aFormId, result);
    return result;
}

void WriteGameId(std::ofstream& aFile, const std::string& acPrefix, const GameId& acId)
{
    aFile << acPrefix << ".serverModId=" << acId.ModId << "\n";
    aFile << acPrefix << ".serverBaseId=" << acId.BaseId << "\n";
}

void SetPosition(Vector3_NetQuantize& aOut, bool& aValid, const VRPoseTransform& acTransform) noexcept
{
    aValid = acTransform.Valid;
    if (acTransform.Valid)
        aOut = glm::vec3(acTransform.Position.x, acTransform.Position.y, acTransform.Position.z);
}

const char* GetProjectileKindName(VRProjectileEvent::Kind aKind) noexcept
{
    switch (aKind)
    {
    case VRProjectileEvent::Kind::kBowShot: return "bowShot";
    case VRProjectileEvent::Kind::kSpellCast: return "spellCast";
    default: return "unknown";
    }
}

void WriteProjectileEvent(std::ofstream& aFile, const std::string& acPrefix, const VRProjectileEvent& acProjectile)
{
    aFile << acPrefix << ".sequence=" << acProjectile.Sequence << "\n";
    aFile << acPrefix << ".kind=" << GetProjectileKindName(acProjectile.EventKind) << "\n";
    WriteGameId(aFile, acPrefix + ".source", acProjectile.SourceId);
    WriteGameId(aFile, acPrefix + ".weapon", acProjectile.WeaponId);
    WriteGameId(aFile, acPrefix + ".ammo", acProjectile.AmmoId);
    WriteGameId(aFile, acPrefix + ".spell", acProjectile.SpellId);
    aFile << acPrefix << ".originValid=" << (acProjectile.OriginValid ? "1" : "0") << "\n";
    aFile << acPrefix << ".destinationValid=" << (acProjectile.DestinationValid ? "1" : "0") << "\n";
    aFile << acPrefix << ".origin=" << acProjectile.Origin.x << "," << acProjectile.Origin.y << "," << acProjectile.Origin.z << "\n";
    aFile << acPrefix << ".destination=" << acProjectile.Destination.x << "," << acProjectile.Destination.y << "," << acProjectile.Destination.z << "\n";
    aFile << acPrefix << ".power=" << acProjectile.Power << "\n";
    aFile << acPrefix << ".isSunGazing=" << (acProjectile.IsSunGazing ? "1" : "0") << "\n";
}
} // namespace

VRProjectileService::VRProjectileService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_projectileStatusPath(m_handoffDir / kProjectileStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR projectile handoff status file: {}", m_projectileStatusPath.string());

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRProjectileService::OnUpdate>(this);
    m_vrProjectileEventConnection = aDispatcher.sink<NotifyVRProjectileEvent>().connect<&VRProjectileService::OnVRProjectileEvent>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&VRProjectileService::OnPlayerLeft>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRProjectileService::OnDisconnected>(this);

    auto* pEventDispatcherManager = EventDispatcherManager::Get();
    if (pEventDispatcherManager)
    {
        pEventDispatcherManager->GetPlayerBowShotEventData().RegisterSink(this);
        pEventDispatcherManager->GetSpellCastEventData().RegisterSink(this);
    }
    else
    {
        spdlog::warn("VRProjectileService could not register projectile event sinks; dispatcher manager is unavailable");
    }
}

BSTEventResult VRProjectileService::OnEvent(const TESPlayerBowShotEvent* apEvent, const EventDispatcher<TESPlayerBowShotEvent>* apSender)
{
    TP_UNUSED(apSender);

    if (!apEvent)
        return BSTEventResult::kOk;

    VRProjectileEvent projectile{};
    if (!CaptureBowShot(*apEvent, projectile))
        return BSTEventResult::kOk;

    m_lastLocalProjectileEvent = projectile;
    m_hasLocalProjectileEvent = true;
    m_statusDirty = true;

    SendProjectileEvent(projectile);
    return BSTEventResult::kOk;
}

BSTEventResult VRProjectileService::OnEvent(const TESSpellCastEvent* apEvent, const EventDispatcher<TESSpellCastEvent>* apSender)
{
    TP_UNUSED(apSender);

    if (!apEvent)
        return BSTEventResult::kOk;

    VRProjectileEvent projectile{};
    if (!CaptureSpellCast(*apEvent, projectile))
        return BSTEventResult::kOk;

    m_lastLocalProjectileEvent = projectile;
    m_hasLocalProjectileEvent = true;
    m_statusDirty = true;

    SendProjectileEvent(projectile);
    return BSTEventResult::kOk;
}

void VRProjectileService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PruneRemoteProjectileEvents(acEvent.Delta);

    m_statusTimer += acEvent.Delta;
    if (!m_statusDirty && m_statusTimer < kProjectileStatusWriteInterval)
        return;

    m_statusTimer = 0.0;
    WriteProjectileStatusFile();
}

void VRProjectileService::OnVRProjectileEvent(const NotifyVRProjectileEvent& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;

    m_remoteProjectileEvents[acMessage.PlayerId] = acMessage.Projectile;
    m_remoteProjectileEventAges[acMessage.PlayerId] = 0.0;
    m_statusDirty = true;
}

void VRProjectileService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    const auto eventCount = m_remoteProjectileEvents.erase(acMessage.PlayerId);
    const auto ageCount = m_remoteProjectileEventAges.erase(acMessage.PlayerId);
    if (eventCount || ageCount)
        m_statusDirty = true;
}

void VRProjectileService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    if (!m_remoteProjectileEvents.empty() || !m_remoteProjectileEventAges.empty())
    {
        m_remoteProjectileEvents.clear();
        m_remoteProjectileEventAges.clear();
        m_statusDirty = true;
    }
}

void VRProjectileService::PruneRemoteProjectileEvents(double aDelta) noexcept
{
    if (!m_transport.IsOnline())
    {
        if (!m_remoteProjectileEvents.empty() || !m_remoteProjectileEventAges.empty())
        {
            m_remoteProjectileEvents.clear();
            m_remoteProjectileEventAges.clear();
            m_statusDirty = true;
        }
        return;
    }

    std::vector<uint32_t> trackedPlayerIds;
    std::vector<uint32_t> expiredPlayerIds;
    for (const auto& [playerId, age] : m_remoteProjectileEventAges)
    {
        trackedPlayerIds.push_back(playerId);
        if (age + aDelta >= kRemoteProjectileEventStaleSeconds)
            expiredPlayerIds.push_back(playerId);
    }
    for (auto playerId : trackedPlayerIds)
        m_remoteProjectileEventAges[playerId] += aDelta;

    for (auto playerId : expiredPlayerIds)
    {
        m_remoteProjectileEventAges.erase(playerId);
        m_remoteProjectileEvents.erase(playerId);
        m_statusDirty = true;
    }
}

bool VRProjectileService::CaptureBowShot(const TESPlayerBowShotEvent& acEvent, VRProjectileEvent& aProjectile) noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;

    VRPlayerPoseSnapshot pose{};
    SkyrimVR::CaptureLocalPlayerPoseSnapshot(pose);

    aProjectile.Sequence = ++m_sequence;
    aProjectile.EventKind = VRProjectileEvent::Kind::kBowShot;
    aProjectile.SourceId = ToServerId(m_world, pPlayer->GetFormIdData());
    aProjectile.WeaponId = ToServerId(m_world, acEvent.GetWeaponData());
    aProjectile.AmmoId = ToServerId(m_world, acEvent.GetAmmoData());
    aProjectile.Power = acEvent.GetShotPowerData();
    aProjectile.IsSunGazing = acEvent.IsSunGazingData();
    SetPosition(aProjectile.Origin, aProjectile.OriginValid, pose.ArrowOrigin);
    SetPosition(aProjectile.Destination, aProjectile.DestinationValid, pose.ArrowDestination);
    return true;
}

bool VRProjectileService::CaptureSpellCast(const TESSpellCastEvent& acEvent, VRProjectileEvent& aProjectile) noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;

    const auto* pPlayerReference = static_cast<const TESObjectREFR*>(pPlayer);
    if (acEvent.GetObjectData() != pPlayerReference)
        return false;

    VRPlayerPoseSnapshot pose{};
    SkyrimVR::CaptureLocalPlayerPoseSnapshot(pose);

    aProjectile.Sequence = ++m_sequence;
    aProjectile.EventKind = VRProjectileEvent::Kind::kSpellCast;
    aProjectile.SourceId = ToServerId(m_world, pPlayer->GetFormIdData());
    aProjectile.SpellId = ToServerId(m_world, acEvent.GetSpellData());
    SetPosition(aProjectile.Origin, aProjectile.OriginValid, pose.SpellOrigin);
    SetPosition(aProjectile.Destination, aProjectile.DestinationValid, pose.SpellDestination);
    return true;
}

void VRProjectileService::SendProjectileEvent(const VRProjectileEvent& acProjectile) noexcept
{
    if (!m_transport.IsOnline())
        return;

    RequestVRProjectileEvent request{};
    request.Projectile = acProjectile;
    m_transport.Send(request);
}

void VRProjectileService::WriteProjectileStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_projectileStatusPath, std::ios::trunc);
    if (!file)
        return;

    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "localProjectileEventAvailable=" << (m_hasLocalProjectileEvent ? "1" : "0") << "\n";
    file << "remoteProjectileEventCount=" << m_remoteProjectileEvents.size() << "\n";

    if (m_hasLocalProjectileEvent)
        WriteProjectileEvent(file, "localProjectileEvent", m_lastLocalProjectileEvent);

    for (const auto& [playerId, projectile] : m_remoteProjectileEvents)
    {
        const auto ageIt = m_remoteProjectileEventAges.find(playerId);
        const auto age = ageIt != m_remoteProjectileEventAges.end() ? ageIt->second : 0.0;
        const auto prefix = std::string("remoteProjectileEvent.") + std::to_string(playerId);

        file << prefix << ".ageSeconds=" << age << "\n";
        WriteProjectileEvent(file, prefix, projectile);
    }

    m_statusDirty = false;
}
