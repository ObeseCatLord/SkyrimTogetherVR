#include <TiltedOnlinePCH.h>

#include <Services/VRSaveLoadService.h>

#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Forms/TESForm.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <PlayerCharacter.h>
#include <VR/VRPlayerReadiness.h>
#include <Services/TransportService.h>
#include <Structs/GameId.h>
#include <World.h>
#include <VRRuntimeDiagnostics.h>
#include <vr_common/VRHandoffPath.h>

#include <fstream>
#include <string>

namespace
{
constexpr double kSaveLoadStatusWriteInterval = 0.25;
constexpr char kSaveLoadStatusFileName[] = "SkyrimTogetherVR.saveload";

uint32_t GetFormId(const TESForm* apForm) noexcept
{
    return apForm ? apForm->GetFormIdData() : 0;
}

GameId ToServerId(World& aWorld, uint32_t aFormId) noexcept
{
    GameId result{};
    if (aFormId)
        aWorld.GetModSystem().GetServerModId(aFormId, result);
    return result;
}

void WriteSaveLoadGameIds(
    std::ofstream& aFile,
    const GameId& acPlayerId,
    const GameId& acPlayerCellId,
    const GameId& acPlayerWorldSpaceId)
{
    aFile << "player.serverModId=" << acPlayerId.ModId << "\n";
    aFile << "player.serverBaseId=" << acPlayerId.BaseId << "\n";
    aFile << "playerCell.serverModId=" << acPlayerCellId.ModId << "\n";
    aFile << "playerCell.serverBaseId=" << acPlayerCellId.BaseId << "\n";
    aFile << "playerWorldSpace.serverModId=" << acPlayerWorldSpaceId.ModId << "\n";
    aFile << "playerWorldSpace.serverBaseId=" << acPlayerWorldSpaceId.BaseId << "\n";
}

std::filesystem::path GetHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
}

bool IsVrPlayerReadyForSaveLoad(const PlayerCharacter* apPlayer) noexcept
{
    return apPlayer && apPlayer->GetBaseFormData() && apPlayer->GetParentCellData();
}
} // namespace

VRSaveLoadService::VRSaveLoadService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_saveLoadStatusPath(m_handoffDir / kSaveLoadStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR save/load handoff status file: {}", m_saveLoadStatusPath.string());

    m_playerReady = IsVrPlayerReadyForSaveLoad(SkyrimTogetherVR::TryGetReadablePlayerForVR());
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRSaveLoadService::OnUpdate>(this);
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&VRSaveLoadService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRSaveLoadService::OnDisconnected>(this);
    m_connectionErrorConnection = aDispatcher.sink<ConnectionErrorEvent>().connect<&VRSaveLoadService::OnConnectionError>(this);

    auto* pEventDispatcherManager = EventDispatcherManager::Get();
    if (pEventDispatcherManager)
        pEventDispatcherManager->GetLoadGameEventData().RegisterSink(this);
    else
        spdlog::warn("VRSaveLoadService could not register TESLoadGameEvent sink; dispatcher manager is unavailable");
}

BSTEventResult VRSaveLoadService::OnEvent(const TESLoadGameEvent* apEvent, const EventDispatcher<TESLoadGameEvent>* apSender)
{
    TP_UNUSED(apEvent);
    TP_UNUSED(apSender);

    ++m_loadGameCount;
    m_hasLoadGameEvent = true;
    m_secondsSinceLastLoad = 0.0;
    m_playerReady = IsVrPlayerReadyForSaveLoad(SkyrimTogetherVR::TryGetReadablePlayerForVR());
    m_readyAfterLastLoad = m_playerReady;
    m_waitingForReadyAfterLoad = !m_playerReady;
    m_statusDirty = true;

    spdlog::info("SkyrimTogetherVR observed TESLoadGameEvent count={}, playerReady={}", m_loadGameCount, m_playerReady);
    WriteSaveLoadStatusFile();
    return BSTEventResult::kOk;
}

void VRSaveLoadService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (m_hasLoadGameEvent)
        m_secondsSinceLastLoad += acEvent.Delta;

    const bool playerReady = IsVrPlayerReadyForSaveLoad(SkyrimTogetherVR::TryGetReadablePlayerForVR());
    if (playerReady != m_playerReady)
    {
        m_playerReady = playerReady;
        m_statusDirty = true;
    }

    if (m_waitingForReadyAfterLoad && playerReady)
    {
        m_waitingForReadyAfterLoad = false;
        m_readyAfterLastLoad = true;
        m_statusDirty = true;
        spdlog::info("SkyrimTogetherVR local player became ready after load event");
    }

    m_statusTimer += acEvent.Delta;
    if (!m_statusDirty && m_statusTimer < kSaveLoadStatusWriteInterval)
        return;

    m_statusTimer = 0.0;
    WriteSaveLoadStatusFile();
}

void VRSaveLoadService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    SkyrimTogetherVR::LogRuntimeCheckpoint("connected.saveload.begin");
    TP_UNUSED(acEvent);

    m_connectionState = "online";
    m_lastConnectionError.clear();
    m_statusDirty = true;
    SkyrimTogetherVR::LogRuntimeCheckpoint("connected.saveload.done");
}

void VRSaveLoadService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    m_connectionState = "offline";
    m_statusDirty = true;
}

void VRSaveLoadService::OnConnectionError(const ConnectionErrorEvent& acEvent) noexcept
{
    m_connectionState = "error";
    m_lastConnectionError = acEvent.ErrorDetail.c_str();
    m_statusDirty = true;
}

void VRSaveLoadService::WriteSaveLoadStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_saveLoadStatusPath, std::ios::trunc);
    if (!file)
        return;

    const auto* pPlayer = SkyrimTogetherVR::TryGetReadablePlayerForVR();
    const auto* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
    const auto* pWorldSpace = pPlayer ? pPlayer->GetWorldSpace() : nullptr;
    const auto playerFormId = GetFormId(pPlayer);
    const auto playerCellFormId = GetFormId(pCell);
    const auto playerWorldSpaceFormId = GetFormId(pWorldSpace);
    const auto playerId = ToServerId(m_world, playerFormId);
    const auto playerCellId = ToServerId(m_world, playerCellFormId);
    const auto playerWorldSpaceId = ToServerId(m_world, playerWorldSpaceFormId);

    file << "ready=" << (m_playerReady ? "1" : "0") << "\n";
    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "connectionState=" << m_connectionState << "\n";
    file << "loadGameObserved=" << (m_hasLoadGameEvent ? "1" : "0") << "\n";
    file << "loadGameCount=" << m_loadGameCount << "\n";
    file << "readyAfterLastLoad=" << (m_readyAfterLastLoad ? "1" : "0") << "\n";
    file << "waitingForReadyAfterLoad=" << (m_waitingForReadyAfterLoad ? "1" : "0") << "\n";
    file << "secondsSinceLastLoad=" << (m_hasLoadGameEvent ? m_secondsSinceLastLoad : 0.0) << "\n";
    file << "playerFormId=" << playerFormId << "\n";
    file << "playerCellFormId=" << playerCellFormId << "\n";
    file << "playerWorldSpaceFormId=" << playerWorldSpaceFormId << "\n";
    WriteSaveLoadGameIds(file, playerId, playerCellId, playerWorldSpaceId);
    if (!m_lastConnectionError.empty())
        file << "lastConnectionError=" << m_lastConnectionError << "\n";

    m_statusDirty = false;
}
