#include <TiltedOnlinePCH.h>

#include <Services/VRRemotePlayerService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyPlayerCellChanged.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Services/TransportService.h>
#include <Services/VRActivationService.h>
#include <Services/VRCombatService.h>
#include <Services/VRGrabService.h>
#include <Services/VRHiggsService.h>
#include <Services/VRInventoryService.h>
#include <Services/VRMagicService.h>
#include <Services/VRMovementService.h>
#include <Services/VRPoseService.h>
#include <Services/VRProjectileService.h>
#include <World.h>
#include <vr_common/VRHandoffPath.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE
#define TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE 0
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

namespace
{
constexpr double kRemotePlayerStatusWriteInterval = 1.0;
constexpr char kRemotePlayerStatusFileName[] = "SkyrimTogetherVR.remoteplayers";

std::filesystem::path GetHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
}

bool HasGameId(const GameId& acId) noexcept
{
    return acId.ModId != 0 || acId.BaseId != 0;
}

void WriteGameId(std::ofstream& aFile, const std::string& acPrefix, const GameId& acId)
{
    aFile << acPrefix << ".serverModId=" << acId.ModId << "\n";
    aFile << acPrefix << ".serverBaseId=" << acId.BaseId << "\n";
}

void WriteBool(std::ofstream& aFile, const std::string& acKey, bool aValue)
{
    aFile << acKey << "=" << (aValue ? "1" : "0") << "\n";
}

void AddPlayerId(std::vector<uint32_t>& aIds, uint32_t aPlayerId)
{
    if (!aPlayerId)
        return;

    if (std::find(aIds.begin(), aIds.end(), aPlayerId) == aIds.end())
        aIds.push_back(aPlayerId);
}

template <class TMap> void AddPlayerIds(std::vector<uint32_t>& aIds, const TMap& acMap)
{
    for (const auto& [playerId, value] : acMap)
    {
        TP_UNUSED(value);
        AddPlayerId(aIds, playerId);
    }
}

const char* GetAvatarValidationBlocker(bool aPoseAvailable, bool aHmdAvailable, bool aLeftHandAvailable,
                                       bool aRightHandAvailable, bool aVrikDetected, bool aVrikInterfaceAvailable,
                                       bool aMovementAvailable, bool aSameSpaceAvailable, bool aSameSpace) noexcept
{
    if (!aPoseAvailable)
        return "no_pose";
    if (!aHmdAvailable)
        return "no_hmd";
    if (!aLeftHandAvailable)
        return "no_left_hand";
    if (!aRightHandAvailable)
        return "no_right_hand";
    if (!aVrikDetected)
        return "no_vrik";
    if (!aVrikInterfaceAvailable)
        return "no_vrik_api";
    if (!aMovementAvailable)
        return "no_movement";
    if (!aSameSpaceAvailable)
        return "no_space";
    if (!aSameSpace)
        return "different_space";

    return "ready";
}

const char* GetHiggsAvatarValidationBlocker(bool aAvatarValidationReady, const char* apAvatarValidationBlocker,
                                            bool aHiggsAvailable) noexcept
{
    if (!aAvatarValidationReady)
        return apAvatarValidationBlocker;
    if (!aHiggsAvailable)
        return "no_higgs";

    return "ready";
}
}

VRRemotePlayerService::VRRemotePlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_remotePlayerStatusPath(m_handoffDir / kRemotePlayerStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR remote-player proxy status file: {}", m_remotePlayerStatusPath.string());

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRRemotePlayerService::OnUpdate>(this);
    m_playerJoinedConnection = aDispatcher.sink<NotifyPlayerJoined>().connect<&VRRemotePlayerService::OnPlayerJoined>(this);
    m_playerCellChangedConnection = aDispatcher.sink<NotifyPlayerCellChanged>().connect<&VRRemotePlayerService::OnPlayerCellChanged>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&VRRemotePlayerService::OnPlayerLeft>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRRemotePlayerService::OnDisconnected>(this);
}

void VRRemotePlayerService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    std::vector<uint32_t> playerIds;
    for (const auto& [playerId, player] : m_players)
    {
        TP_UNUSED(player);
        playerIds.push_back(playerId);
    }
    for (auto playerId : playerIds)
        m_players[playerId].AgeSeconds += acEvent.Delta;

    m_statusTimer += acEvent.Delta;
    if (!m_statusDirty && m_statusTimer < kRemotePlayerStatusWriteInterval)
        return;

    m_statusTimer = 0.0;
    m_statusDirty = false;
    WriteRemotePlayerStatusFile();
}

void VRRemotePlayerService::OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;

    auto& player = m_players[acMessage.PlayerId];
    player.PlayerId = acMessage.PlayerId;
    player.Username = acMessage.Username;
    player.WorldSpaceId = acMessage.WorldSpaceId;
    player.CellId = acMessage.CellId;
    player.Level = acMessage.Level;
    player.AgeSeconds = 0.0;
    m_statusDirty = true;
}

void VRRemotePlayerService::OnPlayerCellChanged(const NotifyPlayerCellChanged& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;

    auto& player = m_players[acMessage.PlayerId];
    player.PlayerId = acMessage.PlayerId;
    player.WorldSpaceId = acMessage.WorldSpaceId;
    player.CellId = acMessage.CellId;
    player.AgeSeconds = 0.0;
    m_statusDirty = true;
}

void VRRemotePlayerService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    if (m_players.erase(acMessage.PlayerId))
        m_statusDirty = true;
}

void VRRemotePlayerService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    if (!m_players.empty())
    {
        m_players.clear();
        m_statusDirty = true;
    }
}

void VRRemotePlayerService::WriteRemotePlayerStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_remotePlayerStatusPath, std::ios::trunc);
    if (!file)
        return;

    const auto* pPoseService = m_world.ctx().find<VRPoseService>();
    const TiltedPhoques::Map<uint32_t, VRPoseUpdate> emptyRemotePoses{};
    const auto& remotePoses = pPoseService ? pPoseService->GetRemotePoses() : emptyRemotePoses;

    std::vector<uint32_t> playerIds;
    for (const auto& [playerId, player] : m_players)
    {
        TP_UNUSED(player);
        AddPlayerId(playerIds, playerId);
    }
    AddPlayerIds(playerIds, remotePoses);

#if TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
    const auto* pMovementService = m_world.ctx().find<VRMovementService>();
    const TiltedPhoques::Map<uint32_t, VRMovementUpdate> emptyRemoteMovements{};
    const auto& remoteMovements = pMovementService ? pMovementService->GetRemoteMovements() : emptyRemoteMovements;
    AddPlayerIds(playerIds, remoteMovements);
#endif

#if TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
    const auto* pInventoryService = m_world.ctx().find<VRInventoryService>();
    const TiltedPhoques::Map<uint32_t, VREquipmentUpdate> emptyRemoteEquipment{};
    const auto& remoteEquipment = pInventoryService ? pInventoryService->GetRemoteEquipment() : emptyRemoteEquipment;
    AddPlayerIds(playerIds, remoteEquipment);
#endif

#if TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE
    const auto* pActivationService = m_world.ctx().find<VRActivationService>();
    const TiltedPhoques::Map<uint32_t, VRActivationEvent> emptyRemoteActivations{};
    const auto& remoteActivations = pActivationService ? pActivationService->GetRemoteActivations() : emptyRemoteActivations;
    AddPlayerIds(playerIds, remoteActivations);
#endif

#if TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE
    const auto* pMagicService = m_world.ctx().find<VRMagicService>();
    const TiltedPhoques::Map<uint32_t, VRMagicEffectEvent> emptyRemoteMagicEffects{};
    const auto& remoteMagicEffects = pMagicService ? pMagicService->GetRemoteMagicEffects() : emptyRemoteMagicEffects;
    AddPlayerIds(playerIds, remoteMagicEffects);
#endif

#if TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE
    const auto* pCombatService = m_world.ctx().find<VRCombatService>();
    const TiltedPhoques::Map<uint32_t, VRCombatHitEvent> emptyRemoteCombatHits{};
    const auto& remoteCombatHits = pCombatService ? pCombatService->GetRemoteCombatHits() : emptyRemoteCombatHits;
    AddPlayerIds(playerIds, remoteCombatHits);
#endif

#if TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE
    const auto* pProjectileService = m_world.ctx().find<VRProjectileService>();
    const TiltedPhoques::Map<uint32_t, VRProjectileEvent> emptyRemoteProjectileEvents{};
    const auto& remoteProjectileEvents = pProjectileService ? pProjectileService->GetRemoteProjectileEvents() : emptyRemoteProjectileEvents;
    AddPlayerIds(playerIds, remoteProjectileEvents);
#endif

#if TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE
    const auto* pGrabService = m_world.ctx().find<VRGrabService>();
    const TiltedPhoques::Map<uint32_t, VRGrabEvent> emptyRemoteGrabs{};
    const auto& remoteGrabs = pGrabService ? pGrabService->GetRemoteGrabs() : emptyRemoteGrabs;
    AddPlayerIds(playerIds, remoteGrabs);
#endif

#if TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE
    const auto* pHiggsService = m_world.ctx().find<VRHiggsService>();
    const TiltedPhoques::Map<uint32_t, VRHiggsState> emptyRemoteHiggs{};
    const auto& remoteHiggs = pHiggsService ? pHiggsService->GetRemoteHiggsStates() : emptyRemoteHiggs;
    AddPlayerIds(playerIds, remoteHiggs);
#endif

    std::sort(playerIds.begin(), playerIds.end());

    uint32_t poseMatchedCount = 0;
    uint32_t movementMatchedCount = 0;
    uint32_t equipmentMatchedCount = 0;
    uint32_t activationMatchedCount = 0;
    uint32_t magicMatchedCount = 0;
    uint32_t combatMatchedCount = 0;
    uint32_t projectileMatchedCount = 0;
    uint32_t grabMatchedCount = 0;
    uint32_t higgsMatchedCount = 0;
    uint32_t vrikMatchedCount = 0;
    uint32_t sameCellCount = 0;
    uint32_t sameWorldSpaceCount = 0;
    uint32_t sameSpaceCount = 0;
    uint32_t avatarValidationReadyCount = 0;
    uint32_t higgsAvatarValidationReadyCount = 0;

    file << "ready=1\n";
    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "trackedPlayerCount=" << playerIds.size() << "\n";
    file << "joinedPlayerCount=" << m_players.size() << "\n";
    file << "poseServicePresent=" << (pPoseService ? "1" : "0") << "\n";
    file << "remotePoseCount=" << remotePoses.size() << "\n";
#if TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
    file << "movementServicePresent=" << (pMovementService ? "1" : "0") << "\n";
    file << "remoteMovementCount=" << remoteMovements.size() << "\n";
#else
    file << "movementServicePresent=0\n";
    file << "remoteMovementCount=0\n";
#endif
#if TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
    file << "inventoryServicePresent=" << (pInventoryService ? "1" : "0") << "\n";
    file << "remoteEquipmentCount=" << remoteEquipment.size() << "\n";
#else
    file << "inventoryServicePresent=0\n";
    file << "remoteEquipmentCount=0\n";
#endif
#if TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE
    file << "activationServicePresent=" << (pActivationService ? "1" : "0") << "\n";
    file << "remoteActivationCount=" << remoteActivations.size() << "\n";
#else
    file << "activationServicePresent=0\n";
    file << "remoteActivationCount=0\n";
#endif
#if TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE
    file << "magicServicePresent=" << (pMagicService ? "1" : "0") << "\n";
    file << "remoteMagicEffectCount=" << remoteMagicEffects.size() << "\n";
#else
    file << "magicServicePresent=0\n";
    file << "remoteMagicEffectCount=0\n";
#endif
#if TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE
    file << "combatServicePresent=" << (pCombatService ? "1" : "0") << "\n";
    file << "remoteCombatHitCount=" << remoteCombatHits.size() << "\n";
#else
    file << "combatServicePresent=0\n";
    file << "remoteCombatHitCount=0\n";
#endif
#if TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE
    file << "projectileServicePresent=" << (pProjectileService ? "1" : "0") << "\n";
    file << "remoteProjectileEventCount=" << remoteProjectileEvents.size() << "\n";
#else
    file << "projectileServicePresent=0\n";
    file << "remoteProjectileEventCount=0\n";
#endif
#if TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE
    file << "grabServicePresent=" << (pGrabService ? "1" : "0") << "\n";
    file << "remoteGrabCount=" << remoteGrabs.size() << "\n";
#else
    file << "grabServicePresent=0\n";
    file << "remoteGrabCount=0\n";
#endif
#if TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE
    file << "higgsServicePresent=" << (pHiggsService ? "1" : "0") << "\n";
    file << "remoteHiggsCount=" << remoteHiggs.size() << "\n";
#else
    file << "higgsServicePresent=0\n";
    file << "remoteHiggsCount=0\n";
#endif

    for (auto playerId : playerIds)
    {
        const auto playerIt = m_players.find(playerId);
        const auto* pPlayer = playerIt != m_players.end() ? &playerIt->second : nullptr;
        const auto poseIt = remotePoses.find(playerId);
        const bool poseAvailable = poseIt != remotePoses.end();
        if (poseAvailable)
            ++poseMatchedCount;
        const bool hmdAvailable = poseAvailable && poseIt->second.Hmd.Valid;
        const bool leftHandAvailable = poseAvailable && poseIt->second.LeftHand.Valid;
        const bool rightHandAvailable = poseAvailable && poseIt->second.RightHand.Valid;
        const bool vrikDetected = poseAvailable && poseIt->second.Vrik.Detected;
        const bool vrikInterfaceAvailable = poseAvailable && poseIt->second.Vrik.InterfaceAvailable;
        const bool vrikAvailable = vrikDetected && vrikInterfaceAvailable;
        if (vrikAvailable)
            ++vrikMatchedCount;

        bool movementAvailable = false;
        bool equipmentAvailable = false;
        bool activationAvailable = false;
        bool magicAvailable = false;
        bool combatAvailable = false;
        bool projectileAvailable = false;
        bool grabAvailable = false;
        bool higgsAvailable = false;
        bool sameCellAvailable = false;
        bool sameCell = false;
        bool sameWorldSpaceAvailable = false;
        bool sameWorldSpace = false;
        bool sameSpaceAvailable = false;
        bool sameSpace = false;
        GameId remoteCell = pPlayer ? pPlayer->CellId : GameId{};
        GameId remoteWorldSpace = pPlayer ? pPlayer->WorldSpaceId : GameId{};

#if TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
        const auto movementIt = remoteMovements.find(playerId);
        movementAvailable = movementIt != remoteMovements.end();
        if (movementAvailable)
        {
            ++movementMatchedCount;
            remoteCell = movementIt->second.CellId;
            remoteWorldSpace = movementIt->second.WorldSpaceId;
        }

        if (pMovementService && pMovementService->HasLocalMovement())
        {
            const auto& localMovement = pMovementService->GetLocalMovement();
            sameCellAvailable = HasGameId(localMovement.CellId) && HasGameId(remoteCell);
            sameCell = sameCellAvailable && localMovement.CellId == remoteCell;
            sameWorldSpaceAvailable = HasGameId(localMovement.WorldSpaceId) && HasGameId(remoteWorldSpace);
            sameWorldSpace = sameWorldSpaceAvailable && localMovement.WorldSpaceId == remoteWorldSpace;
        }
#endif

#if TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
        equipmentAvailable = remoteEquipment.find(playerId) != remoteEquipment.end();
        if (equipmentAvailable)
            ++equipmentMatchedCount;
#endif

#if TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE
        activationAvailable = remoteActivations.find(playerId) != remoteActivations.end();
        if (activationAvailable)
            ++activationMatchedCount;
#endif

#if TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE
        magicAvailable = remoteMagicEffects.find(playerId) != remoteMagicEffects.end();
        if (magicAvailable)
            ++magicMatchedCount;
#endif

#if TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE
        combatAvailable = remoteCombatHits.find(playerId) != remoteCombatHits.end();
        if (combatAvailable)
            ++combatMatchedCount;
#endif

#if TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE
        projectileAvailable = remoteProjectileEvents.find(playerId) != remoteProjectileEvents.end();
        if (projectileAvailable)
            ++projectileMatchedCount;
#endif

#if TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE
        grabAvailable = remoteGrabs.find(playerId) != remoteGrabs.end();
        if (grabAvailable)
            ++grabMatchedCount;
#endif

#if TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE
        higgsAvailable = remoteHiggs.find(playerId) != remoteHiggs.end();
        if (higgsAvailable)
            ++higgsMatchedCount;
#endif

        if (sameCell)
            ++sameCellCount;
        if (sameWorldSpace)
            ++sameWorldSpaceCount;

        sameSpaceAvailable = sameCellAvailable || sameWorldSpaceAvailable;
        sameSpace = sameCell || sameWorldSpace;
        if (sameSpace)
            ++sameSpaceCount;

        const bool avatarValidationReady =
            poseAvailable && hmdAvailable && leftHandAvailable && rightHandAvailable && vrikAvailable &&
            movementAvailable && sameSpace;
        if (avatarValidationReady)
            ++avatarValidationReadyCount;
        const auto* pAvatarValidationBlocker = GetAvatarValidationBlocker(
            poseAvailable, hmdAvailable, leftHandAvailable, rightHandAvailable, vrikDetected,
            vrikInterfaceAvailable, movementAvailable, sameSpaceAvailable, sameSpace);
        const bool higgsAvatarValidationReady = avatarValidationReady && higgsAvailable;
        if (higgsAvatarValidationReady)
            ++higgsAvatarValidationReadyCount;
        const auto* pHiggsAvatarValidationBlocker =
            GetHiggsAvatarValidationBlocker(avatarValidationReady, pAvatarValidationBlocker, higgsAvailable);

        const std::string prefix = "remotePlayer." + std::to_string(playerId);
        file << prefix << ".playerId=" << playerId << "\n";
        file << prefix << ".username=" << (pPlayer ? pPlayer->Username.c_str() : "") << "\n";
        file << prefix << ".level=" << (pPlayer ? pPlayer->Level : 0) << "\n";
        file << prefix << ".ageSeconds=" << (pPlayer ? pPlayer->AgeSeconds : 0.0) << "\n";
        WriteGameId(file, prefix + ".cell", remoteCell);
        WriteGameId(file, prefix + ".worldSpace", remoteWorldSpace);
        WriteBool(file, prefix + ".poseAvailable", poseAvailable);
        if (poseAvailable)
            file << prefix << ".poseSequence=" << poseIt->second.Sequence << "\n";
        WriteBool(file, prefix + ".hmdAvailable", hmdAvailable);
        WriteBool(file, prefix + ".leftHandAvailable", leftHandAvailable);
        WriteBool(file, prefix + ".rightHandAvailable", rightHandAvailable);
        WriteBool(file, prefix + ".vrikDetected", vrikDetected);
        WriteBool(file, prefix + ".vrikInterfaceAvailable", vrikInterfaceAvailable);
        WriteBool(file, prefix + ".vrikAvailable", vrikAvailable);
        WriteBool(file, prefix + ".movementAvailable", movementAvailable);
        WriteBool(file, prefix + ".equipmentAvailable", equipmentAvailable);
        WriteBool(file, prefix + ".activationAvailable", activationAvailable);
        WriteBool(file, prefix + ".magicAvailable", magicAvailable);
        WriteBool(file, prefix + ".combatAvailable", combatAvailable);
        WriteBool(file, prefix + ".projectileAvailable", projectileAvailable);
        WriteBool(file, prefix + ".grabAvailable", grabAvailable);
        WriteBool(file, prefix + ".higgsAvailable", higgsAvailable);
        WriteBool(file, prefix + ".sameCellAvailable", sameCellAvailable);
        WriteBool(file, prefix + ".sameCell", sameCell);
        WriteBool(file, prefix + ".sameWorldSpaceAvailable", sameWorldSpaceAvailable);
        WriteBool(file, prefix + ".sameWorldSpace", sameWorldSpace);
        WriteBool(file, prefix + ".sameSpaceAvailable", sameSpaceAvailable);
        WriteBool(file, prefix + ".sameSpace", sameSpace);
        WriteBool(file, prefix + ".avatarValidationReady", avatarValidationReady);
        file << prefix << ".avatarValidationBlocker=" << pAvatarValidationBlocker << "\n";
        WriteBool(file, prefix + ".higgsAvatarValidationReady", higgsAvatarValidationReady);
        file << prefix << ".higgsAvatarValidationBlocker=" << pHiggsAvatarValidationBlocker << "\n";
    }

    file << "poseMatchedCount=" << poseMatchedCount << "\n";
    file << "movementMatchedCount=" << movementMatchedCount << "\n";
    file << "equipmentMatchedCount=" << equipmentMatchedCount << "\n";
    file << "activationMatchedCount=" << activationMatchedCount << "\n";
    file << "magicMatchedCount=" << magicMatchedCount << "\n";
    file << "combatMatchedCount=" << combatMatchedCount << "\n";
    file << "projectileMatchedCount=" << projectileMatchedCount << "\n";
    file << "grabMatchedCount=" << grabMatchedCount << "\n";
    file << "higgsMatchedCount=" << higgsMatchedCount << "\n";
    file << "vrikMatchedCount=" << vrikMatchedCount << "\n";
    file << "sameCellCount=" << sameCellCount << "\n";
    file << "sameWorldSpaceCount=" << sameWorldSpaceCount << "\n";
    file << "sameSpaceCount=" << sameSpaceCount << "\n";
    file << "avatarValidationReadyCount=" << avatarValidationReadyCount << "\n";
    file << "higgsAvatarValidationReadyCount=" << higgsAvatarValidationReadyCount << "\n";
}
