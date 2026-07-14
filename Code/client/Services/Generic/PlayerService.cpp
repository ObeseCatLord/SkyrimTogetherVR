#include <Services/PlayerService.h>

#include <World.h>

#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/GridCellChangeEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/PlayerDialogueEvent.h>
#include <Events/PlayerLevelEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/BeastFormChangeEvent.h>

#include <Messages/PlayerRespawnRequest.h>
#include <Messages/NotifyPlayerRespawn.h>
#include <Messages/ShiftGridCellRequest.h>
#include <Messages/EnterExteriorCellRequest.h>
#include <Messages/EnterInteriorCellRequest.h>
#include <Messages/PlayerDialogueRequest.h>
#include <Messages/PlayerLevelRequest.h>

#include <Structs/ServerSettings.h>

#include <PlayerCharacter.h>
#include <VR/VRPlayerReadiness.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESGlobal.h>
#include <Games/Overrides.h>
#include <Games/References.h>
#include <AI/AIProcess.h>
#include <EquipManager.h>
#include <Forms/TESRace.h>
#include <vr_common/VRHandoffPath.h>

#include <filesystem>
#include <fstream>
#include <string>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY
#define TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE
#define TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE 0
#endif

namespace
{
constexpr bool kVrCellOnlyPlayerService = TP_SKYRIM_VR && TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY;
constexpr bool kVrPlayerCellStatusEnabled = TP_SKYRIM_VR && TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE;
constexpr uint16_t kVrFallbackLevel = 1;
constexpr double kVrPlayerCellStatusWriteInterval = 1.0;
constexpr char kVrPlayerCellStatusFileName[] = "SkyrimTogetherVR.playercell";

std::filesystem::path GetVrHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
}

void WriteGameId(std::ofstream& aFile, const std::string& acPrefix, const GameId& acId)
{
    aFile << acPrefix << ".serverModId=" << acId.ModId << "\n";
    aFile << acPrefix << ".serverBaseId=" << acId.BaseId << "\n";
}

void WriteGridCoords(std::ofstream& aFile, const char* apKey, const GridCellCoords& acCoords)
{
    aFile << apKey << "=" << acCoords.X << "," << acCoords.Y << "\n";
}
}

PlayerService::PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_vrPlayerCellStatusPath(GetVrHandoffDirectory() / kVrPlayerCellStatusFileName)
{
    if constexpr (kVrPlayerCellStatusEnabled)
    {
        std::error_code ec;
        std::filesystem::create_directories(m_vrPlayerCellStatusPath.parent_path(), ec);
        spdlog::info("SkyrimTogetherVR player cell handoff status file: {}", m_vrPlayerCellStatusPath.string());
    }

    if constexpr (kVrCellOnlyPlayerService)
    {
        spdlog::info("SkyrimTogetherVR PlayerService network-only mode: sending cell/grid changes; level reads are disabled");
        m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&PlayerService::OnUpdate>(this);
        m_gridCellChangeConnection = m_dispatcher.sink<GridCellChangeEvent>().connect<&PlayerService::OnGridCellChangeEvent>(this);
        m_cellChangeConnection = m_dispatcher.sink<CellChangeEvent>().connect<&PlayerService::OnCellChangeEvent>(this);
        return;
    }

    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&PlayerService::OnUpdate>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&PlayerService::OnConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&PlayerService::OnDisconnected>(this);
    m_settingsConnection = m_dispatcher.sink<ServerSettings>().connect<&PlayerService::OnServerSettingsReceived>(this);
    m_notifyRespawnConnection = m_dispatcher.sink<NotifyPlayerRespawn>().connect<&PlayerService::OnNotifyPlayerRespawn>(this);
    m_gridCellChangeConnection = m_dispatcher.sink<GridCellChangeEvent>().connect<&PlayerService::OnGridCellChangeEvent>(this);
    m_cellChangeConnection = m_dispatcher.sink<CellChangeEvent>().connect<&PlayerService::OnCellChangeEvent>(this);
    m_playerDialogueConnection = m_dispatcher.sink<PlayerDialogueEvent>().connect<&PlayerService::OnPlayerDialogueEvent>(this);
    m_playerLevelConnection = m_dispatcher.sink<PlayerLevelEvent>().connect<&PlayerService::OnPlayerLevelEvent>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&PlayerService::OnPartyJoinedEvent>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&PlayerService::OnPartyLeftEvent>(this);
}

void PlayerService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if constexpr (kVrCellOnlyPlayerService)
    {
        RunVrLevelUpdates(acEvent.Delta);
        RunVrPlayerCellStatusWrite(acEvent.Delta);
        return;
    }

    RunRespawnUpdates(acEvent.Delta);
    RunPostDeathUpdates(acEvent.Delta);
    RunDifficultyUpdates();
    RunLevelUpdates();
    RunBeastFormDetection();
    if constexpr (kVrPlayerCellStatusEnabled)
        RunVrPlayerCellStatusWrite(acEvent.Delta);
}

void PlayerService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    // TODO: SkyrimTogether.esm
    TESGlobal* pKillMove = Cast<TESGlobal>(TESForm::GetById(0x100F19));
    pKillMove->SetValueData(0.f);

    TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
    pWorldEncountersEnabled->SetValueData(0.f);
}

void PlayerService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    PlayerCharacter::Get()->SetDifficulty(m_previousDifficulty);
    m_serverDifficulty = m_previousDifficulty = 6;

    ToggleDeathSystem(false);

    TESGlobal* pKillMove = Cast<TESGlobal>(TESForm::GetById(0x100F19));
    pKillMove->SetValueData(1.f);

    // Restore to the default value (150 in skyrim, 175 in fallout 4)
    float* greetDistance = Settings::GetGreetDistance();
    *greetDistance = 150.f;

    TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
    pWorldEncountersEnabled->SetValueData(1.f);
}

void PlayerService::OnServerSettingsReceived(const ServerSettings& acSettings) noexcept
{
    m_previousDifficulty = *Settings::GetDifficulty();
    PlayerCharacter::Get()->SetDifficulty(acSettings.Difficulty);
    m_serverDifficulty = acSettings.Difficulty;

    if (!acSettings.GreetingsEnabled)
    {
        float* greetDistance = Settings::GetGreetDistance();
        *greetDistance = 0.f;
    }

    ToggleDeathSystem(acSettings.DeathSystemEnabled);
}

void PlayerService::OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) const noexcept
{
    PlayerCharacter::Get()->PayGold(acMessage.GoldLost);

    std::string message = fmt::format("You died and lost {} gold.", acMessage.GoldLost);
    Utils::ShowHudMessage(String(message));
}

void PlayerService::OnGridCellChangeEvent(const GridCellChangeEvent& acEvent) noexcept
{
    if constexpr (kVrCellOnlyPlayerService)
    {
        if (!m_transport.IsOnline())
        {
            ++m_vrOfflineSkippedRequestCount;
            m_vrPlayerCellStatusDirty = true;
            return;
        }
    }

    uint32_t baseId = 0;
    uint32_t modId = 0;

    if (m_world.GetModSystem().GetServerModId(acEvent.WorldSpaceId, modId, baseId))
    {
        ShiftGridCellRequest request;
        request.WorldSpaceId = GameId(modId, baseId);
        request.PlayerCell = acEvent.PlayerCell;
        request.CenterCoords = acEvent.CenterCoords;
        request.Cells = acEvent.Cells;

        const bool sent = m_transport.Send(request);

        if constexpr (kVrPlayerCellStatusEnabled)
        {
            if (!sent)
            {
                ++m_vrOfflineSkippedRequestCount;
                m_vrPlayerCellStatusDirty = true;
                return;
            }

            ++m_vrGridCellRequestCount;
            m_lastVrGridWorldSpace = request.WorldSpaceId;
            m_lastVrGridPlayerCell = request.PlayerCell;
            m_lastVrGridCenterCoords = request.CenterCoords;
            m_lastVrGridCellCount = static_cast<uint32_t>(request.Cells.size());
            m_lastVrGridConnectionGeneration = m_transport.GetConnectionGeneration();
            m_hasVrGridCellRequest = true;
            m_vrPlayerCellStatusDirty = true;
        }
    }
    else if constexpr (kVrPlayerCellStatusEnabled)
    {
        ++m_vrWorldSpaceTranslationFailureCount;
        m_vrPlayerCellStatusDirty = true;
    }
}

void PlayerService::OnCellChangeEvent(const CellChangeEvent& acEvent) noexcept
{
    if constexpr (kVrCellOnlyPlayerService)
    {
        if (!m_transport.IsOnline())
        {
            ++m_vrOfflineSkippedRequestCount;
            m_vrPlayerCellStatusDirty = true;
            return;
        }
    }

    if (acEvent.WorldSpaceId)
    {
        EnterExteriorCellRequest message;
        message.CellId = acEvent.CellId;
        message.WorldSpaceId = acEvent.WorldSpaceId;
        message.CurrentCoords = acEvent.CurrentCoords;

        const bool sent = m_transport.Send(message);

        if constexpr (kVrPlayerCellStatusEnabled)
        {
            if (!sent)
            {
                ++m_vrOfflineSkippedRequestCount;
                m_vrPlayerCellStatusDirty = true;
                return;
            }

            ++m_vrExteriorCellRequestCount;
            m_lastVrCell = message.CellId;
            m_lastVrCellWorldSpace = message.WorldSpaceId;
            m_lastVrCellCurrentCoords = message.CurrentCoords;
            m_lastVrCellWasExterior = true;
            m_lastVrCellConnectionGeneration = m_transport.GetConnectionGeneration();
            m_hasVrCellRequest = true;
            m_vrPlayerCellStatusDirty = true;
        }
    }
    else
    {
        EnterInteriorCellRequest message;
        message.CellId = acEvent.CellId;

        const bool sent = m_transport.Send(message);

        if constexpr (kVrPlayerCellStatusEnabled)
        {
            if (!sent)
            {
                ++m_vrOfflineSkippedRequestCount;
                m_vrPlayerCellStatusDirty = true;
                return;
            }

            ++m_vrInteriorCellRequestCount;
            m_lastVrCell = message.CellId;
            m_lastVrCellWorldSpace = GameId{};
            m_lastVrCellCurrentCoords = GridCellCoords{};
            m_lastVrCellWasExterior = false;
            m_lastVrCellConnectionGeneration = m_transport.GetConnectionGeneration();
            m_hasVrCellRequest = true;
            m_vrPlayerCellStatusDirty = true;
        }
    }
}

void PlayerService::OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;

    const auto& partyService = m_world.GetPartyService();
    if (!partyService.IsInParty())
        return;

    PlayerDialogueRequest request{};
    request.Text = acEvent.Text;

    m_transport.Send(request);
}

void PlayerService::OnPlayerLevelEvent(const PlayerLevelEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    PlayerLevelRequest request{};
    request.NewLevel = PlayerCharacter::Get()->GetLevel();

    m_transport.Send(request);

    if constexpr (kVrPlayerCellStatusEnabled)
    {
        m_cachedVrLevel = request.NewLevel;
        m_lastVrLevelSent = request.NewLevel;
        ++m_vrLevelRequestCount;
        m_vrPlayerCellStatusDirty = true;
    }
}

void PlayerService::OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept
{
    // TODO: this can be done a bit prettier
    if (acEvent.IsLeader)
    {
        TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
        pWorldEncountersEnabled->SetValueData(1.f);
    }
}

void PlayerService::OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept
{
    // TODO: this can be done a bit prettier
    if (World::Get().GetTransport().IsConnected())
    {
        TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
        pWorldEncountersEnabled->SetValueData(0.f);
    }
}

void PlayerService::RunRespawnUpdates(const double acDeltaTime) noexcept
{
    if (!m_isDeathSystemEnabled)
        return;

    static bool s_startTimer = false;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer->GetActorStateData().IsBleedingOut())
    {
        const auto* pMainSpell = pPlayer->GetSelectedSpellData(0);
        const auto* pSecondarySpell = pPlayer->GetSelectedSpellData(1);
        const auto* pPower = pPlayer->GetSelectedPowerOrShoutData();
        m_cachedMainSpellId = pMainSpell ? pMainSpell->GetFormIdData() : 0;
        m_cachedSecondarySpellId = pSecondarySpell ? pSecondarySpell->GetFormIdData() : 0;
        m_cachedPowerId = pPower ? pPower->GetFormIdData() : 0;

        s_startTimer = false;
        return;
    }

    if (!s_startTimer)
    {
        s_startTimer = true;
        m_respawnTimer = 5.0;
        FadeOutGame(true, true, 3.0f, true, 2.0f);

        // If a player dies not by its health reaching 0, getting it up from its bleedout state isn't possible
        // just by setting its health back to max. Therefore, put it to 0.
        if (pPlayer->GetActorValue(ActorValueInfo::kHealth) > 0.f)
            pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, 0);

        pPlayer->PayCrimeGoldToAllFactions();
    }

    m_respawnTimer -= acDeltaTime;

    if (m_respawnTimer <= 0.0)
    {
        pPlayer->RespawnPlayer();

        m_knockdownTimer = 1.5;
        m_knockdownStart = true;

        m_transport.Send(PlayerRespawnRequest());

        s_startTimer = false;

        auto* pEquipManager = EquipManager::Get();
        TESForm* pSpell = TESForm::GetById(m_cachedMainSpellId);
        if (pSpell)
            pEquipManager->EquipSpell(pPlayer, pSpell, 0);
        pSpell = TESForm::GetById(m_cachedSecondarySpellId);
        if (pSpell)
            pEquipManager->EquipSpell(pPlayer, pSpell, 1);
        pSpell = TESForm::GetById(m_cachedPowerId);
        if (pSpell)
            pEquipManager->EquipShout(pPlayer, pSpell);
    }
}

// Doesn't seem to respawn quite yet
void PlayerService::RunPostDeathUpdates(const double acDeltaTime) noexcept
{
    if (!m_isDeathSystemEnabled)
        return;

    // If a player dies in ragdoll, it gets stuck.
    // This code ragdolls the player again upon respawning.
    // It also makes the player invincible for 5 seconds.
    if (m_knockdownStart)
    {
        m_knockdownTimer -= acDeltaTime;
        if (m_knockdownTimer <= 0.0)
        {
            PlayerCharacter::SetGodMode(true);
            m_godmodeStart = true;
            m_godmodeTimer = 10.0;

            PlayerCharacter* pPlayer = PlayerCharacter::Get();
            if (auto* pCurrentProcess = pPlayer->GetCurrentProcessData())
            {
                const auto& position = pPlayer->GetPositionData();
                pCurrentProcess->KnockExplosion(pPlayer, &position, 0.f);
            }

            FadeOutGame(false, true, 0.5f, true, 2.f);

            m_knockdownStart = false;
        }
    }

    if (m_godmodeStart)
    {
        m_godmodeTimer -= acDeltaTime;
        if (m_godmodeTimer <= 0.0)
        {
            PlayerCharacter::SetGodMode(false);

            m_godmodeStart = false;
        }
    }
}

void PlayerService::RunDifficultyUpdates() const noexcept
{
    if (!m_transport.IsConnected())
        return;

    PlayerCharacter::Get()->SetDifficulty(m_serverDifficulty);
}

void PlayerService::RunLevelUpdates() noexcept
{
    // The LevelUp hook is kinda weird, so ehh, just check periodically, doesn't really cost anything.

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    static uint16_t oldLevel = PlayerCharacter::Get()->GetLevel();

    uint16_t newLevel = PlayerCharacter::Get()->GetLevel();
    if constexpr (kVrPlayerCellStatusEnabled)
    {
        if (m_cachedVrLevel == 0)
        {
            m_cachedVrLevel = newLevel;
            m_vrPlayerCellStatusDirty = true;
        }
    }

    if (newLevel != oldLevel)
    {
        PlayerLevelRequest request{};
        request.NewLevel = newLevel;

        m_transport.Send(request);

        if constexpr (kVrPlayerCellStatusEnabled)
        {
            m_cachedVrLevel = newLevel;
            m_lastVrLevelSent = newLevel;
            ++m_vrLevelRequestCount;
            m_vrPlayerCellStatusDirty = true;
        }

        oldLevel = newLevel;
    }
}

void PlayerService::RunVrLevelUpdates(const double acDeltaTime) noexcept
{
    TP_UNUSED(acDeltaTime);
    m_cachedVrLevel = kVrFallbackLevel;
}

void PlayerService::RunVrPlayerCellStatusWrite(const double acDeltaTime) noexcept
{
    m_vrPlayerCellStatusTimer += acDeltaTime;
    if (!m_vrPlayerCellStatusDirty && m_vrPlayerCellStatusTimer < kVrPlayerCellStatusWriteInterval)
        return;

    m_vrPlayerCellStatusTimer = 0.0;
    WriteVrPlayerCellStatusFile();
}

void PlayerService::WriteVrPlayerCellStatusFile() noexcept
{
    if constexpr (!kVrPlayerCellStatusEnabled)
        return;

    if (m_vrPlayerCellStatusPath.empty())
        return;

    std::error_code ec;
    std::filesystem::create_directories(m_vrPlayerCellStatusPath.parent_path(), ec);

    std::ofstream file(m_vrPlayerCellStatusPath, std::ios::trunc);
    if (!file)
        return;

    const auto* pPlayer = SkyrimTogetherVR::TryGetReadablePlayerForVR();
    const bool ready = pPlayer && pPlayer->GetBaseFormData() && pPlayer->GetParentCellData();

    file << "ready=" << (ready ? "1" : "0") << "\n";
    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "sessionId=" << m_transport.GetSessionId() << "\n";
    file << "connectionGeneration=" << m_transport.GetConnectionGeneration() << "\n";
    file << "playerFormId=" << (pPlayer ? pPlayer->GetFormIdData() : 0) << "\n";
    file << "currentLevel=" << m_cachedVrLevel << "\n";
    file << "cachedLevel=" << m_cachedVrLevel << "\n";
    file << "lastLevelSent=" << m_lastVrLevelSent << "\n";
    file << "gridCellRequestCount=" << m_vrGridCellRequestCount << "\n";
    file << "exteriorCellRequestCount=" << m_vrExteriorCellRequestCount << "\n";
    file << "interiorCellRequestCount=" << m_vrInteriorCellRequestCount << "\n";
    file << "levelRequestCount=" << m_vrLevelRequestCount << "\n";
    file << "offlineSkippedRequestCount=" << m_vrOfflineSkippedRequestCount << "\n";
    file << "worldSpaceTranslationFailureCount=" << m_vrWorldSpaceTranslationFailureCount << "\n";
    file << "lastGrid.valid=" << (m_hasVrGridCellRequest ? "1" : "0") << "\n";
    WriteGameId(file, "lastGrid.worldSpace", m_lastVrGridWorldSpace);
    WriteGameId(file, "lastGrid.playerCell", m_lastVrGridPlayerCell);
    WriteGridCoords(file, "lastGrid.center", m_lastVrGridCenterCoords);
    file << "lastGrid.cellCount=" << m_lastVrGridCellCount << "\n";
    file << "lastGrid.connectionGeneration=" << m_lastVrGridConnectionGeneration << "\n";
    file << "lastCell.valid=" << (m_hasVrCellRequest ? "1" : "0") << "\n";
    file << "lastCell.exterior=" << (m_lastVrCellWasExterior ? "1" : "0") << "\n";
    file << "lastCell.connectionGeneration=" << m_lastVrCellConnectionGeneration << "\n";
    WriteGameId(file, "lastCell.cell", m_lastVrCell);
    WriteGameId(file, "lastCell.worldSpace", m_lastVrCellWorldSpace);
    WriteGridCoords(file, "lastCell.currentCoords", m_lastVrCellCurrentCoords);

    m_vrPlayerCellStatusDirty = false;
}

void PlayerService::RunBeastFormDetection() const noexcept
{
    static uint32_t lastRaceFormID = 0;
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const auto* pRace = pPlayer->GetRaceData();
    if (!pRace)
        return;

    const uint32_t raceFormId = pRace->GetFormIdData();
    if (raceFormId == lastRaceFormID)
        return;

    if (raceFormId == 0x200283A || raceFormId == 0xCDD84)
        m_world.GetDispatcher().trigger(BeastFormChangeEvent());

    lastRaceFormID = raceFormId;
}

void PlayerService::ToggleDeathSystem(bool aSet) noexcept
{
    m_isDeathSystemEnabled = aSet;

    PlayerCharacter::Get()->SetPlayerRespawnMode(aSet);
}
