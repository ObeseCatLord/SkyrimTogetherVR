#pragma once

#include <filesystem>

#include <Structs/GameId.h>
#include <Structs/GridCellCoords.h>

struct World;
struct TransportService;

struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct ServerSettings;
struct GridCellChangeEvent;
struct CellChangeEvent;
struct PlayerDialogueEvent;
struct PlayerLevelEvent;
struct PartyJoinedEvent;
struct PartyLeftEvent;

struct NotifyPlayerRespawn;

/**
 * @brief Handles logic related to the local player.
 */
struct PlayerService
{
    PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~PlayerService() noexcept = default;

    TP_NOCOPYMOVE(PlayerService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnServerSettingsReceived(const ServerSettings& acSettings) noexcept;
    void OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) const noexcept;
    void OnGridCellChangeEvent(const GridCellChangeEvent& acEvent) noexcept;
    void OnCellChangeEvent(const CellChangeEvent& acEvent) noexcept;
    void OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) const noexcept;
    void OnPlayerLevelEvent(const PlayerLevelEvent& acEvent) noexcept;
    void OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept;
    void OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept;

private:
    /**
     * @brief Run the respawn timer, and if it hits 0, respawn the player.
     */
    void RunRespawnUpdates(const double acDeltaTime) noexcept;
    void RunPostDeathUpdates(const double acDeltaTime) noexcept;
    /**
     * @brief Make sure difficulty doesn't get changed while connected
     */
    void RunDifficultyUpdates() const noexcept;
    void RunLevelUpdates() noexcept;
    void RunVrLevelUpdates(const double acDeltaTime) noexcept;
    void RunVrPlayerCellStatusWrite(const double acDeltaTime) noexcept;
    void RunBeastFormDetection() const noexcept;
    void WriteVrPlayerCellStatusFile() noexcept;

    void ToggleDeathSystem(bool aSet) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;

    double m_respawnTimer = 0.0;
    int32_t m_serverDifficulty = 6;
    int32_t m_previousDifficulty = 6;

    bool m_isDeathSystemEnabled = true;

    bool m_knockdownStart = false;
    double m_knockdownTimer = 0.0;

    bool m_godmodeStart = false;
    double m_godmodeTimer = 0.0;
    double m_vrPlayerCellStatusTimer = 0.0;

    uint32_t m_cachedMainSpellId = 0;
    uint32_t m_cachedSecondarySpellId = 0;
    uint32_t m_cachedPowerId = 0;
    uint16_t m_cachedVrLevel = 1;
    uint16_t m_lastVrLevelSent = 0;
    uint32_t m_vrGridCellRequestCount = 0;
    uint32_t m_vrExteriorCellRequestCount = 0;
    uint32_t m_vrInteriorCellRequestCount = 0;
    uint32_t m_vrLevelRequestCount = 0;
    uint32_t m_vrOfflineSkippedRequestCount = 0;
    uint32_t m_vrWorldSpaceTranslationFailureCount = 0;
    uint32_t m_lastVrGridCellCount = 0;
    uint64_t m_lastVrGridConnectionGeneration = 0;
    uint64_t m_lastVrCellConnectionGeneration = 0;
    GameId m_lastVrGridWorldSpace{};
    GameId m_lastVrGridPlayerCell{};
    GameId m_lastVrCell{};
    GameId m_lastVrCellWorldSpace{};
    GridCellCoords m_lastVrGridCenterCoords{};
    GridCellCoords m_lastVrCellCurrentCoords{};
    std::filesystem::path m_vrPlayerCellStatusPath;
    bool m_hasVrGridCellRequest = false;
    bool m_hasVrCellRequest = false;
    bool m_lastVrCellWasExterior = false;
    bool m_vrPlayerCellStatusDirty = true;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_settingsConnection;
    entt::scoped_connection m_notifyRespawnConnection;
    entt::scoped_connection m_gridCellChangeConnection;
    entt::scoped_connection m_cellChangeConnection;
    entt::scoped_connection m_playerDialogueConnection;
    entt::scoped_connection m_playerLevelConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
};
