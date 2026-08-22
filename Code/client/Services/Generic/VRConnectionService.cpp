#include <TiltedOnlinePCH.h>

#include <Services/VRConnectionService.h>
#include <vr_common/VRHandoffPath.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <vector>

#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/SendChatMessageRequest.h>
#include <Messages/SetTimeCommandRequest.h>
#include <Messages/TeleportCommandRequest.h>
#include <Messages/TeleportRequest.h>
#include <Services/PartyService.h>
#include <Services/TransportService.h>
#include <Services/VRAvatarService.h>
#include <Services/VRLifecycleService.h>
#include <Structs/GameplayCapabilities.h>
#include <VRRuntimeDiagnostics.h>
#include <World.h>

namespace
{
constexpr double kCommandPollInterval = 0.5;
constexpr double kStatusWriteInterval = 1.0;
constexpr char kCommandFileName[] = "SkyrimTogetherVR.command";
constexpr char kStatusFileName[] = "SkyrimTogetherVR.status";
constexpr char kControlsFileName[] = "SkyrimTogetherVR.controls";
constexpr std::size_t kMaximumCommandBytes = 2 * 1024;
constexpr std::size_t kMaximumEndpointBytes = 255;
constexpr std::size_t kMaximumPasswordBytes = 256;
constexpr std::size_t kMaximumChatBytes = 512;
constexpr std::size_t kMaximumTeleportTargetBytes = 512;
constexpr std::size_t kMaximumStatusErrorBytes = 256;
constexpr uint32_t kMaximumTeleportPlayerId = 0xffff;
constexpr std::size_t kMaximumControlSnapshotEntries = 64;
constexpr std::size_t kMaximumControlPlayerNameBytes = 128;

bool IsVrPlayerReadyForConnection(World& aWorld) noexcept
{
    return aWorld.ctx().at<VRLifecycleService>().IsReady();
}

std::string Trim(std::string aValue)
{
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    aValue.erase(aValue.begin(), std::find_if_not(aValue.begin(), aValue.end(), isSpace));
    aValue.erase(std::find_if_not(aValue.rbegin(), aValue.rend(), isSpace).base(), aValue.end());
    return aValue;
}

std::string ToLower(std::string aValue)
{
    std::transform(aValue.begin(), aValue.end(), aValue.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return aValue;
}

std::filesystem::path GetHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
}

bool ReadTextFile(const std::filesystem::path& acPath, std::string& aOut)
{
    std::ifstream file(acPath);
    if (!file)
        return false;

    aOut.clear();
    std::array<char, 256> buffer{};
    while (file)
    {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = file.gcount();
        if (read <= 0)
            break;

        if (aOut.size() + static_cast<std::size_t>(read) > kMaximumCommandBytes)
            return false;

        aOut.append(buffer.data(), static_cast<std::size_t>(read));
    }

    return !file.bad();
}

bool HasControlCharacter(const std::string_view acValue) noexcept
{
    return std::any_of(acValue.begin(), acValue.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

bool IsValidEndpoint(const std::string_view acEndpoint) noexcept
{
    return !acEndpoint.empty() && acEndpoint.size() <= kMaximumEndpointBytes && !HasControlCharacter(acEndpoint) &&
           std::none_of(acEndpoint.begin(), acEndpoint.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

bool IsValidPassword(const std::string_view acPassword) noexcept
{
    return acPassword.size() <= kMaximumPasswordBytes && !HasControlCharacter(acPassword);
}

bool ParseUnsignedValue(const std::string_view acValue, uint32_t& aOut) noexcept
{
    if (acValue.empty() || acValue.size() > 10)
        return false;

    const auto* begin = acValue.data();
    const auto* end = begin + acValue.size();
    const auto [position, error] = std::from_chars(begin, end, aOut);
    return error == std::errc{} && position == end;
}

bool ParseUnsignedValue(const std::string_view acValue, uint64_t& aOut) noexcept
{
    if (acValue.empty() || acValue.size() > 20)
        return false;

    const auto* begin = acValue.data();
    const auto* end = begin + acValue.size();
    const auto [position, error] = std::from_chars(begin, end, aOut);
    return error == std::errc{} && position == end;
}

bool ParsePlayerId(const std::string_view acValue, uint32_t& aOut) noexcept
{
    return ParseUnsignedValue(acValue, aOut) && aOut != 0;
}

bool ParseTimeComponent(const std::string_view acValue, const uint32_t aMaximum, uint8_t& aOut) noexcept
{
    uint32_t value{};
    if (!ParseUnsignedValue(acValue, value) || value > aMaximum)
        return false;

    aOut = static_cast<uint8_t>(value);
    return true;
}

bool IsValidTeleportTarget(const std::string_view acTarget) noexcept
{
    return !acTarget.empty() && acTarget.size() <= kMaximumTeleportTargetBytes && !HasControlCharacter(acTarget);
}

template <class TString> std::string SnapshotText(const TString& acValue)
{
    std::string aValue(acValue.begin(), acValue.end());
    for (auto& character : aValue)
    {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x20 || value == 0x7f)
            character = ' ';
    }
    if (aValue.size() > kMaximumControlPlayerNameBytes)
        aValue.resize(kMaximumControlPlayerNameBytes);
    return aValue;
}

}

bool VRConnectionService::IsPartyTargetAction(const CommandAction aAction) noexcept
{
    return aAction == CommandAction::InviteToParty || aAction == CommandAction::AcceptPartyInvite ||
           aAction == CommandAction::DeclinePartyInvite || aAction == CommandAction::KickPartyMember ||
           aAction == CommandAction::ChangePartyLeader;
}

VRConnectionService::VRConnectionService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_commandPath(m_handoffDir / kCommandFileName)
    , m_statusPath(m_handoffDir / kStatusFileName)
    , m_controlsPath(m_handoffDir / kControlsFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR connection handoff command file: {}", m_commandPath.string());

    m_lastLifecycleEpoch = m_world.ctx().at<VRLifecycleService>().GetEpoch();

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRConnectionService::OnUpdate>(this);
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&VRConnectionService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRConnectionService::OnDisconnected>(this);
    m_connectionErrorConnection = aDispatcher.sink<ConnectionErrorEvent>().connect<&VRConnectionService::OnConnectionError>(this);
}

void VRConnectionService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    m_commandQueuedThisUpdate = false;

    // Resolve lifecycle retirement before accepting queued commands. A load can
    // become ready between frames while retaining a reconnect command.
    AdvanceRehydration(acEvent.Delta);

    m_commandPollTimer += acEvent.Delta;
    if (m_commandPollTimer >= kCommandPollInterval)
    {
        m_commandPollTimer = 0.0;
        PollCommandFile();
    }

    if (!m_commandQueuedThisUpdate)
    {
        TryRunPendingCommand();
    }

    if (!m_commandQueuedThisUpdate)
    {
        PollEnvironmentAutoconnect();
    }

    m_statusTimer += acEvent.Delta;
    if (m_statusDirty || m_statusTimer >= kStatusWriteInterval)
    {
        m_statusTimer = 0.0;
        WriteStatusFile();
        WriteControlsSnapshot();
    }
}

void VRConnectionService::OnConnected(const ConnectedEvent&) noexcept
{
    SkyrimTogetherVR::LogRuntimeCheckpoint("connected.connection_service.begin");
    if (!IsVrPlayerReadyForConnection(m_world) || m_waitingForRetirementDisconnect)
    {
        spdlog::warn("SkyrimTogetherVR connection completed before the current lifecycle epoch was stable; closing it");
        m_transport.Close();
        SkyrimTogetherVR::LogRuntimeCheckpoint("connected.connection_service.closed_not_ready");
        return;
    }

    m_connectInFlight = false;
    SetRehydrationState(VRRehydrationState::Authenticated);
    SetRehydrationStatus();
    SkyrimTogetherVR::LogRuntimeCheckpoint("connected.connection_service.done");
}

void VRConnectionService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_connectInFlight = false;
    if (IsVRRehydrationTerminal(m_rehydrationState))
    {
        SetRehydrationStatus();
    }
    else if (m_waitingForRetirementDisconnect)
    {
        m_waitingForRetirementDisconnect = false;
        SetRehydrationState(VRRehydrationState::Stable);
        SetRehydrationStatus();
    }
    else
    {
        SetRehydrationState(VRRehydrationState::Offline);
        SetStatus(m_hasPendingCommand ? "waiting_for_gameplay" : "offline");
    }
    WriteStatusFile();
}

void VRConnectionService::OnConnectionError(const ConnectionErrorEvent& acEvent) noexcept
{
    m_connectInFlight = false;
    if (m_waitingForRetirementDisconnect)
        return;
    FailRehydration(acEvent.ErrorDetail.empty() ? "connection error" : acEvent.ErrorDetail.c_str());
}

bool VRConnectionService::RequestConnect(const std::string& acEndpoint, const std::string& acPassword) noexcept
{
    const auto endpoint = Trim(acEndpoint);
    if (!IsValidEndpoint(endpoint) || !IsValidPassword(acPassword))
    {
        SetStatus("error", "connect command has an invalid endpoint or password");
        return false;
    }

    if (m_transport.IsOnline())
    {
        SetRehydrationStatus();
        return false;
    }
    if (IsVRRehydrationTerminal(m_rehydrationState))
    {
        m_rehydrationFailure.clear();
        SetRehydrationState(VRRehydrationState::Stable);
    }
    if (m_connectInFlight)
    {
        spdlog::info("SkyrimTogetherVR connection request ignored because a connection is already in flight");
        SetStatus("connecting");
        return false;
    }

    Command command{};
    command.Action = CommandAction::Connect;
    command.Endpoint = endpoint;
    command.Password = acPassword;

    if (!IsVrPlayerReadyForConnection(m_world))
    {
        m_pendingCommand = std::move(command);
        m_hasPendingCommand = true;
        SetStatus("waiting_for_gameplay");
        spdlog::info("SkyrimTogetherVR connection request queued until a stable gameplay lifecycle epoch is ready");
        m_commandQueuedThisUpdate = true;
        return true;
    }

    RunCommand(command);
    m_commandQueuedThisUpdate = true;
    return true;
}

bool VRConnectionService::RequestDisconnect() noexcept
{
    QueueDisconnect();
    m_commandQueuedThisUpdate = true;
    return true;
}

void VRConnectionService::BeginTeardown() noexcept
{
    InvalidateQueuedConnect();
    m_hasPendingCommand = false;
    m_pendingCommand = {};
    m_retainedEndpoint.clear();
    m_retainedPassword.clear();
    m_connectInFlight = false;
    m_waitingForRetirementDisconnect = false;
    m_rehydrationFailure.clear();
    SetRehydrationState(VRRehydrationState::Offline);
    SetStatus("offline");
    WriteStatusFile();
}

void VRConnectionService::HandleLifecycleBoundary() noexcept
{
    auto& lifecycle = m_world.ctx().at<VRLifecycleService>();
    const auto epoch = lifecycle.GetEpoch();
    if (epoch == m_lastLifecycleEpoch)
        return;

    m_lastLifecycleEpoch = epoch;
    m_statusDirty = true;
    if (IsVRRehydrationTerminal(m_rehydrationState))
    {
        SetRehydrationStatus();
        return;
    }
    SetRehydrationState(VRRehydrationState::Retiring);

    const bool authenticatedTransport = m_transport.IsOnline();
    const bool unauthenticatedConnectInFlight = !authenticatedTransport && m_connectInFlight;
    if (authenticatedTransport || unauthenticatedConnectInFlight)
    {
        if (!m_retainedEndpoint.empty())
        {
            m_pendingCommand.Action = CommandAction::Connect;
            m_pendingCommand.Endpoint = m_retainedEndpoint;
            m_pendingCommand.Password = m_retainedPassword;
            m_hasPendingCommand = true;
        }
        InvalidateQueuedConnect();
        m_connectInFlight = false;
        m_waitingForRetirementDisconnect = authenticatedTransport;
        SetRehydrationStatus();
        spdlog::info(
            "SkyrimTogetherVR lifecycle epoch {} invalidated a {} connection; reconnectRetained={}", epoch,
            authenticatedTransport ? "authenticated" : "pre-authentication", m_hasPendingCommand);
        m_transport.Close();

        // Client::Close cancels DNS resolution, but the resolver may never
        // dispatch a Disconnect callback while World::Update is suspended.
        // An unauthenticated attempt owns no gameplay session, so retirement
        // completes now.  Authenticated sessions retain close-before-reconnect.
        if (!authenticatedTransport)
        {
            SetRehydrationState(lifecycle.IsReady() ? VRRehydrationState::Stable : VRRehydrationState::Retiring);
            SetRehydrationStatus();
        }
    }
    else if (lifecycle.IsReady())
    {
        SetRehydrationState(VRRehydrationState::Stable);
        SetRehydrationStatus();
    }
    else
    {
        SetRehydrationStatus();
    }
}

void VRConnectionService::PollEnvironmentAutoconnect() noexcept
{
    if (m_envAutoconnectQueued)
        return;

    const char* pEndpoint = std::getenv("STVR_AUTOCONNECT");
    if (!pEndpoint || !pEndpoint[0])
    {
        if (!m_reportedMissingEnv)
        {
            spdlog::info("SkyrimTogetherVR connection handoff: set STVR_AUTOCONNECT=host:port or write {}", m_commandPath.string());
            m_reportedMissingEnv = true;
        }
        return;
    }

    if (!IsVrPlayerReadyForConnection(m_world))
    {
        if (!m_reportedWaitingForPlayer)
        {
            spdlog::info("SkyrimTogetherVR connection handoff: waiting for a stable gameplay lifecycle epoch before autoconnect");
            m_reportedWaitingForPlayer = true;
        }
        return;
    }

    const char* pPassword = std::getenv("STVR_PASSWORD");
    RequestConnect(pEndpoint, pPassword ? pPassword : "");
    m_envAutoconnectQueued = true;
}

void VRConnectionService::PollCommandFile() noexcept
{
    std::error_code ec;
    if (!std::filesystem::exists(m_commandPath, ec))
        return;

    std::string contents;
    if (!ReadTextFile(m_commandPath, contents))
    {
        spdlog::warn("SkyrimTogetherVR connection handoff rejected because its command file is unavailable or exceeds {} bytes", kMaximumCommandBytes);
        m_commandQueuedThisUpdate = true;
        SetStatus("error", "command file is unavailable or too large");
        ArchiveCommandFile(".error");
        return;
    }

    if (contents == m_lastCommandContents)
        return;

    m_lastCommandContents = contents;

    Command command = ParseCommandFile(contents);
    if (!command.Error.empty())
    {
        m_commandQueuedThisUpdate = true;
        spdlog::warn("SkyrimTogetherVR connection handoff rejected: {}", command.Error);
        SetStatus("error", command.Error);
        ArchiveCommandFile(".error");
        return;
    }

    if (command.Action == CommandAction::Connect && !IsVrPlayerReadyForConnection(m_world))
    {
        m_pendingCommand = std::move(command);
        m_hasPendingCommand = true;
        m_commandQueuedThisUpdate = true;
        SetStatus("waiting_for_gameplay");
        spdlog::info("SkyrimTogetherVR connection handoff queued until a stable gameplay lifecycle epoch is ready");
        return;
    }

    if (RunCommand(command))
        ArchiveCommandFile(".sent");
    else
        ArchiveCommandFile(".error");
}

VRConnectionService::Command VRConnectionService::ParseCommandFile(const std::string& acContents) const noexcept
{
    Command command{};

    std::istringstream stream(acContents);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        line = Trim(std::move(line));
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        const auto lowerLine = ToLower(line);
        if (lowerLine == "disconnect")
        {
            command.Action = CommandAction::Disconnect;
            continue;
        }

        if (lowerLine.rfind("connect ", 0) == 0)
        {
            command.Action = CommandAction::Connect;
            command.Endpoint = Trim(line.substr(8));
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos)
        {
            if (command.Endpoint.empty() && line.find(':') != std::string::npos)
            {
                command.Action = CommandAction::Connect;
                command.Endpoint = line;
            }
            continue;
        }

        const auto key = ToLower(Trim(line.substr(0, equals)));
        const auto value = Trim(line.substr(equals + 1));

        if (key == "action" || key == "command")
        {
            const auto lowerValue = ToLower(value);
            if (lowerValue == "connect")
                command.Action = CommandAction::Connect;
            else if (lowerValue == "disconnect")
                command.Action = CommandAction::Disconnect;
            else if (lowerValue == "chat")
                command.Action = CommandAction::Chat;
            else if (lowerValue == "set_time")
                command.Action = CommandAction::SetTime;
            else if (lowerValue == "teleport_to_player")
                command.Action = CommandAction::TeleportToPlayer;
            else if (lowerValue == "admin_teleport")
                command.Action = CommandAction::AdminTeleport;
            else if (lowerValue == "party_create")
                command.Action = CommandAction::CreateParty;
            else if (lowerValue == "party_leave")
                command.Action = CommandAction::LeaveParty;
            else if (lowerValue == "party_invite")
                command.Action = CommandAction::InviteToParty;
            else if (lowerValue == "party_accept")
                command.Action = CommandAction::AcceptPartyInvite;
            else if (lowerValue == "party_decline")
                command.Action = CommandAction::DeclinePartyInvite;
            else if (lowerValue == "party_kick")
                command.Action = CommandAction::KickPartyMember;
            else if (lowerValue == "party_change_leader")
                command.Action = CommandAction::ChangePartyLeader;
            else
                command.Error = "command file contains an unsupported action";
        }
        else if (key == "endpoint" || key == "address" || key == "server")
        {
            command.Endpoint = value;
        }
        else if (key == "password")
        {
            command.Password = value;
        }
        else if (key == "envelope")
        {
            if (command.HasEnvelope)
                command.Error = "command contains duplicate envelope identity fields";
            command.HasEnvelope = true;
            if (value == "online")
                command.Envelope = CommandEnvelope::Online;
            else if (value == "launch_bound_connect")
                command.Envelope = CommandEnvelope::LaunchBoundConnect;
            else
                command.Error = "command contains an unsupported envelope";
        }
        else if (key == "launchnonce")
        {
            std::string commandNonce;
            if (command.HasLaunchNonce || !SkyrimTogetherVR::Handoff::NormalizeLaunchNonce(value, commandNonce))
                command.Error = "command contains an invalid or duplicate launch nonce";
            else
            {
                command.HasLaunchNonce = true;
                command.LaunchNonce = std::move(commandNonce);
            }
        }
        else if (key == "lifecycleepoch")
        {
            if (command.HasLifecycleEpoch || !ParseUnsignedValue(value, command.LifecycleEpoch))
                command.Error = "command contains an invalid or duplicate lifecycle epoch";
            else
                command.HasLifecycleEpoch = true;
        }
        else if (key == "connectiongeneration")
        {
            if (command.HasConnectionGeneration || !ParseUnsignedValue(value, command.ConnectionGeneration))
                command.Error = "command contains an invalid or duplicate connection generation";
            else
                command.HasConnectionGeneration = true;
        }
        else if (key == "sessionid")
        {
            if (command.HasSessionId || !ParseUnsignedValue(value, command.SessionId))
                command.Error = "command contains an invalid or duplicate session id";
            else
                command.HasSessionId = true;
        }
        else if (key == "serverinstancenonce")
        {
            if (command.HasServerInstanceNonce || !ParseUnsignedValue(value, command.ServerInstanceNonce))
                command.Error = "command contains an invalid or duplicate server instance nonce";
            else
                command.HasServerInstanceNonce = true;
        }
        else if (key == "message" || key == "text")
        {
            command.Message = value;
        }
        else if (key == "hours")
        {
            command.HasHours = ParseTimeComponent(value, 23, command.Hours);
            if (!command.HasHours)
                command.Error = "set-time command has invalid hours";
        }
        else if (key == "minutes")
        {
            command.HasMinutes = ParseTimeComponent(value, 59, command.Minutes);
            if (!command.HasMinutes)
                command.Error = "set-time command has invalid minutes";
        }
        else if (key == "playerid" || key == "player_id" || key == "inviterid" || key == "inviter_id")
        {
            command.HasPlayerId = ParsePlayerId(value, command.PlayerId);
            if (!command.HasPlayerId)
                command.Error = "command contains an invalid player id";
        }
        else if (key == "targetplayer" || key == "target_player")
        {
            command.TargetPlayer = value;
        }
    }

    if (!command.Error.empty())
        return command;

    if (command.Action == CommandAction::None && !command.Endpoint.empty())
        command.Action = CommandAction::Connect;

    if (command.Action == CommandAction::Connect && (!IsValidEndpoint(command.Endpoint) || !IsValidPassword(command.Password)))
        command.Error = "connect command has an invalid endpoint or password";
    else if (command.Action == CommandAction::Chat &&
             (command.Message.empty() || command.Message.size() > kMaximumChatBytes || HasControlCharacter(command.Message)))
        command.Error = "chat message is empty or too long";
    else if (command.Action == CommandAction::SetTime && (!command.HasHours || !command.HasMinutes))
        command.Error = "set-time command requires hours from 0 to 23 and minutes from 0 to 59";
    else if (command.Action == CommandAction::TeleportToPlayer &&
             (!command.HasPlayerId || command.PlayerId > kMaximumTeleportPlayerId))
        command.Error = "teleport-to-player command requires a player id from 1 to 65535";
    else if (command.Action == CommandAction::AdminTeleport && !IsValidTeleportTarget(command.TargetPlayer))
        command.Error = "admin-teleport command requires a valid target player name";
    else if (IsPartyTargetAction(command.Action) && !command.HasPlayerId)
        command.Error = "party command is missing player id";
    else if (command.Action == CommandAction::None)
        command.Error = "command file did not contain a supported action";

    if (command.Error.empty() && (!command.HasEnvelope || !command.HasLaunchNonce || !command.HasLifecycleEpoch ||
                                  !command.HasConnectionGeneration || !command.HasSessionId || !command.HasServerInstanceNonce))
        command.Error = "command is missing its required identity envelope";
    else if (command.Error.empty() && command.Envelope == CommandEnvelope::Online && command.Action == CommandAction::Connect)
        command.Error = "connect commands require the launch-bound connect envelope";
    else if (command.Error.empty() && command.Envelope == CommandEnvelope::LaunchBoundConnect && command.Action != CommandAction::Connect)
        command.Error = "online commands require the authenticated identity envelope";

    return command;
}

void VRConnectionService::TryRunPendingCommand() noexcept
{
    if (!m_hasPendingCommand)
        return;

    if (m_waitingForRetirementDisconnect || IsVRRehydrationTerminal(m_rehydrationState))
        return;

    if (m_pendingCommand.Action == CommandAction::Connect && !IsVrPlayerReadyForConnection(m_world))
        return;

    const bool sent = RunCommand(m_pendingCommand);
    m_hasPendingCommand = false;
    m_pendingCommand = {};
    ArchiveCommandFile(sent ? ".sent" : ".error");
}

bool VRConnectionService::RunCommand(const Command& acCommand) noexcept
{
    m_commandQueuedThisUpdate = true;

    if (acCommand.Envelope == CommandEnvelope::Online && !HasCurrentOnlineCommandIdentity(acCommand))
    {
        SetStatus(m_transport.IsOnline() ? "error" : "offline", "command identity is stale or no longer authenticated");
        spdlog::warn("SkyrimTogetherVR rejected a stale online command identity");
        return false;
    }
    if (acCommand.Envelope == CommandEnvelope::LaunchBoundConnect && !HasCurrentLaunchBoundConnectIdentity(acCommand))
    {
        SetStatus("error", "connect command launch identity is stale or malformed");
        spdlog::warn("SkyrimTogetherVR rejected a stale launch-bound connect command");
        return false;
    }

    switch (acCommand.Action)
    {
    case CommandAction::Connect:
        QueueConnect(acCommand.Endpoint, acCommand.Password);
        return true;
    case CommandAction::Disconnect:
        QueueDisconnect();
        return true;
    case CommandAction::Chat: return SendChat(acCommand);
    case CommandAction::SetTime: return SendSetTimeCommand(acCommand);
    case CommandAction::TeleportToPlayer: return SendTeleportToPlayerCommand(acCommand);
    case CommandAction::AdminTeleport: return SendAdminTeleportCommand(acCommand);
    case CommandAction::CreateParty:
    case CommandAction::LeaveParty:
    case CommandAction::InviteToParty:
    case CommandAction::AcceptPartyInvite:
    case CommandAction::DeclinePartyInvite:
    case CommandAction::KickPartyMember:
    case CommandAction::ChangePartyLeader: return RunPartyCommand(acCommand);
    default: return false;
    }
}

void VRConnectionService::QueueConnect(const std::string& acEndpoint, const std::string& acPassword) noexcept
{
    if (m_transport.IsOnline())
    {
        spdlog::warn("SkyrimTogetherVR connection request ignored because the client already has an authenticated socket");
        m_connectInFlight = false;
        SetRehydrationStatus();
        return;
    }
    if (m_connectInFlight)
    {
        spdlog::info("SkyrimTogetherVR connection request ignored because a connection is already in flight");
        SetStatus("connecting");
        return;
    }

    if (m_rehydrationState == VRRehydrationState::Offline ||
        (m_rehydrationState == VRRehydrationState::Retiring && !m_waitingForRetirementDisconnect &&
         IsVrPlayerReadyForConnection(m_world)))
        SetRehydrationState(VRRehydrationState::Stable);

    spdlog::info("SkyrimTogetherVR queueing connection to {}", acEndpoint);
    m_retainedEndpoint = acEndpoint;
    m_retainedPassword = acPassword;
    m_connectInFlight = true;
    SetRehydrationState(VRRehydrationState::Connecting);
    SetStatus("connecting");

    const auto lifecycleEpoch = m_world.ctx().at<VRLifecycleService>().GetEpoch();
    auto connectToken = ++m_connectRequestToken;
    if (connectToken == 0)
        connectToken = ++m_connectRequestToken;
    m_world.GetRunner().Queue([this, endpoint = acEndpoint, password = acPassword, lifecycleEpoch, connectToken]() {
        if (connectToken != m_connectRequestToken)
        {
            spdlog::info("SkyrimTogetherVR discarded a superseded queued connect token {}", connectToken);
            return;
        }

        auto& world = World::Get();
        const auto& lifecycle = world.ctx().at<VRLifecycleService>();
        if (!lifecycle.IsReady() || lifecycle.GetEpoch() != lifecycleEpoch)
        {
            m_connectInFlight = false;
            if (!m_retainedEndpoint.empty())
            {
                m_pendingCommand.Action = CommandAction::Connect;
                m_pendingCommand.Endpoint = m_retainedEndpoint;
                m_pendingCommand.Password = m_retainedPassword;
                m_hasPendingCommand = true;
            }
            SetRehydrationState(lifecycle.IsReady() ? VRRehydrationState::Stable : VRRehydrationState::Retiring);
            SetRehydrationStatus();
            spdlog::info(
                "SkyrimTogetherVR discarded a stale queued connect from lifecycle epoch {}; current epoch={} state={}",
                lifecycleEpoch, lifecycle.GetEpoch(), lifecycle.GetStateName());
            return;
        }

        if (!password.empty())
            world.GetTransport().SetServerPassword(password);

        world.GetTransport().Connect(endpoint);
    });
}

void VRConnectionService::QueueDisconnect() noexcept
{
    spdlog::info("SkyrimTogetherVR queueing disconnect");
    InvalidateQueuedConnect();
    m_hasPendingCommand = false;
    m_pendingCommand = {};
    m_retainedEndpoint.clear();
    m_retainedPassword.clear();
    m_connectInFlight = false;
    m_waitingForRetirementDisconnect = false;
    SetStatus("disconnecting");
    m_world.GetRunner().Queue([]() { World::Get().GetTransport().Close(); });
}

bool VRConnectionService::SendChat(const Command& acCommand) noexcept
{
    if (!HasStableAuthenticatedTransport() || acCommand.Message.empty() || acCommand.Message.size() > kMaximumChatBytes ||
        HasControlCharacter(acCommand.Message))
    {
        SetStatus(m_transport.IsOnline() ? "error" : "offline", "chat command requires an online connection and a valid message");
        spdlog::warn("SkyrimTogetherVR chat command rejected because the client is offline or the message is invalid");
        return false;
    }
    const auto lifecycleEpoch = m_world.ctx().at<VRLifecycleService>().GetEpoch();
    const auto localPlayerId = m_transport.GetLocalPlayerId();
    const auto connectionGeneration = m_transport.GetConnectionGeneration();
    const auto sessionId = m_transport.GetSessionId();
    const auto serverInstanceNonce = m_transport.GetServerInstanceNonce();
    SendChatMessageRequest request{};
    request.MessageType = ChatMessageType::kGlobalChat;
    request.ChatMessage = acCommand.Message;
    if (!HasCurrentOnlineCommandIdentity(acCommand) || !IsVrPlayerReadyForConnection(m_world) ||
        m_world.ctx().at<VRLifecycleService>().GetEpoch() != lifecycleEpoch || m_transport.GetLocalPlayerId() != localPlayerId ||
        m_transport.GetConnectionGeneration() != connectionGeneration || m_transport.GetSessionId() != sessionId ||
        m_transport.GetServerInstanceNonce() != serverInstanceNonce || !m_transport.Send(request))
    {
        SetStatus("error", "chat command was not accepted by the transport");
        spdlog::warn("SkyrimTogetherVR chat command was not accepted by the transport");
        return false;
    }

    SetRehydrationStatus();
    return true;
}

bool VRConnectionService::HasStableAuthenticatedTransport() const noexcept
{
    return IsReadyForGameplay() && HasAuthenticatedTransportIdentity();
}

bool VRConnectionService::HasAuthenticatedTransportIdentity() const noexcept
{
    return IsVrPlayerReadyForConnection(m_world) && m_transport.IsOnline() &&
           m_transport.GetLocalPlayerId() != 0 &&
           m_transport.GetSessionId() != 0 && m_transport.GetConnectionGeneration() != 0 &&
           m_transport.GetServerInstanceNonce() != 0;
}

bool VRConnectionService::HasCurrentOnlineCommandIdentity(const Command& acCommand) const noexcept
{
    const auto launchNonce = SkyrimTogetherVR::Handoff::GetLaunchNonce();
    return acCommand.HasEnvelope && acCommand.Envelope == CommandEnvelope::Online && acCommand.HasLaunchNonce &&
           acCommand.HasLifecycleEpoch && acCommand.HasConnectionGeneration && acCommand.HasSessionId && acCommand.HasServerInstanceNonce &&
           HasStableAuthenticatedTransport() && !launchNonce.empty() && acCommand.LaunchNonce == launchNonce &&
           acCommand.LifecycleEpoch == m_world.ctx().at<VRLifecycleService>().GetEpoch() &&
           acCommand.ConnectionGeneration == m_transport.GetConnectionGeneration() && acCommand.SessionId == m_transport.GetSessionId() &&
           acCommand.ServerInstanceNonce == m_transport.GetServerInstanceNonce();
}

bool VRConnectionService::HasCurrentLaunchBoundConnectIdentity(const Command& acCommand) const noexcept
{
    const auto launchNonce = SkyrimTogetherVR::Handoff::GetLaunchNonce();
    return acCommand.HasEnvelope && acCommand.Envelope == CommandEnvelope::LaunchBoundConnect && acCommand.HasLaunchNonce &&
           acCommand.HasLifecycleEpoch && acCommand.HasConnectionGeneration && acCommand.HasSessionId && acCommand.HasServerInstanceNonce &&
           !launchNonce.empty() && acCommand.LaunchNonce == launchNonce && acCommand.LifecycleEpoch == 0 &&
           acCommand.ConnectionGeneration == 0 && acCommand.SessionId == 0 && acCommand.ServerInstanceNonce == 0;
}

bool VRConnectionService::IsCurrentPartyMember(const uint32_t aPlayerId) const noexcept
{
    const auto& members = m_world.GetPartyService().GetPartyMembers();
    return aPlayerId != 0 && aPlayerId != m_transport.GetLocalPlayerId() &&
           std::find(members.begin(), members.end(), aPlayerId) != members.end();
}

void VRConnectionService::InvalidateQueuedConnect() noexcept
{
    ++m_connectRequestToken;
    if (m_connectRequestToken == 0)
        ++m_connectRequestToken;
}

bool VRConnectionService::SendSetTimeCommand(const Command& acCommand) noexcept
{
    if (!HasStableAuthenticatedTransport())
    {
        SetStatus(m_transport.IsOnline() ? "waiting_for_gameplay" : "offline",
                  "set-time command requires an authenticated transport and stable local player id");
        spdlog::warn("SkyrimTogetherVR set-time command rejected because the authenticated transport identity is not stable");
        return false;
    }

    const auto lifecycleEpoch = m_world.ctx().at<VRLifecycleService>().GetEpoch();
    const auto senderId = m_transport.GetLocalPlayerId();
    const auto connectionGeneration = m_transport.GetConnectionGeneration();
    const auto sessionId = m_transport.GetSessionId();
    const auto serverInstanceNonce = m_transport.GetServerInstanceNonce();
    SetTimeCommandRequest request{};
    request.Hours = acCommand.Hours;
    request.Minutes = acCommand.Minutes;
    request.PlayerId = senderId;

    if (!HasCurrentOnlineCommandIdentity(acCommand) || !IsVrPlayerReadyForConnection(m_world) ||
        m_world.ctx().at<VRLifecycleService>().GetEpoch() != lifecycleEpoch || m_transport.GetLocalPlayerId() != senderId ||
        m_transport.GetConnectionGeneration() != connectionGeneration || m_transport.GetSessionId() != sessionId ||
        m_transport.GetServerInstanceNonce() != serverInstanceNonce || !m_transport.Send(request))
    {
        SetStatus("error", "set-time command was not accepted by the transport");
        spdlog::warn("SkyrimTogetherVR set-time command was not accepted by the transport");
        return false;
    }

    SetRehydrationStatus();
    return true;
}

bool VRConnectionService::SendTeleportToPlayerCommand(const Command& acCommand) noexcept
{
    if (!HasStableAuthenticatedTransport())
    {
        SetStatus(m_transport.IsOnline() ? "waiting_for_gameplay" : "offline",
                  "teleport-to-player command requires an authenticated transport and stable local player id");
        spdlog::warn("SkyrimTogetherVR teleport-to-player command rejected because the authenticated transport identity is not stable");
        return false;
    }

    const auto lifecycleEpoch = m_world.ctx().at<VRLifecycleService>().GetEpoch();
    const auto senderId = m_transport.GetLocalPlayerId();
    const auto connectionGeneration = m_transport.GetConnectionGeneration();
    const auto sessionId = m_transport.GetSessionId();
    const auto serverInstanceNonce = m_transport.GetServerInstanceNonce();
    TeleportRequest request{};
    request.PlayerId = static_cast<uint16_t>(acCommand.PlayerId);

    if (!IsCurrentPartyMember(acCommand.PlayerId) || !HasCurrentOnlineCommandIdentity(acCommand) || !IsVrPlayerReadyForConnection(m_world) ||
        m_world.ctx().at<VRLifecycleService>().GetEpoch() != lifecycleEpoch || m_transport.GetLocalPlayerId() != senderId ||
        m_transport.GetConnectionGeneration() != connectionGeneration || m_transport.GetSessionId() != sessionId ||
        m_transport.GetServerInstanceNonce() != serverInstanceNonce || !m_transport.Send(request))
    {
        SetStatus("error", "teleport-to-player command was not accepted by the transport");
        spdlog::warn("SkyrimTogetherVR teleport-to-player command was not accepted by the transport");
        return false;
    }

    SetRehydrationStatus();
    return true;
}

bool VRConnectionService::SendAdminTeleportCommand(const Command& acCommand) noexcept
{
    if (!HasStableAuthenticatedTransport())
    {
        SetStatus(m_transport.IsOnline() ? "waiting_for_gameplay" : "offline",
                  "admin-teleport command requires an authenticated transport and stable local player id");
        spdlog::warn("SkyrimTogetherVR admin-teleport command rejected because the authenticated transport identity is not stable");
        return false;
    }

    const auto lifecycleEpoch = m_world.ctx().at<VRLifecycleService>().GetEpoch();
    const auto senderId = m_transport.GetLocalPlayerId();
    const auto connectionGeneration = m_transport.GetConnectionGeneration();
    const auto sessionId = m_transport.GetSessionId();
    const auto serverInstanceNonce = m_transport.GetServerInstanceNonce();
    TeleportCommandRequest request{};
    request.TargetPlayer = acCommand.TargetPlayer;

    if (!HasCurrentOnlineCommandIdentity(acCommand) || !IsVrPlayerReadyForConnection(m_world) ||
        m_world.ctx().at<VRLifecycleService>().GetEpoch() != lifecycleEpoch || m_transport.GetLocalPlayerId() != senderId ||
        m_transport.GetConnectionGeneration() != connectionGeneration || m_transport.GetSessionId() != sessionId ||
        m_transport.GetServerInstanceNonce() != serverInstanceNonce || !m_transport.Send(request))
    {
        SetStatus("error", "admin-teleport command was not accepted by the transport");
        spdlog::warn("SkyrimTogetherVR admin-teleport command was not accepted by the transport");
        return false;
    }

    SetRehydrationStatus();
    return true;
}

bool VRConnectionService::RunPartyCommand(const Command& acCommand) noexcept
{
    if (!HasCurrentOnlineCommandIdentity(acCommand))
    {
        SetStatus("offline", "party command requires an online connection");
        spdlog::warn("SkyrimTogetherVR party command rejected because the client is offline");
        return false;
    }

    auto& party = m_world.GetPartyService();
    bool accepted = false;
    switch (acCommand.Action)
    {
    case CommandAction::CreateParty: accepted = party.CreateParty(); break;
    case CommandAction::LeaveParty: accepted = party.LeaveParty(); break;
    case CommandAction::InviteToParty: accepted = party.CreateInvite(acCommand.PlayerId); break;
    case CommandAction::AcceptPartyInvite: accepted = party.AcceptInvite(acCommand.PlayerId); break;
    case CommandAction::DeclinePartyInvite: accepted = party.DeclineInvite(acCommand.PlayerId); break;
    case CommandAction::KickPartyMember: accepted = party.KickPartyMember(acCommand.PlayerId); break;
    case CommandAction::ChangePartyLeader: accepted = party.ChangePartyLeader(acCommand.PlayerId); break;
    default: break;
    }

    if (!accepted)
    {
        SetStatus("error", "party command is not valid for the current party state");
        spdlog::debug("SkyrimTogetherVR rejected a party command for the current party state");
    }
    return accepted;
}

void VRConnectionService::AdvanceRehydration(const double aDelta) noexcept
{
    if (IsVRRehydrationTerminal(m_rehydrationState))
        return;

    const auto& lifecycle = m_world.ctx().at<VRLifecycleService>();
    if (!lifecycle.IsReady())
    {
        SetRehydrationState(VRRehydrationState::Retiring);
        SetRehydrationStatus();
        return;
    }

    if (m_rehydrationState == VRRehydrationState::Retiring && !m_waitingForRetirementDisconnect)
    {
        SetRehydrationState(VRRehydrationState::Stable);
        SetRehydrationStatus();
    }
    else if (m_rehydrationState == VRRehydrationState::Retiring)
    {
        m_rehydrationStageElapsed += std::clamp(aDelta, 0.0, 1.0);
        if (m_rehydrationStageElapsed >= VRRehydrationDeadlineSeconds(VRRehydrationState::Retiring))
            FailRehydration("the prior lifecycle connection did not close before its retirement deadline");
        return;
    }

    if (m_rehydrationState == VRRehydrationState::Stable)
    {
        if (m_connectInFlight)
        {
            SetRehydrationState(VRRehydrationState::Connecting);
            SetRehydrationStatus();
        }
        return;
    }

    if (m_rehydrationState == VRRehydrationState::Offline)
        return;

    if (!HasAuthenticatedTransportIdentity())
    {
        if (m_rehydrationState != VRRehydrationState::Connecting)
        {
            SetRehydrationState(VRRehydrationState::Offline);
            SetRehydrationStatus();
        }
        return;
    }

    if (m_rehydrationState == VRRehydrationState::Connecting)
    {
        SetRehydrationState(VRRehydrationState::Authenticated);
        SetRehydrationStatus();
    }

    if (m_rehydrationState == VRRehydrationState::Authenticated &&
        !VRRehydrationProfileRequiresAvatar(GetBuildRehydrationProfile()))
    {
        SetRehydrationState(VRRehydrationState::Ready);
        SetRehydrationStatus();
        return;
    }

    if (VRRehydrationProfileRequiresAvatar(GetBuildRehydrationProfile()) &&
        m_rehydrationState >= VRRehydrationState::Authenticated &&
        m_rehydrationState < VRRehydrationState::Ready)
    {
        const auto* avatar = m_world.ctx().find<VRAvatarService>();
        if (!avatar)
        {
            FailRehydration("the VR avatar rehydration service is unavailable");
            return;
        }

        const auto avatarState = avatar->GetRehydrationState();
        if (avatarState == VRRehydrationState::Failed)
        {
            FailRehydration(avatar->GetRehydrationFailure());
            return;
        }

        if (avatarState > m_rehydrationState && avatarState < VRRehydrationState::Failed)
        {
            const auto next = static_cast<VRRehydrationState>(
                static_cast<std::underlying_type_t<VRRehydrationState>>(m_rehydrationState) + 1);
            SetRehydrationState(next);
            SetRehydrationStatus();
        }
    }

    const auto deadline = VRRehydrationDeadlineSeconds(m_rehydrationState);
    if (deadline <= 0.0)
        return;

    m_rehydrationStageElapsed += std::clamp(aDelta, 0.0, 1.0);
    if (m_rehydrationStageElapsed >= deadline)
    {
        std::string reason{"rehydration state "};
        reason += VRRehydrationStateName(m_rehydrationState);
        reason += " exceeded its deadline";
        FailRehydration(reason.c_str());
    }
}

void VRConnectionService::SetRehydrationState(const VRRehydrationState aState) noexcept
{
    if (m_rehydrationState == aState)
        return;

    if (!CanTransitionVRRehydrationState(m_rehydrationState, aState))
    {
        if (aState != VRRehydrationState::Failed)
        {
            FailRehydration("invalid VR lifecycle rehydration state transition");
            return;
        }
    }

    const auto previous = m_rehydrationState;
    m_rehydrationState = aState;
    m_rehydrationStageElapsed = 0.0;
    m_statusDirty = true;
    spdlog::info("SkyrimTogetherVR lifecycle rehydration transition: {} -> {}",
                 VRRehydrationStateName(previous), VRRehydrationStateName(aState));

}

void VRConnectionService::SetRehydrationStatus() noexcept
{
    if (m_rehydrationState == VRRehydrationState::Ready)
    {
        SetStatus("online");
        return;
    }
    if (m_rehydrationState == VRRehydrationState::Failed)
    {
        SetStatus("rehydration_failed", m_rehydrationFailure);
        return;
    }
    SetStatus(VRRehydrationStateName(m_rehydrationState));
}

void VRConnectionService::FailRehydration(const char* const apReason) noexcept
{
    if (IsVRRehydrationTerminal(m_rehydrationState))
        return;

    m_rehydrationFailure = apReason && apReason[0] != '\0' ? apReason : "unknown VR lifecycle rehydration failure";
    for (auto& character : m_rehydrationFailure)
    {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x20 || value == 0x7f)
            character = ' ';
    }
    if (m_rehydrationFailure.size() > kMaximumStatusErrorBytes)
        m_rehydrationFailure.resize(kMaximumStatusErrorBytes);

    m_connectInFlight = false;
    m_waitingForRetirementDisconnect = false;
    m_hasPendingCommand = false;
    m_pendingCommand = {};
    InvalidateQueuedConnect();
    SetRehydrationState(VRRehydrationState::Failed);
    SetRehydrationStatus();
    spdlog::error("SkyrimTogetherVR lifecycle rehydration failed: {}", m_rehydrationFailure);
    if (m_transport.IsOnline())
        m_transport.Close();
}

void VRConnectionService::ArchiveCommandFile(const char* apSuffix) noexcept
{
    std::error_code ec;
    if (!std::filesystem::exists(m_commandPath, ec))
        return;

    auto archivedPath = m_commandPath;
    archivedPath += apSuffix;
    std::filesystem::remove(archivedPath, ec);
    ec.clear();
    std::filesystem::rename(m_commandPath, archivedPath, ec);
    if (ec)
    {
        spdlog::warn("SkyrimTogetherVR could not archive connection handoff file {}: {}", m_commandPath.string(), ec.message());
        return;
    }

    m_lastCommandContents.clear();
}

void VRConnectionService::SetStatus(std::string aState, std::string aError) noexcept
{
    for (auto& character : aError)
    {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x20 || value == 0x7f)
            character = ' ';
    }
    if (aError.size() > kMaximumStatusErrorBytes)
        aError.resize(kMaximumStatusErrorBytes);
    m_state = std::move(aState);
    m_lastError = std::move(aError);
    m_statusDirty = true;
}

void VRConnectionService::WriteStatusFile() noexcept
{
    const auto published = SkyrimTogetherVR::Handoff::WriteFileAtomically(
        m_statusPath,
        [this](std::ofstream& file)
        {
            SkyrimTogetherVR::Handoff::WriteLaunchIdentity(file);
            file << "state=" << m_state << "\n";
            file << "online=" << (IsReadyForGameplay() ? "1" : "0") << "\n";
            file << "transportOnline=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
            file << "rehydrationState=" << VRRehydrationStateName(m_rehydrationState) << "\n";
            file << "rehydrationProfile=" << VRRehydrationProfileName(GetBuildRehydrationProfile()) << "\n";
            file << "rehydrationReady=" << (IsReadyForGameplay() ? "1" : "0") << "\n";
            file << "rehydrationStageElapsedMs=" << static_cast<std::uint64_t>(m_rehydrationStageElapsed * 1000.0) << "\n";
            file << "rehydrationStageDeadlineMs=" << static_cast<std::uint64_t>(
                VRRehydrationDeadlineSeconds(m_rehydrationState) * 1000.0) << "\n";
            file << "playerId=" << m_transport.GetLocalPlayerId() << "\n";
            file << "sessionId=" << m_transport.GetSessionId() << "\n";
            file << "connectionGeneration=" << m_transport.GetConnectionGeneration() << "\n";
            file << "clientVersion=" << BUILD_COMMIT << "\n";
            file << "serverVersion=" << m_transport.GetAcceptedServerVersion() << "\n";
            file << "gameplayProtocolRevision=" << SkyrimTogether::Protocol::kGameplayProtocolRevision << "\n";
            file << "serverInstanceNonce=" << m_transport.GetServerInstanceNonce() << "\n";
            const auto& lifecycle = m_world.ctx().at<VRLifecycleService>();
            file << "lifecycleState=" << lifecycle.GetStateName() << "\n";
            file << "lifecycleEpoch=" << lifecycle.GetEpoch() << "\n";
            file << "commandFile=" << m_commandPath.string() << "\n";
            if (const auto* avatar = m_world.ctx().find<VRAvatarService>())
                file << "avatarRehydrationState=" << VRRehydrationStateName(avatar->GetRehydrationState()) << "\n";
            if (!m_rehydrationFailure.empty())
                file << "rehydrationFailure=" << m_rehydrationFailure << "\n";
            if (!m_lastError.empty())
                file << "error=" << m_lastError << "\n";
        });

    m_statusDirty = !published;
}

void VRConnectionService::WriteControlsSnapshot() noexcept
{
    const auto& party = m_world.GetPartyService();
    const auto now = m_transport.GetClock().GetCurrentTick();
    const auto published = SkyrimTogetherVR::Handoff::WriteFileAtomically(
        m_controlsPath,
        [this, &party, now](std::ofstream& file)
        {
            SkyrimTogetherVR::Handoff::WriteLaunchIdentity(file);
            file << "ready=1\n";
            file << "online=" << (HasStableAuthenticatedTransport() ? "1" : "0") << "\n";
            const auto& lifecycle = m_world.ctx().at<VRLifecycleService>();
            file << "lifecycleEpoch=" << lifecycle.GetEpoch() << "\n";
            file << "connectionGeneration=" << m_transport.GetConnectionGeneration() << "\n";
            file << "sessionId=" << m_transport.GetSessionId() << "\n";
            file << "serverInstanceNonce=" << m_transport.GetServerInstanceNonce() << "\n";
            file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
            file << "chat.available=1\n";
            // The client cannot determine admin membership. Expose the command
            // surface while keeping authorization entirely server-owned.
            file << "admin.enforcement=server_authoritative\n";
            file << "party.inParty=" << (party.IsInParty() ? "1" : "0") << "\n";
            file << "party.isLeader=" << (party.IsLeader() ? "1" : "0") << "\n";
            file << "party.leaderPlayerId=" << party.GetLeaderPlayerId() << "\n";

            std::vector<uint32_t> playerIds;
            playerIds.reserve(party.GetPlayers().size());
            for (const auto& [playerId, playerName] : party.GetPlayers())
            {
                TP_UNUSED(playerName);
                if (playerId != m_transport.GetLocalPlayerId())
                    playerIds.push_back(playerId);
            }
            std::sort(playerIds.begin(), playerIds.end());
            if (playerIds.size() > kMaximumControlSnapshotEntries)
                playerIds.resize(kMaximumControlSnapshotEntries);
            file << "player.count=" << playerIds.size() << "\n";
            for (const auto playerId : playerIds)
            {
                const auto player = party.GetPlayers().find(playerId);
                if (player != party.GetPlayers().end())
                    file << "player." << playerId << ".name=" << SnapshotText(player->second) << "\n";
            }

            std::vector<uint32_t> members(party.GetPartyMembers().begin(), party.GetPartyMembers().end());
            std::sort(members.begin(), members.end());
            if (members.size() > kMaximumControlSnapshotEntries)
                members.resize(kMaximumControlSnapshotEntries);
            file << "party.memberCount=" << members.size() << "\n";
            for (const auto playerId : members)
                file << "party.member." << playerId << "=1\n";

            std::vector<uint32_t> inviters;
            inviters.reserve(party.GetInvitations().size());
            for (const auto& [inviterId, expiry] : party.GetInvitations())
            {
                if (expiry >= now)
                    inviters.push_back(inviterId);
            }
            std::sort(inviters.begin(), inviters.end());
            if (inviters.size() > kMaximumControlSnapshotEntries)
                inviters.resize(kMaximumControlSnapshotEntries);
            file << "invite.count=" << inviters.size() << "\n";
            for (const auto inviterId : inviters)
            {
                const auto invitation = party.GetInvitations().find(inviterId);
                if (invitation == party.GetInvitations().end())
                    continue;
                file << "invite." << inviterId << ".expiryTick=" << invitation->second << "\n";
                const auto player = party.GetPlayers().find(inviterId);
                if (player != party.GetPlayers().end())
                    file << "invite." << inviterId << ".name=" << SnapshotText(player->second) << "\n";
            }
        });

    if (!published)
        m_statusDirty = true;
}
