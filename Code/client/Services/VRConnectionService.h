#pragma once

#include <filesystem>
#include <string>
#include <cstdint>

#include <Services/VRRehydrationState.h>
#include <TiltedCore/Platform.hpp>
#include <entt/entt.hpp>

struct ConnectedEvent;
struct ConnectionErrorEvent;
struct DisconnectedEvent;
struct TransportService;
struct UpdateEvent;
struct World;

struct VRConnectionService
{
    VRConnectionService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRConnectionService() noexcept = default;

    TP_NOCOPYMOVE(VRConnectionService);

    bool RequestConnect(const std::string& acEndpoint, const std::string& acPassword) noexcept;
    bool RequestDisconnect() noexcept;
    void BeginTeardown() noexcept;
    void HandleLifecycleBoundary() noexcept;
    [[nodiscard]] const std::string& GetState() const noexcept { return m_state; }
    [[nodiscard]] VRRehydrationState GetRehydrationState() const noexcept { return m_rehydrationState; }
    [[nodiscard]] bool IsReadyForGameplay() const noexcept { return m_rehydrationState == VRRehydrationState::Ready; }
    [[nodiscard]] const std::string& GetRehydrationFailure() const noexcept { return m_rehydrationFailure; }
    [[nodiscard]] VRRehydrationProfile GetRehydrationProfile() const noexcept { return GetBuildRehydrationProfile(); }
    void FailRehydration(const char* apReason) noexcept;

private:
    enum class CommandAction
    {
        None,
        Connect,
        Disconnect,
        Chat,
        SetTime,
        TeleportToPlayer,
        AdminTeleport,
        CreateParty,
        LeaveParty,
        InviteToParty,
        AcceptPartyInvite,
        KickPartyMember,
        ChangePartyLeader
    };

    struct Command
    {
        CommandAction Action{CommandAction::None};
        std::string Endpoint;
        std::string Password;
        std::string Message;
        std::string TargetPlayer;
        std::string Error;
        uint32_t PlayerId{};
        uint8_t Hours{};
        uint8_t Minutes{};
        bool HasPlayerId{false};
        bool HasHours{false};
        bool HasMinutes{false};
    };

    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnConnectionError(const ConnectionErrorEvent& acEvent) noexcept;

    void PollEnvironmentAutoconnect() noexcept;
    void PollCommandFile() noexcept;
    Command ParseCommandFile(const std::string& acContents) const noexcept;
    static bool IsPartyTargetAction(CommandAction aAction) noexcept;
    void TryRunPendingCommand() noexcept;
    [[nodiscard]] bool RunCommand(const Command& acCommand) noexcept;
    void QueueConnect(const std::string& acEndpoint, const std::string& acPassword) noexcept;
    void QueueDisconnect() noexcept;
    [[nodiscard]] bool SendChat(const std::string& acMessage) noexcept;
    [[nodiscard]] bool SendSetTimeCommand(const Command& acCommand) noexcept;
    [[nodiscard]] bool SendTeleportToPlayerCommand(const Command& acCommand) noexcept;
    [[nodiscard]] bool SendAdminTeleportCommand(const Command& acCommand) noexcept;
    [[nodiscard]] bool HasStableAuthenticatedTransport() const noexcept;
    [[nodiscard]] bool HasAuthenticatedTransportIdentity() const noexcept;
    [[nodiscard]] static constexpr VRRehydrationProfile GetBuildRehydrationProfile() noexcept
    {
#if TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC
#if TP_SKYRIM_VR_ENABLE_NATIVE_GAMEPLAY_PARITY
        return VRRehydrationProfile::Gameplay;
#else
        return VRRehydrationProfile::AvatarSync;
#endif
#else
        return VRRehydrationProfile::ConnectionOnly;
#endif
    }
    void InvalidateQueuedConnect() noexcept;
    [[nodiscard]] bool RunPartyCommand(const Command& acCommand) noexcept;
    void AdvanceRehydration(double aDelta) noexcept;
    void SetRehydrationState(VRRehydrationState aState) noexcept;
    void SetRehydrationStatus() noexcept;
    void ArchiveCommandFile(const char* apSuffix) noexcept;
    void SetStatus(std::string aState, std::string aError = {}) noexcept;
    void WriteStatusFile() noexcept;

    World& m_world;
    TransportService& m_transport;
    std::filesystem::path m_handoffDir;
    std::filesystem::path m_commandPath;
    std::filesystem::path m_statusPath;
    std::string m_state{"offline"};
    std::string m_lastError;
    std::string m_rehydrationFailure;
    std::string m_lastCommandContents;
    std::string m_retainedEndpoint;
    std::string m_retainedPassword;
    Command m_pendingCommand{};
    uint64_t m_lastLifecycleEpoch{0};
    uint64_t m_connectRequestToken{0};
    VRRehydrationState m_rehydrationState{VRRehydrationState::Offline};
    bool m_hasPendingCommand{false};
    bool m_connectInFlight{false};
    bool m_envAutoconnectQueued{false};
    bool m_commandQueuedThisUpdate{false};
    bool m_waitingForRetirementDisconnect{false};
    bool m_reportedMissingEnv{false};
    bool m_reportedWaitingForPlayer{false};
    bool m_statusDirty{true};
    double m_commandPollTimer{0.0};
    double m_statusTimer{0.0};
    double m_rehydrationStageElapsed{0.0};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_connectionErrorConnection;
};
