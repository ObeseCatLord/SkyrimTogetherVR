#include <TiltedOnlinePCH.h>

#include <Services/VRGrabService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Forms/TESForm.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyVRGrabEvent.h>
#include <Messages/RequestVRGrabEvent.h>
#include <TESObjectREFR.h>
#include <Services/TransportService.h>
#include <World.h>
#include <vr_common/VRHandoffPath.h>

#include <fstream>
#include <string>
#include <vector>

namespace
{
constexpr double kGrabStatusWriteInterval = 0.25;
constexpr double kRemoteGrabStaleSeconds = 10.0;
constexpr char kGrabStatusFileName[] = "SkyrimTogetherVR.grab";

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

void WriteGrabEvent(std::ofstream& aFile, const std::string& acPrefix, const VRGrabEvent& acGrab)
{
    aFile << acPrefix << ".sequence=" << acGrab.Sequence << "\n";
    WriteGameId(aFile, acPrefix + ".object", acGrab.ObjectId);
    WriteGameId(aFile, acPrefix + ".cell", acGrab.CellId);
    WriteGameId(aFile, acPrefix + ".worldSpace", acGrab.WorldSpaceId);
    aFile << acPrefix << ".position=" << acGrab.Position.x << "," << acGrab.Position.y << "," << acGrab.Position.z << "\n";
    aFile << acPrefix << ".formType=" << static_cast<uint32_t>(acGrab.FormType) << "\n";
    aFile << acPrefix << ".grabbed=" << (acGrab.Grabbed ? "1" : "0") << "\n";
}
} // namespace

VRGrabService::VRGrabService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_grabStatusPath(m_handoffDir / kGrabStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR grab handoff status file: {}", m_grabStatusPath.string());

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRGrabService::OnUpdate>(this);
    m_vrGrabEventConnection = aDispatcher.sink<NotifyVRGrabEvent>().connect<&VRGrabService::OnVRGrabEvent>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&VRGrabService::OnPlayerLeft>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRGrabService::OnDisconnected>(this);

    auto* pEventDispatcherManager = EventDispatcherManager::Get();
    if (pEventDispatcherManager)
        pEventDispatcherManager->GetGrabReleaseEventData().RegisterSink(this);
    else
        spdlog::warn("VRGrabService could not register TESGrabReleaseEvent sink; dispatcher manager is unavailable");
}

BSTEventResult VRGrabService::OnEvent(const TESGrabReleaseEvent* apEvent, const EventDispatcher<TESGrabReleaseEvent>* apSender)
{
    TP_UNUSED(apSender);

    if (!apEvent)
        return BSTEventResult::kOk;

    VRGrabEvent grab{};
    if (!CaptureGrab(*apEvent, grab))
        return BSTEventResult::kOk;

    m_lastLocalGrab = grab;
    m_hasLocalGrab = true;
    m_statusDirty = true;

    SendGrabEvent(grab);
    return BSTEventResult::kOk;
}

void VRGrabService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PruneRemoteGrabs(acEvent.Delta);

    m_statusTimer += acEvent.Delta;
    if (!m_statusDirty && m_statusTimer < kGrabStatusWriteInterval)
        return;

    m_statusTimer = 0.0;
    WriteGrabStatusFile();
}

void VRGrabService::OnVRGrabEvent(const NotifyVRGrabEvent& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;

    m_remoteGrabs[acMessage.PlayerId] = acMessage.Grab;
    m_remoteGrabAges[acMessage.PlayerId] = 0.0;
    m_statusDirty = true;
}

void VRGrabService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    const auto grabCount = m_remoteGrabs.erase(acMessage.PlayerId);
    const auto ageCount = m_remoteGrabAges.erase(acMessage.PlayerId);
    if (grabCount || ageCount)
        m_statusDirty = true;
}

void VRGrabService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    if (!m_remoteGrabs.empty() || !m_remoteGrabAges.empty())
    {
        m_remoteGrabs.clear();
        m_remoteGrabAges.clear();
        m_statusDirty = true;
    }
}

void VRGrabService::PruneRemoteGrabs(double aDelta) noexcept
{
    if (!m_transport.IsOnline())
    {
        if (!m_remoteGrabs.empty() || !m_remoteGrabAges.empty())
        {
            m_remoteGrabs.clear();
            m_remoteGrabAges.clear();
            m_statusDirty = true;
        }
        return;
    }

    std::vector<uint32_t> trackedPlayerIds;
    std::vector<uint32_t> expiredPlayerIds;
    for (const auto& [playerId, age] : m_remoteGrabAges)
    {
        trackedPlayerIds.push_back(playerId);
        if (age + aDelta >= kRemoteGrabStaleSeconds)
            expiredPlayerIds.push_back(playerId);
    }
    for (auto playerId : trackedPlayerIds)
        m_remoteGrabAges[playerId] += aDelta;

    for (auto playerId : expiredPlayerIds)
    {
        m_remoteGrabAges.erase(playerId);
        m_remoteGrabs.erase(playerId);
        m_statusDirty = true;
    }
}

bool VRGrabService::CaptureGrab(const TESGrabReleaseEvent& acEvent, VRGrabEvent& aGrab) noexcept
{
    auto* pObject = acEvent.GetReferenceData();
    auto* pBaseForm = pObject ? pObject->GetBaseFormData() : nullptr;
    if (!pObject || !pBaseForm)
        return false;

    const auto* pCell = pObject->GetParentCellEx();
    const auto* pWorldSpace = pCell ? pCell->GetWorldSpaceData() : nullptr;
    if (!pWorldSpace)
        pWorldSpace = pObject->GetWorldSpace();

    aGrab.Sequence = ++m_sequence;
    aGrab.ObjectId = ToServerId(m_world, pObject->GetFormIdData());
    aGrab.CellId = ToServerId(m_world, pCell ? pCell->GetFormIdData() : 0);
    aGrab.WorldSpaceId = ToServerId(m_world, pWorldSpace ? pWorldSpace->GetFormIdData() : 0);
    const auto& position = pObject->GetPositionData();
    aGrab.Position = glm::vec3(position.x, position.y, position.z);
    aGrab.FormType = static_cast<uint8_t>(pBaseForm->GetFormTypeData());
    aGrab.Grabbed = acEvent.GetGrabbedData();
    return true;
}

void VRGrabService::SendGrabEvent(const VRGrabEvent& acGrab) noexcept
{
    if (!m_transport.IsOnline())
        return;

    RequestVRGrabEvent request{};
    request.Grab = acGrab;
    m_transport.Send(request);
}

void VRGrabService::WriteGrabStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_grabStatusPath, std::ios::trunc);
    if (!file)
        return;

    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "localGrabAvailable=" << (m_hasLocalGrab ? "1" : "0") << "\n";
    file << "remoteGrabCount=" << m_remoteGrabs.size() << "\n";

    if (m_hasLocalGrab)
        WriteGrabEvent(file, "localGrab", m_lastLocalGrab);

    for (const auto& [playerId, grab] : m_remoteGrabs)
    {
        const auto ageIt = m_remoteGrabAges.find(playerId);
        const auto age = ageIt != m_remoteGrabAges.end() ? ageIt->second : 0.0;
        const auto prefix = std::string("remoteGrab.") + std::to_string(playerId);

        file << prefix << ".ageSeconds=" << age << "\n";
        WriteGrabEvent(file, prefix, grab);
    }

    m_statusDirty = false;
}
