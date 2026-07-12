#include <TiltedOnlinePCH.h>

#include <Services/VRInventoryService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Forms/TESForm.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyVREquipmentUpdate.h>
#include <Messages/RequestVREquipmentUpdate.h>
#include <PlayerCharacter.h>
#include <VR/VRPlayerReadiness.h>
#include <Services/TransportService.h>
#include <World.h>
#include <vr_common/VRHandoffPath.h>

#include <fstream>
#include <string>

namespace
{
constexpr double kEquipmentSendInterval = 1.0;
constexpr double kInventoryStatusWriteInterval = 1.0;
constexpr char kInventoryStatusFileName[] = "SkyrimTogetherVR.inventory";

std::filesystem::path GetHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
}

bool IsVrPlayerReadyForInventory(const PlayerCharacter* apPlayer) noexcept
{
    return apPlayer && apPlayer->GetBaseFormData() && apPlayer->GetParentCellData();
}

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

void WriteLocalMappedForm(std::ofstream& aFile, World& aWorld, const char* apPrefix, uint32_t aFormId)
{
    const GameId serverId = ToServerId(aWorld, aFormId);

    aFile << apPrefix << ".formId=" << aFormId << "\n";
    aFile << apPrefix << ".serverModId=" << serverId.ModId << "\n";
    aFile << apPrefix << ".serverBaseId=" << serverId.BaseId << "\n";
}

void WriteEquipmentGameId(std::ofstream& aFile, const std::string& acPrefix, const GameId& acId)
{
    aFile << acPrefix << ".serverModId=" << acId.ModId << "\n";
    aFile << acPrefix << ".serverBaseId=" << acId.BaseId << "\n";
}

void WriteEquipmentUpdate(std::ofstream& aFile, const std::string& acPrefix, const VREquipmentUpdate& acEquipment)
{
    aFile << acPrefix << ".sequence=" << acEquipment.Sequence << "\n";
    aFile << acPrefix << ".weaponDrawn=" << (acEquipment.WeaponDrawn ? "1" : "0") << "\n";
    aFile << acPrefix << ".weaponFullyDrawn=" << (acEquipment.WeaponFullyDrawn ? "1" : "0") << "\n";
    WriteEquipmentGameId(aFile, acPrefix + ".leftWeapon", acEquipment.LeftWeapon);
    WriteEquipmentGameId(aFile, acPrefix + ".rightWeapon", acEquipment.RightWeapon);
    WriteEquipmentGameId(aFile, acPrefix + ".ammo", acEquipment.Ammo);
    WriteEquipmentGameId(aFile, acPrefix + ".leftSpell", acEquipment.LeftSpell);
    WriteEquipmentGameId(aFile, acPrefix + ".rightSpell", acEquipment.RightSpell);
    WriteEquipmentGameId(aFile, acPrefix + ".powerOrShout", acEquipment.PowerOrShout);
}

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}
}

VRInventoryService::VRInventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_inventoryStatusPath(m_handoffDir / kInventoryStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR inventory handoff status file: {}", m_inventoryStatusPath.string());

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRInventoryService::OnUpdate>(this);
    m_vrEquipmentUpdateConnection = aDispatcher.sink<NotifyVREquipmentUpdate>().connect<&VRInventoryService::OnVREquipmentUpdate>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&VRInventoryService::OnPlayerLeft>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRInventoryService::OnDisconnected>(this);
}

void VRInventoryService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    VREquipmentUpdate equipment{};
    if (CaptureLocalEquipment(equipment))
    {
        equipment.Sequence = m_lastEquipment.Sequence;
        if (!m_hasEquipment || equipment != m_lastEquipment)
        {
            m_lastEquipment = equipment;
            m_hasEquipment = true;
            m_statusDirty = true;
        }
    }

    m_sendTimer += acEvent.Delta;
    if (m_sendTimer >= kEquipmentSendInterval)
    {
        m_sendTimer = 0.0;
        SendEquipmentUpdate();
    }

    m_statusTimer += acEvent.Delta;
    if (!m_statusDirty && m_statusTimer < kInventoryStatusWriteInterval)
        return;

    m_statusTimer = 0.0;
    WriteInventoryStatusFile();
}

void VRInventoryService::OnVREquipmentUpdate(const NotifyVREquipmentUpdate& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;

    const auto existingIt = m_remoteEquipment.find(acMessage.PlayerId);
    if (existingIt != m_remoteEquipment.end() && !IsNewerSequence(acMessage.Equipment.Sequence, existingIt->second.Sequence))
        return;

    m_remoteEquipment[acMessage.PlayerId] = acMessage.Equipment;
    m_statusDirty = true;
}

void VRInventoryService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    if (m_remoteEquipment.erase(acMessage.PlayerId))
        m_statusDirty = true;
}

void VRInventoryService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    if (!m_remoteEquipment.empty())
    {
        m_remoteEquipment.clear();
        m_statusDirty = true;
    }
}

bool VRInventoryService::CaptureLocalEquipment(VREquipmentUpdate& aUpdate) noexcept
{
    const auto* pPlayer = SkyrimTogetherVR::TryGetReadablePlayerForVR();
    if (!IsVrPlayerReadyForInventory(pPlayer))
        return false;

    const auto& actorState = pPlayer->GetActorStateData();
    aUpdate.WeaponDrawn = actorState.IsWeaponDrawn();
    aUpdate.WeaponFullyDrawn = actorState.IsWeaponFullyDrawn();
    aUpdate.LeftWeapon = ToServerId(m_world, GetFormId(pPlayer->GetEquippedWeapon(0)));
    aUpdate.RightWeapon = ToServerId(m_world, GetFormId(pPlayer->GetEquippedWeapon(1)));
    aUpdate.Ammo = ToServerId(m_world, GetFormId(pPlayer->GetEquippedAmmo()));
    aUpdate.LeftSpell = ToServerId(m_world, GetFormId(pPlayer->GetSelectedSpellData(0)));
    aUpdate.RightSpell = ToServerId(m_world, GetFormId(pPlayer->GetSelectedSpellData(1)));
    aUpdate.PowerOrShout = ToServerId(m_world, GetFormId(pPlayer->GetSelectedPowerOrShoutData()));
    return true;
}

void VRInventoryService::SendEquipmentUpdate() noexcept
{
    if (!m_transport.IsOnline() || !m_hasEquipment)
        return;

    RequestVREquipmentUpdate request{};
    m_lastEquipment.Sequence = ++m_sequence;
    request.Equipment = m_lastEquipment;
    m_transport.Send(request);
}

void VRInventoryService::WriteInventoryStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_inventoryStatusPath, std::ios::trunc);
    if (!file)
        return;

    const auto* pPlayer = SkyrimTogetherVR::TryGetReadablePlayerForVR();
    const bool ready = IsVrPlayerReadyForInventory(pPlayer);

    file << "ready=" << (ready ? "1" : "0") << "\n";
    file << "policy=equipment_snapshot_only\n";
    file << "fullInventoryTraversal=0\n";
    file << "inventoryMutation=0\n";
    file << "remoteEquipmentMutation=0\n";
    file << "normalInventoryPackets=0\n";
    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "playerFormId=" << GetFormId(pPlayer) << "\n";
    file << "localEquipmentAvailable=" << (m_hasEquipment ? "1" : "0") << "\n";
    file << "remoteEquipmentCount=" << m_remoteEquipment.size() << "\n";

    if (!ready)
    {
        m_statusDirty = false;
        return;
    }

    const auto& actorState = pPlayer->GetActorStateData();
    file << "weaponDrawn=" << (actorState.IsWeaponDrawn() ? "1" : "0") << "\n";
    file << "weaponFullyDrawn=" << (actorState.IsWeaponFullyDrawn() ? "1" : "0") << "\n";

    WriteLocalMappedForm(file, m_world, "leftWeapon", GetFormId(pPlayer->GetEquippedWeapon(0)));
    WriteLocalMappedForm(file, m_world, "rightWeapon", GetFormId(pPlayer->GetEquippedWeapon(1)));
    WriteLocalMappedForm(file, m_world, "ammo", GetFormId(pPlayer->GetEquippedAmmo()));
    WriteLocalMappedForm(file, m_world, "leftSpell", GetFormId(pPlayer->GetSelectedSpellData(0)));
    WriteLocalMappedForm(file, m_world, "rightSpell", GetFormId(pPlayer->GetSelectedSpellData(1)));
    WriteLocalMappedForm(file, m_world, "powerOrShout", GetFormId(pPlayer->GetSelectedPowerOrShoutData()));

    if (m_hasEquipment)
        WriteEquipmentUpdate(file, "localEquipment", m_lastEquipment);

    uint32_t remoteIndex = 0;
    for (const auto& [playerId, equipment] : m_remoteEquipment)
    {
        const std::string prefix = "remoteEquipment." + std::to_string(remoteIndex);
        file << prefix << ".playerId=" << playerId << "\n";
        WriteEquipmentUpdate(file, prefix, equipment);
        ++remoteIndex;
    }

    m_statusDirty = false;
}
