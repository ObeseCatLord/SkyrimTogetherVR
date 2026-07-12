#include <TiltedOnlinePCH.h>

#include <Services/VRMovementService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Forms/TESForm.h>
#include <Forms/TESWorldSpace.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyVRMovementUpdate.h>
#include <Messages/RequestVRMovementUpdate.h>
#include <PlayerCharacter.h>
#include <VR/VRPlayerReadiness.h>
#include <Services/TransportService.h>
#include <World.h>
#include <vr_common/VRHandoffPath.h>

#include <fstream>
#include <string>
#include <vector>

namespace
{
constexpr double kMovementSendInterval = 1.0 / 20.0;
constexpr double kMovementStatusWriteInterval = 0.25;
constexpr double kRemoteMovementStaleSeconds = 3.0;
constexpr char kMovementStatusFileName[] = "SkyrimTogetherVR.movement";

std::filesystem::path GetHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
}

bool IsVrPlayerReadyForMovement(const PlayerCharacter* apPlayer) noexcept
{
    return apPlayer && apPlayer->GetBaseFormData() && apPlayer->GetParentCellData();
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

void WriteMovementUpdate(std::ofstream& aFile, const std::string& acPrefix, const VRMovementUpdate& acMovement)
{
    aFile << acPrefix << ".sequence=" << acMovement.Sequence << "\n";
    WriteGameId(aFile, acPrefix + ".cell", acMovement.CellId);
    WriteGameId(aFile, acPrefix + ".worldSpace", acMovement.WorldSpaceId);
    aFile << acPrefix << ".position=" << acMovement.Position.x << "," << acMovement.Position.y << "," << acMovement.Position.z << "\n";
    aFile << acPrefix << ".rotation=" << acMovement.Rotation.x << "," << acMovement.Rotation.y << "\n";
    aFile << acPrefix << ".direction=" << acMovement.Direction << "\n";
}

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}
}

VRMovementService::VRMovementService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_movementStatusPath(m_handoffDir / kMovementStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR movement handoff status file: {}", m_movementStatusPath.string());

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRMovementService::OnUpdate>(this);
    m_vrMovementUpdateConnection = aDispatcher.sink<NotifyVRMovementUpdate>().connect<&VRMovementService::OnVRMovementUpdate>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&VRMovementService::OnPlayerLeft>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRMovementService::OnDisconnected>(this);
}

void VRMovementService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    VRMovementUpdate movement{};
    if (CaptureLocalMovement(movement))
    {
        movement.Sequence = m_lastMovement.Sequence;
        if (!m_hasMovement || movement != m_lastMovement)
        {
            m_lastMovement = movement;
            m_hasMovement = true;
            m_statusDirty = true;
        }
    }

    PruneRemoteMovements(acEvent.Delta);

    m_sendTimer += acEvent.Delta;
    if (m_sendTimer >= kMovementSendInterval)
    {
        m_sendTimer = 0.0;
        SendMovementUpdate();
    }

    m_statusTimer += acEvent.Delta;
    if (!m_statusDirty && m_statusTimer < kMovementStatusWriteInterval)
        return;

    m_statusTimer = 0.0;
    WriteMovementStatusFile();
}

void VRMovementService::OnVRMovementUpdate(const NotifyVRMovementUpdate& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;

    const auto existingIt = m_remoteMovements.find(acMessage.PlayerId);
    if (existingIt != m_remoteMovements.end() && !IsNewerSequence(acMessage.Movement.Sequence, existingIt->second.Sequence))
        return;

    m_remoteMovements[acMessage.PlayerId] = acMessage.Movement;
    m_remoteMovementAges[acMessage.PlayerId] = 0.0;
    m_statusDirty = true;
}

void VRMovementService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    const auto movementCount = m_remoteMovements.erase(acMessage.PlayerId);
    const auto ageCount = m_remoteMovementAges.erase(acMessage.PlayerId);
    if (movementCount || ageCount)
        m_statusDirty = true;
}

void VRMovementService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    if (!m_remoteMovements.empty() || !m_remoteMovementAges.empty())
    {
        m_remoteMovements.clear();
        m_remoteMovementAges.clear();
        m_statusDirty = true;
    }
}

void VRMovementService::PruneRemoteMovements(double aDelta) noexcept
{
    if (!m_transport.IsOnline())
    {
        if (!m_remoteMovements.empty() || !m_remoteMovementAges.empty())
        {
            m_remoteMovements.clear();
            m_remoteMovementAges.clear();
            m_statusDirty = true;
        }
        return;
    }

    std::vector<uint32_t> trackedPlayerIds;
    std::vector<uint32_t> expiredPlayerIds;
    for (const auto& [playerId, age] : m_remoteMovementAges)
    {
        trackedPlayerIds.push_back(playerId);
        if (age + aDelta >= kRemoteMovementStaleSeconds)
            expiredPlayerIds.push_back(playerId);
    }
    for (auto playerId : trackedPlayerIds)
        m_remoteMovementAges[playerId] += aDelta;

    for (auto playerId : expiredPlayerIds)
    {
        m_remoteMovementAges.erase(playerId);
        m_remoteMovements.erase(playerId);
        m_statusDirty = true;
    }
}

bool VRMovementService::CaptureLocalMovement(VRMovementUpdate& aUpdate) noexcept
{
    const auto* pPlayer = SkyrimTogetherVR::TryGetReadablePlayerForVR();
    if (!IsVrPlayerReadyForMovement(pPlayer))
        return false;

    const auto* pWorldSpace = pPlayer->GetWorldSpace();
    aUpdate.CellId = ToServerId(m_world, pPlayer->GetCellId());
    aUpdate.WorldSpaceId = ToServerId(m_world, pWorldSpace ? pWorldSpace->GetFormIdData() : 0);
    const auto& position = pPlayer->GetPositionData();
    const auto& rotation = pPlayer->GetRotationData();
    aUpdate.Position = glm::vec3(position.x, position.y, position.z);
    aUpdate.Rotation.x = rotation.x;
    aUpdate.Rotation.y = rotation.z;
    aUpdate.Direction = rotation.z;
    return true;
}

void VRMovementService::SendMovementUpdate() noexcept
{
    if (!m_transport.IsOnline() || !m_hasMovement)
        return;

    RequestVRMovementUpdate request{};
    m_lastMovement.Sequence = ++m_sequence;
    request.Movement = m_lastMovement;
    m_transport.Send(request);
}

void VRMovementService::WriteMovementStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_movementStatusPath, std::ios::trunc);
    if (!file)
        return;

    const auto* pPlayer = SkyrimTogetherVR::TryGetReadablePlayerForVR();
    const bool ready = IsVrPlayerReadyForMovement(pPlayer);

    file << "ready=" << (ready ? "1" : "0") << "\n";
    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "localMovementAvailable=" << (m_hasMovement ? "1" : "0") << "\n";
    file << "remoteMovementCount=" << m_remoteMovements.size() << "\n";

    if (m_hasMovement)
        WriteMovementUpdate(file, "localMovement", m_lastMovement);

    for (const auto& [playerId, movement] : m_remoteMovements)
    {
        const auto ageIt = m_remoteMovementAges.find(playerId);
        const auto age = ageIt != m_remoteMovementAges.end() ? ageIt->second : 0.0;
        const auto prefix = std::string("remoteMovement.") + std::to_string(playerId);

        file << prefix << ".ageSeconds=" << age << "\n";
        WriteMovementUpdate(file, prefix, movement);
    }

    m_statusDirty = false;
}
