#pragma once

#include <filesystem>
#include <string>
#include <cstdint>

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
    void HandleLifecycleBoundary() noexcept;
    [[nodiscard]] const std::string& GetState() const noexcept { return m_state; }

private:
    enum class CommandAction
    {
        None,
        Connect,
        Disconnect,
        Chat,
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
        std::string Error;
        uint32_t PlayerId{};
        bool HasPlayerId{false};
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
    void RunCommand(const Command& acCommand) noexcept;
    void QueueConnect(const std::string& acEndpoint, const std::string& acPassword) noexcept;
    void QueueDisconnect() noexcept;
    void SendChat(const std::string& acMessage) noexcept;
    void RunPartyCommand(const Command& acCommand) noexcept;
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
    std::string m_lastCommandContents;
    std::string m_retainedEndpoint;
    std::string m_retainedPassword;
    Command m_pendingCommand{};
    uint64_t m_lastLifecycleEpoch{0};
    bool m_hasPendingCommand{false};
    bool m_connectInFlight{false};
    bool m_envAutoconnectQueued{false};
    bool m_commandQueuedThisUpdate{false};
    bool m_reportedMissingEnv{false};
    bool m_reportedWaitingForPlayer{false};
    bool m_statusDirty{true};
    double m_commandPollTimer{0.0};
    double m_statusTimer{0.0};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_connectionErrorConnection;
};
