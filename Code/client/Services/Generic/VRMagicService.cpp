#include <TiltedOnlinePCH.h>

#include <Services/VRMagicService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Forms/TESForm.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyVRMagicEffectEvent.h>
#include <Messages/RequestVRMagicEffectEvent.h>
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
constexpr double kMagicStatusWriteInterval = 0.25;
constexpr double kRemoteMagicEffectStaleSeconds = 10.0;
constexpr char kMagicStatusFileName[] = "SkyrimTogetherVR.magic";

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

void WriteMagicEffectEvent(std::ofstream& aFile, const std::string& acPrefix, const VRMagicEffectEvent& acMagicEffect)
{
    aFile << acPrefix << ".sequence=" << acMagicEffect.Sequence << "\n";
    WriteGameId(aFile, acPrefix + ".effect", acMagicEffect.EffectId);
    WriteGameId(aFile, acPrefix + ".caster", acMagicEffect.CasterId);
    WriteGameId(aFile, acPrefix + ".target", acMagicEffect.TargetId);
    aFile << acPrefix << ".casterPosition=" << acMagicEffect.CasterPosition.x << "," << acMagicEffect.CasterPosition.y << "," << acMagicEffect.CasterPosition.z << "\n";
    aFile << acPrefix << ".targetPosition=" << acMagicEffect.TargetPosition.x << "," << acMagicEffect.TargetPosition.y << "," << acMagicEffect.TargetPosition.z << "\n";
    aFile << acPrefix << ".casterFormType=" << static_cast<uint32_t>(acMagicEffect.CasterFormType) << "\n";
    aFile << acPrefix << ".targetFormType=" << static_cast<uint32_t>(acMagicEffect.TargetFormType) << "\n";
    aFile << acPrefix << ".casterIsPlayer=" << (acMagicEffect.CasterIsPlayer ? "1" : "0") << "\n";
    aFile << acPrefix << ".targetIsPlayer=" << (acMagicEffect.TargetIsPlayer ? "1" : "0") << "\n";
}
} // namespace

VRMagicService::VRMagicService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_magicStatusPath(m_handoffDir / kMagicStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR magic handoff status file: {}", m_magicStatusPath.string());

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRMagicService::OnUpdate>(this);
    m_vrMagicEffectEventConnection = aDispatcher.sink<NotifyVRMagicEffectEvent>().connect<&VRMagicService::OnVRMagicEffectEvent>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&VRMagicService::OnPlayerLeft>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRMagicService::OnDisconnected>(this);

    auto* pEventDispatcherManager = EventDispatcherManager::Get();
    if (pEventDispatcherManager)
        pEventDispatcherManager->GetMagicEffectApplyEventData().RegisterSink(this);
    else
        spdlog::warn("VRMagicService could not register TESMagicEffectApplyEvent sink; dispatcher manager is unavailable");
}

BSTEventResult VRMagicService::OnEvent(const TESMagicEffectApplyEvent* apEvent, const EventDispatcher<TESMagicEffectApplyEvent>* apSender)
{
    TP_UNUSED(apSender);

    if (!apEvent)
        return BSTEventResult::kOk;

    VRMagicEffectEvent magicEffect{};
    if (!CaptureMagicEffect(*apEvent, magicEffect))
        return BSTEventResult::kOk;

    m_lastLocalMagicEffect = magicEffect;
    m_hasLocalMagicEffect = true;
    m_statusDirty = true;

    SendMagicEffectEvent(magicEffect);
    return BSTEventResult::kOk;
}

void VRMagicService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PruneRemoteMagicEffects(acEvent.Delta);

    m_statusTimer += acEvent.Delta;
    if (!m_statusDirty && m_statusTimer < kMagicStatusWriteInterval)
        return;

    m_statusTimer = 0.0;
    WriteMagicStatusFile();
}

void VRMagicService::OnVRMagicEffectEvent(const NotifyVRMagicEffectEvent& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;

    m_remoteMagicEffects[acMessage.PlayerId] = acMessage.MagicEffect;
    m_remoteMagicEffectAges[acMessage.PlayerId] = 0.0;
    m_statusDirty = true;
}

void VRMagicService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    const auto magicCount = m_remoteMagicEffects.erase(acMessage.PlayerId);
    const auto ageCount = m_remoteMagicEffectAges.erase(acMessage.PlayerId);
    if (magicCount || ageCount)
        m_statusDirty = true;
}

void VRMagicService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    if (!m_remoteMagicEffects.empty() || !m_remoteMagicEffectAges.empty())
    {
        m_remoteMagicEffects.clear();
        m_remoteMagicEffectAges.clear();
        m_statusDirty = true;
    }
}

void VRMagicService::PruneRemoteMagicEffects(double aDelta) noexcept
{
    if (!m_transport.IsOnline())
    {
        if (!m_remoteMagicEffects.empty() || !m_remoteMagicEffectAges.empty())
        {
            m_remoteMagicEffects.clear();
            m_remoteMagicEffectAges.clear();
            m_statusDirty = true;
        }
        return;
    }

    std::vector<uint32_t> trackedPlayerIds;
    std::vector<uint32_t> expiredPlayerIds;
    for (const auto& [playerId, age] : m_remoteMagicEffectAges)
    {
        trackedPlayerIds.push_back(playerId);
        if (age + aDelta >= kRemoteMagicEffectStaleSeconds)
            expiredPlayerIds.push_back(playerId);
    }
    for (auto playerId : trackedPlayerIds)
        m_remoteMagicEffectAges[playerId] += aDelta;

    for (auto playerId : expiredPlayerIds)
    {
        m_remoteMagicEffectAges.erase(playerId);
        m_remoteMagicEffects.erase(playerId);
        m_statusDirty = true;
    }
}

bool VRMagicService::CaptureMagicEffect(const TESMagicEffectApplyEvent& acEvent, VRMagicEffectEvent& aMagicEffect) noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;

    auto* pCaster = acEvent.GetCasterData();
    auto* pTarget = acEvent.GetTargetData();
    const bool casterIsPlayer = pCaster == pPlayer;
    const bool targetIsPlayer = pTarget == pPlayer;
    if (!casterIsPlayer && !targetIsPlayer)
        return false;

    aMagicEffect.Sequence = ++m_sequence;
    aMagicEffect.EffectId = ToServerId(m_world, acEvent.GetMagicEffectData());
    aMagicEffect.CasterId = ToServerId(m_world, pCaster ? pCaster->GetFormIdData() : 0);
    aMagicEffect.TargetId = ToServerId(m_world, pTarget ? pTarget->GetFormIdData() : 0);
    aMagicEffect.CasterPosition = GetPosition(pCaster);
    aMagicEffect.TargetPosition = GetPosition(pTarget);
    aMagicEffect.CasterFormType = GetFormType(pCaster);
    aMagicEffect.TargetFormType = GetFormType(pTarget);
    aMagicEffect.CasterIsPlayer = casterIsPlayer;
    aMagicEffect.TargetIsPlayer = targetIsPlayer;
    return true;
}

void VRMagicService::SendMagicEffectEvent(const VRMagicEffectEvent& acMagicEffect) noexcept
{
    if (!m_transport.IsOnline())
        return;

    RequestVRMagicEffectEvent request{};
    request.MagicEffect = acMagicEffect;
    m_transport.Send(request);
}

void VRMagicService::WriteMagicStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_magicStatusPath, std::ios::trunc);
    if (!file)
        return;

    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "localMagicEffectAvailable=" << (m_hasLocalMagicEffect ? "1" : "0") << "\n";
    file << "remoteMagicEffectCount=" << m_remoteMagicEffects.size() << "\n";

    if (m_hasLocalMagicEffect)
        WriteMagicEffectEvent(file, "localMagicEffect", m_lastLocalMagicEffect);

    for (const auto& [playerId, magicEffect] : m_remoteMagicEffects)
    {
        const auto ageIt = m_remoteMagicEffectAges.find(playerId);
        const auto age = ageIt != m_remoteMagicEffectAges.end() ? ageIt->second : 0.0;
        const auto prefix = std::string("remoteMagicEffect.") + std::to_string(playerId);

        file << prefix << ".ageSeconds=" << age << "\n";
        WriteMagicEffectEvent(file, prefix, magicEffect);
    }

    m_statusDirty = false;
}
