#include <TiltedOnlinePCH.h>

#include <Services/VRActivationService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Forms/TESForm.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyVRActivationEvent.h>
#include <Messages/RequestVRActivationEvent.h>
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
constexpr double kActivationStatusWriteInterval = 0.25;
constexpr double kRemoteActivationStaleSeconds = 10.0;
constexpr char kActivationStatusFileName[] = "SkyrimTogetherVR.activation";

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

void WriteActivationEvent(std::ofstream& aFile, const std::string& acPrefix, const VRActivationEvent& acActivation)
{
    aFile << acPrefix << ".sequence=" << acActivation.Sequence << "\n";
    WriteGameId(aFile, acPrefix + ".object", acActivation.ObjectId);
    WriteGameId(aFile, acPrefix + ".cell", acActivation.CellId);
    WriteGameId(aFile, acPrefix + ".worldSpace", acActivation.WorldSpaceId);
    aFile << acPrefix << ".position=" << acActivation.Position.x << "," << acActivation.Position.y << "," << acActivation.Position.z << "\n";
    aFile << acPrefix << ".formType=" << static_cast<uint32_t>(acActivation.FormType) << "\n";
    aFile << acPrefix << ".openState=" << static_cast<uint32_t>(acActivation.OpenState) << "\n";
}
} // namespace

VRActivationService::VRActivationService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_activationStatusPath(m_handoffDir / kActivationStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR activation handoff status file: {}", m_activationStatusPath.string());

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRActivationService::OnUpdate>(this);
    m_vrActivationEventConnection = aDispatcher.sink<NotifyVRActivationEvent>().connect<&VRActivationService::OnVRActivationEvent>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&VRActivationService::OnPlayerLeft>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRActivationService::OnDisconnected>(this);

    auto* pEventDispatcherManager = EventDispatcherManager::Get();
    if (pEventDispatcherManager)
        pEventDispatcherManager->GetActivateEventData().RegisterSink(this);
    else
        spdlog::warn("VRActivationService could not register TESActivateEvent sink; dispatcher manager is unavailable");
}

BSTEventResult VRActivationService::OnEvent(const TESActivateEvent* apEvent, const EventDispatcher<TESActivateEvent>* apSender)
{
    TP_UNUSED(apSender);

    if (!apEvent)
        return BSTEventResult::kOk;

    VRActivationEvent activation{};
    if (!CaptureActivation(*apEvent, activation))
        return BSTEventResult::kOk;

    m_lastLocalActivation = activation;
    m_hasLocalActivation = true;
    m_statusDirty = true;

    SendActivationEvent(activation);
    return BSTEventResult::kOk;
}

void VRActivationService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PruneRemoteActivations(acEvent.Delta);

    m_statusTimer += acEvent.Delta;
    if (!m_statusDirty && m_statusTimer < kActivationStatusWriteInterval)
        return;

    m_statusTimer = 0.0;
    WriteActivationStatusFile();
}

void VRActivationService::OnVRActivationEvent(const NotifyVRActivationEvent& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;

    m_remoteActivations[acMessage.PlayerId] = acMessage.Activation;
    m_remoteActivationAges[acMessage.PlayerId] = 0.0;
    m_statusDirty = true;
}

void VRActivationService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    const auto activationCount = m_remoteActivations.erase(acMessage.PlayerId);
    const auto ageCount = m_remoteActivationAges.erase(acMessage.PlayerId);
    if (activationCount || ageCount)
        m_statusDirty = true;
}

void VRActivationService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    if (!m_remoteActivations.empty() || !m_remoteActivationAges.empty())
    {
        m_remoteActivations.clear();
        m_remoteActivationAges.clear();
        m_statusDirty = true;
    }
}

void VRActivationService::PruneRemoteActivations(double aDelta) noexcept
{
    if (!m_transport.IsOnline())
    {
        if (!m_remoteActivations.empty() || !m_remoteActivationAges.empty())
        {
            m_remoteActivations.clear();
            m_remoteActivationAges.clear();
            m_statusDirty = true;
        }
        return;
    }

    std::vector<uint32_t> trackedPlayerIds;
    std::vector<uint32_t> expiredPlayerIds;
    for (const auto& [playerId, age] : m_remoteActivationAges)
    {
        trackedPlayerIds.push_back(playerId);
        if (age + aDelta >= kRemoteActivationStaleSeconds)
            expiredPlayerIds.push_back(playerId);
    }
    for (auto playerId : trackedPlayerIds)
        m_remoteActivationAges[playerId] += aDelta;

    for (auto playerId : expiredPlayerIds)
    {
        m_remoteActivationAges.erase(playerId);
        m_remoteActivations.erase(playerId);
        m_statusDirty = true;
    }
}

bool VRActivationService::CaptureActivation(const TESActivateEvent& acEvent, VRActivationEvent& aActivation) noexcept
{
    const auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || acEvent.GetActionRefData() != pPlayer)
        return false;

    auto* pObject = acEvent.GetObjectActivatedData();
    auto* pBaseForm = pObject ? pObject->GetBaseFormData() : nullptr;
    if (!pObject || !pBaseForm)
        return false;

    const auto* pCell = pObject->GetParentCellEx();
    const auto* pWorldSpace = pCell ? pCell->GetWorldSpaceData() : nullptr;
    if (!pWorldSpace)
        pWorldSpace = pObject->GetWorldSpace();

    aActivation.Sequence = ++m_sequence;
    aActivation.ObjectId = ToServerId(m_world, pObject->GetFormIdData());
    aActivation.CellId = ToServerId(m_world, pCell ? pCell->GetFormIdData() : 0);
    aActivation.WorldSpaceId = ToServerId(m_world, pWorldSpace ? pWorldSpace->GetFormIdData() : 0);
    const auto& position = pObject->GetPositionData();
    aActivation.Position = glm::vec3(position.x, position.y, position.z);
    aActivation.FormType = static_cast<uint8_t>(pBaseForm->GetFormTypeData());
    aActivation.OpenState = static_cast<uint8_t>(pObject->GetOpenState());
    return true;
}

void VRActivationService::SendActivationEvent(const VRActivationEvent& acActivation) noexcept
{
    if (!m_transport.IsOnline())
        return;

    RequestVRActivationEvent request{};
    request.Activation = acActivation;
    m_transport.Send(request);
}

void VRActivationService::WriteActivationStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_activationStatusPath, std::ios::trunc);
    if (!file)
        return;

    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "localActivationAvailable=" << (m_hasLocalActivation ? "1" : "0") << "\n";
    file << "remoteActivationCount=" << m_remoteActivations.size() << "\n";

    if (m_hasLocalActivation)
        WriteActivationEvent(file, "localActivation", m_lastLocalActivation);

    for (const auto& [playerId, activation] : m_remoteActivations)
    {
        const auto ageIt = m_remoteActivationAges.find(playerId);
        const auto age = ageIt != m_remoteActivationAges.end() ? ageIt->second : 0.0;
        const auto prefix = std::string("remoteActivation.") + std::to_string(playerId);

        file << prefix << ".ageSeconds=" << age << "\n";
        WriteActivationEvent(file, prefix, activation);
    }

    m_statusDirty = false;
}
