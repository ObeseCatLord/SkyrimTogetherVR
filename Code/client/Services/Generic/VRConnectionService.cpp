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

#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/SendChatMessageRequest.h>
#include <Services/PartyService.h>
#include <Services/TransportService.h>
#include <Services/VRLifecycleService.h>
#include <VRRuntimeDiagnostics.h>
#include <World.h>

namespace
{
constexpr double kCommandPollInterval = 0.5;
constexpr double kStatusWriteInterval = 1.0;
constexpr char kCommandFileName[] = "SkyrimTogetherVR.command";
constexpr char kStatusFileName[] = "SkyrimTogetherVR.status";
constexpr std::size_t kMaximumCommandBytes = 2 * 1024;
constexpr std::size_t kMaximumEndpointBytes = 255;
constexpr std::size_t kMaximumPasswordBytes = 256;
constexpr std::size_t kMaximumChatBytes = 512;

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

bool ParsePlayerId(const std::string_view acValue, uint32_t& aOut) noexcept
{
    if (acValue.empty() || acValue.size() > 10)
        return false;

    const auto* begin = acValue.data();
    const auto* end = begin + acValue.size();
    const auto [position, error] = std::from_chars(begin, end, aOut);
    return error == std::errc{} && position == end && aOut != 0;
}

}

bool VRConnectionService::IsPartyTargetAction(const CommandAction aAction) noexcept
{
    return aAction == CommandAction::InviteToParty || aAction == CommandAction::AcceptPartyInvite ||
           aAction == CommandAction::KickPartyMember || aAction == CommandAction::ChangePartyLeader;
}

VRConnectionService::VRConnectionService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_commandPath(m_handoffDir / kCommandFileName)
    , m_statusPath(m_handoffDir / kStatusFileName)
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
    }
}

void VRConnectionService::OnConnected(const ConnectedEvent&) noexcept
{
    SkyrimTogetherVR::LogRuntimeCheckpoint("connected.connection_service.begin");
    if (!IsVrPlayerReadyForConnection(m_world))
    {
        spdlog::warn("SkyrimTogetherVR connection completed outside a ready lifecycle epoch; closing it");
        m_transport.Close();
        SkyrimTogetherVR::LogRuntimeCheckpoint("connected.connection_service.closed_not_ready");
        return;
    }

    m_connectInFlight = false;
    SetStatus("online");
    SkyrimTogetherVR::LogRuntimeCheckpoint("connected.connection_service.done");
}

void VRConnectionService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_connectInFlight = false;
    SetStatus(m_hasPendingCommand ? "waiting_for_gameplay" : "offline");
}

void VRConnectionService::OnConnectionError(const ConnectionErrorEvent& acEvent) noexcept
{
    m_connectInFlight = false;
    if (m_hasPendingCommand)
        SetStatus("waiting_for_gameplay");
    else
        SetStatus("error", acEvent.ErrorDetail.c_str());
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
        SetStatus("online");
        return false;
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

void VRConnectionService::HandleLifecycleBoundary() noexcept
{
    auto& lifecycle = m_world.ctx().at<VRLifecycleService>();
    const auto epoch = lifecycle.GetEpoch();
    if (epoch == m_lastLifecycleEpoch)
        return;

    m_lastLifecycleEpoch = epoch;
    m_statusDirty = true;
    if (lifecycle.IsReady())
        return;

    if (m_transport.IsOnline() || m_connectInFlight)
    {
        if (!m_retainedEndpoint.empty())
        {
            m_pendingCommand.Action = CommandAction::Connect;
            m_pendingCommand.Endpoint = m_retainedEndpoint;
            m_pendingCommand.Password = m_retainedPassword;
            m_hasPendingCommand = true;
        }
        m_connectInFlight = false;
        SetStatus(m_hasPendingCommand ? "waiting_for_gameplay" : "offline");
        spdlog::info(
            "SkyrimTogetherVR lifecycle epoch {} invalidated the connection; reconnectRetained={}", epoch,
            m_hasPendingCommand);
        m_transport.Close();
    }
    else if (m_hasPendingCommand)
    {
        SetStatus("waiting_for_gameplay");
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

    RunCommand(command);
    ArchiveCommandFile(".processed");
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
            else if (lowerValue == "party_create")
                command.Action = CommandAction::CreateParty;
            else if (lowerValue == "party_leave")
                command.Action = CommandAction::LeaveParty;
            else if (lowerValue == "party_invite")
                command.Action = CommandAction::InviteToParty;
            else if (lowerValue == "party_accept")
                command.Action = CommandAction::AcceptPartyInvite;
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
        else if (key == "message" || key == "text")
        {
            command.Message = value;
        }
        else if (key == "playerid" || key == "player_id" || key == "inviterid" || key == "inviter_id")
        {
            command.HasPlayerId = ParsePlayerId(value, command.PlayerId);
            if (!command.HasPlayerId)
                command.Error = "party command has an invalid player id";
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
    else if (IsPartyTargetAction(command.Action) && !command.HasPlayerId)
        command.Error = "party command is missing player id";
    else if (command.Action == CommandAction::None)
        command.Error = "command file did not contain a supported action";

    return command;
}

void VRConnectionService::TryRunPendingCommand() noexcept
{
    if (!m_hasPendingCommand)
        return;

    if (m_pendingCommand.Action == CommandAction::Connect && !IsVrPlayerReadyForConnection(m_world))
        return;

    RunCommand(m_pendingCommand);
    m_hasPendingCommand = false;
    m_pendingCommand = {};
    ArchiveCommandFile(".processed");
}

void VRConnectionService::RunCommand(const Command& acCommand) noexcept
{
    m_commandQueuedThisUpdate = true;

    switch (acCommand.Action)
    {
    case CommandAction::Connect: QueueConnect(acCommand.Endpoint, acCommand.Password); break;
    case CommandAction::Disconnect: QueueDisconnect(); break;
    case CommandAction::Chat: SendChat(acCommand.Message); break;
    case CommandAction::CreateParty:
    case CommandAction::LeaveParty:
    case CommandAction::InviteToParty:
    case CommandAction::AcceptPartyInvite:
    case CommandAction::KickPartyMember:
    case CommandAction::ChangePartyLeader: RunPartyCommand(acCommand); break;
    default: break;
    }
}

void VRConnectionService::QueueConnect(const std::string& acEndpoint, const std::string& acPassword) noexcept
{
    if (m_transport.IsOnline())
    {
        spdlog::warn("SkyrimTogetherVR connection request ignored because the client is already online");
        m_connectInFlight = false;
        SetStatus("online");
        return;
    }
    if (m_connectInFlight)
    {
        spdlog::info("SkyrimTogetherVR connection request ignored because a connection is already in flight");
        SetStatus("connecting");
        return;
    }

    spdlog::info("SkyrimTogetherVR queueing connection to {}", acEndpoint);
    m_retainedEndpoint = acEndpoint;
    m_retainedPassword = acPassword;
    m_connectInFlight = true;
    SetStatus("connecting");

    const auto lifecycleEpoch = m_world.ctx().at<VRLifecycleService>().GetEpoch();
    m_world.GetRunner().Queue([this, endpoint = acEndpoint, password = acPassword, lifecycleEpoch]() {
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
            SetStatus(m_hasPendingCommand ? "waiting_for_gameplay" : "offline");
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
    m_hasPendingCommand = false;
    m_pendingCommand = {};
    m_retainedEndpoint.clear();
    m_retainedPassword.clear();
    m_connectInFlight = false;
    SetStatus("disconnecting");
    m_world.GetRunner().Queue([]() { World::Get().GetTransport().Close(); });
}

void VRConnectionService::SendChat(const std::string& acMessage) noexcept
{
    if (!m_transport.IsOnline() || acMessage.empty() || acMessage.size() > kMaximumChatBytes || HasControlCharacter(acMessage))
    {
        spdlog::warn("SkyrimTogetherVR chat command rejected because the client is offline or the message is invalid");
        return;
    }
    SendChatMessageRequest request{};
    request.MessageType = ChatMessageType::kGlobalChat;
    request.ChatMessage = acMessage;
    m_transport.Send(request);
}

void VRConnectionService::RunPartyCommand(const Command& acCommand) noexcept
{
    if (!m_transport.IsOnline())
    {
        SetStatus("offline", "party command requires an online connection");
        spdlog::warn("SkyrimTogetherVR party command rejected because the client is offline");
        return;
    }

    auto& party = m_world.GetPartyService();
    switch (acCommand.Action)
    {
    case CommandAction::CreateParty: party.CreateParty(); break;
    case CommandAction::LeaveParty: party.LeaveParty(); break;
    case CommandAction::InviteToParty: party.CreateInvite(acCommand.PlayerId); break;
    case CommandAction::AcceptPartyInvite: party.AcceptInvite(acCommand.PlayerId); break;
    case CommandAction::KickPartyMember: party.KickPartyMember(acCommand.PlayerId); break;
    case CommandAction::ChangePartyLeader: party.ChangePartyLeader(acCommand.PlayerId); break;
    default: break;
    }
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
    m_state = std::move(aState);
    m_lastError = std::move(aError);
    m_statusDirty = true;
}

void VRConnectionService::WriteStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_statusPath, std::ios::trunc);
    if (!file)
        return;

    file << "state=" << m_state << "\n";
    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "playerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "sessionId=" << m_transport.GetSessionId() << "\n";
    file << "connectionGeneration=" << m_transport.GetConnectionGeneration() << "\n";
    const auto& lifecycle = m_world.ctx().at<VRLifecycleService>();
    file << "lifecycleState=" << lifecycle.GetStateName() << "\n";
    file << "lifecycleEpoch=" << lifecycle.GetEpoch() << "\n";
    file << "commandFile=" << m_commandPath.string() << "\n";
    if (!m_lastError.empty())
        file << "error=" << m_lastError << "\n";

    m_statusDirty = false;
}
