#include <TiltedOnlinePCH.h>

#include <Services/VRCombatService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Forms/TESForm.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyVRCombatHitEvent.h>
#include <Messages/RequestVRCombatHitEvent.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>
#include <Services/TransportService.h>
#include <World.h>
#include <vr_common/VRHandoffPath.h>

#include <fstream>
#include <string>
#include <vector>

namespace
{
constexpr double kCombatStatusWriteInterval = 0.25;
constexpr double kRemoteCombatHitStaleSeconds = 10.0;
constexpr char kCombatStatusFileName[] = "SkyrimTogetherVR.combat";
constexpr uint32_t kPlanckHitEventMagicNumber = 0x59914000;
constexpr uint32_t kPlanckHitEventMask = 0xFFFFFF00;

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

uint8_t GetFormType(const TESObjectREFR* apReference) noexcept
{
    const auto* pBaseForm = apReference ? apReference->GetBaseFormData() : nullptr;
    return pBaseForm ? static_cast<uint8_t>(pBaseForm->GetFormTypeData()) : 0;
}

Vector3_NetQuantize GetPosition(const TESObjectREFR* apReference) noexcept
{
    Vector3_NetQuantize result{};
    if (apReference)
    {
        const auto& position = apReference->GetPositionData();
        result = glm::vec3(position.x, position.y, position.z);
    }
    return result;
}

void WriteGameId(std::ofstream& aFile, const std::string& acPrefix, const GameId& acId)
{
    aFile << acPrefix << ".serverModId=" << acId.ModId << "\n";
    aFile << acPrefix << ".serverBaseId=" << acId.BaseId << "\n";
}

bool IsPlanckHit(uint32_t aRawHitFlags) noexcept
{
    return (aRawHitFlags & kPlanckHitEventMask) == kPlanckHitEventMagicNumber;
}

void WriteCombatHitEvent(std::ofstream& aFile, const std::string& acPrefix, const VRCombatHitEvent& acHit)
{
    aFile << acPrefix << ".sequence=" << acHit.Sequence << "\n";
    WriteGameId(aFile, acPrefix + ".hitter", acHit.HitterId);
    WriteGameId(aFile, acPrefix + ".hittee", acHit.HitteeId);
    WriteGameId(aFile, acPrefix + ".source", acHit.SourceId);
    WriteGameId(aFile, acPrefix + ".projectile", acHit.ProjectileId);
    aFile << acPrefix << ".hitterPosition=" << acHit.HitterPosition.x << "," << acHit.HitterPosition.y << "," << acHit.HitterPosition.z << "\n";
    aFile << acPrefix << ".hitteePosition=" << acHit.HitteePosition.x << "," << acHit.HitteePosition.y << "," << acHit.HitteePosition.z << "\n";
    aFile << acPrefix << ".rawHitFlags=" << acHit.RawHitFlags << "\n";
    aFile << acPrefix << ".planckHit=" << (acHit.PlanckHit ? "1" : "0") << "\n";
    aFile << acPrefix << ".hitterFormType=" << static_cast<uint32_t>(acHit.HitterFormType) << "\n";
    aFile << acPrefix << ".hitteeFormType=" << static_cast<uint32_t>(acHit.HitteeFormType) << "\n";
    aFile << acPrefix << ".hitterIsPlayer=" << (acHit.HitterIsPlayer ? "1" : "0") << "\n";
    aFile << acPrefix << ".hitteeIsPlayer=" << (acHit.HitteeIsPlayer ? "1" : "0") << "\n";
}
} // namespace

VRCombatService::VRCombatService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_combatStatusPath(m_handoffDir / kCombatStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR combat handoff status file: {}", m_combatStatusPath.string());

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRCombatService::OnUpdate>(this);
    m_vrCombatHitEventConnection = aDispatcher.sink<NotifyVRCombatHitEvent>().connect<&VRCombatService::OnVRCombatHitEvent>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&VRCombatService::OnPlayerLeft>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRCombatService::OnDisconnected>(this);

    auto* pEventDispatcherManager = EventDispatcherManager::Get();
    if (pEventDispatcherManager)
        pEventDispatcherManager->GetHitEventData().RegisterSink(this);
    else
        spdlog::warn("VRCombatService could not register TESHitEvent sink; dispatcher manager is unavailable");
}

BSTEventResult VRCombatService::OnEvent(const TESHitEvent* apEvent, const EventDispatcher<TESHitEvent>* apSender)
{
    TP_UNUSED(apSender);

    if (!apEvent)
        return BSTEventResult::kOk;

    VRCombatHitEvent hit{};
    if (!CaptureCombatHit(*apEvent, hit))
        return BSTEventResult::kOk;

    m_lastLocalCombatHit = hit;
    m_hasLocalCombatHit = true;
    m_statusDirty = true;

    SendCombatHitEvent(hit);
    return BSTEventResult::kOk;
}

void VRCombatService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PruneRemoteCombatHits(acEvent.Delta);

    m_statusTimer += acEvent.Delta;
    if (!m_statusDirty && m_statusTimer < kCombatStatusWriteInterval)
        return;

    m_statusTimer = 0.0;
    WriteCombatStatusFile();
}

void VRCombatService::OnVRCombatHitEvent(const NotifyVRCombatHitEvent& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;

    m_remoteCombatHits[acMessage.PlayerId] = acMessage.Hit;
    m_remoteCombatHitAges[acMessage.PlayerId] = 0.0;
    m_statusDirty = true;
}

void VRCombatService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    const auto hitCount = m_remoteCombatHits.erase(acMessage.PlayerId);
    const auto ageCount = m_remoteCombatHitAges.erase(acMessage.PlayerId);
    if (hitCount || ageCount)
        m_statusDirty = true;
}

void VRCombatService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    if (!m_remoteCombatHits.empty() || !m_remoteCombatHitAges.empty())
    {
        m_remoteCombatHits.clear();
        m_remoteCombatHitAges.clear();
        m_statusDirty = true;
    }
}

void VRCombatService::PruneRemoteCombatHits(double aDelta) noexcept
{
    if (!m_transport.IsOnline())
    {
        if (!m_remoteCombatHits.empty() || !m_remoteCombatHitAges.empty())
        {
            m_remoteCombatHits.clear();
            m_remoteCombatHitAges.clear();
            m_statusDirty = true;
        }
        return;
    }

    std::vector<uint32_t> trackedPlayerIds;
    std::vector<uint32_t> expiredPlayerIds;
    for (const auto& [playerId, age] : m_remoteCombatHitAges)
    {
        trackedPlayerIds.push_back(playerId);
        if (age + aDelta >= kRemoteCombatHitStaleSeconds)
            expiredPlayerIds.push_back(playerId);
    }
    for (auto playerId : trackedPlayerIds)
        m_remoteCombatHitAges[playerId] += aDelta;

    for (auto playerId : expiredPlayerIds)
    {
        m_remoteCombatHitAges.erase(playerId);
        m_remoteCombatHits.erase(playerId);
        m_statusDirty = true;
    }
}

bool VRCombatService::CaptureCombatHit(const TESHitEvent& acEvent, VRCombatHitEvent& aHit) noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;

    const auto* pPlayerReference = static_cast<const TESObjectREFR*>(pPlayer);
    auto* pHitter = acEvent.GetCauseData();
    auto* pHittee = acEvent.GetTargetData();
    const bool hitterIsPlayer = pHitter == pPlayerReference;
    const bool hitteeIsPlayer = pHittee == pPlayerReference;
    if (!hitterIsPlayer && !hitteeIsPlayer)
        return false;

    aHit.Sequence = ++m_sequence;
    aHit.HitterId = ToServerId(m_world, pHitter ? pHitter->GetFormIdData() : 0);
    aHit.HitteeId = ToServerId(m_world, pHittee ? pHittee->GetFormIdData() : 0);
    aHit.SourceId = ToServerId(m_world, acEvent.GetSourceData());
    aHit.ProjectileId = ToServerId(m_world, acEvent.GetProjectileData());
    aHit.HitterPosition = GetPosition(pHitter);
    aHit.HitteePosition = GetPosition(pHittee);
    aHit.RawHitFlags = acEvent.GetRawFlagsData();
    aHit.PlanckHit = IsPlanckHit(aHit.RawHitFlags);
    aHit.HitterFormType = GetFormType(pHitter);
    aHit.HitteeFormType = GetFormType(pHittee);
    aHit.HitterIsPlayer = hitterIsPlayer;
    aHit.HitteeIsPlayer = hitteeIsPlayer;
    return true;
}

void VRCombatService::SendCombatHitEvent(const VRCombatHitEvent& acHit) noexcept
{
    if (!m_transport.IsOnline())
        return;

    RequestVRCombatHitEvent request{};
    request.Hit = acHit;
    m_transport.Send(request);
}

void VRCombatService::WriteCombatStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_combatStatusPath, std::ios::trunc);
    if (!file)
        return;

    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "localCombatHitAvailable=" << (m_hasLocalCombatHit ? "1" : "0") << "\n";
    file << "remoteCombatHitCount=" << m_remoteCombatHits.size() << "\n";

    if (m_hasLocalCombatHit)
        WriteCombatHitEvent(file, "localCombatHit", m_lastLocalCombatHit);

    for (const auto& [playerId, hit] : m_remoteCombatHits)
    {
        const auto ageIt = m_remoteCombatHitAges.find(playerId);
        const auto age = ageIt != m_remoteCombatHitAges.end() ? ageIt->second : 0.0;
        const auto prefix = std::string("remoteCombatHit.") + std::to_string(playerId);

        file << prefix << ".ageSeconds=" << age << "\n";
        WriteCombatHitEvent(file, prefix, hit);
    }

    m_statusDirty = false;
}
