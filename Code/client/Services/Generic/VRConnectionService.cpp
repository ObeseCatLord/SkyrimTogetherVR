#include <TiltedOnlinePCH.h>

#include <Services/VRConnectionService.h>
#include <vr_common/VRHandoffPath.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <PlayerCharacter.h>
#include <VR/VRPlayerReadiness.h>
#include <Services/TransportService.h>
#include <World.h>

namespace
{
constexpr double kCommandPollInterval = 0.5;
constexpr double kStatusWriteInterval = 1.0;
constexpr char kCommandFileName[] = "SkyrimTogetherVR.command";
constexpr char kStatusFileName[] = "SkyrimTogetherVR.status";

bool IsVrPlayerReadyForConnection() noexcept
{
    const auto* pPlayer = SkyrimTogetherVR::TryGetReadablePlayerForVR();
    return pPlayer && pPlayer->GetBaseFormData() && pPlayer->GetParentCellData();
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

    std::ostringstream stream;
    stream << file.rdbuf();
    aOut = stream.str();
    return true;
}

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
    m_connectInFlight = false;
    SetStatus("online");
}

void VRConnectionService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_connectInFlight = false;
    SetStatus("offline");
}

void VRConnectionService::OnConnectionError(const ConnectionErrorEvent& acEvent) noexcept
{
    m_connectInFlight = false;
    SetStatus("error", acEvent.ErrorDetail.c_str());
}

bool VRConnectionService::RequestConnect(const std::string& acEndpoint, const std::string& acPassword) noexcept
{
    const auto endpoint = Trim(acEndpoint);
    if (endpoint.empty())
    {
        SetStatus("error", "connect command is missing endpoint");
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

    if (!IsVrPlayerReadyForConnection())
    {
        m_pendingCommand = std::move(command);
        m_hasPendingCommand = true;
        SetStatus("waiting_for_player");
        spdlog::info("SkyrimTogetherVR connection request queued until local player and parent cell are available");
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

    if (!IsVrPlayerReadyForConnection())
    {
        if (!m_reportedWaitingForPlayer)
        {
            spdlog::info("SkyrimTogetherVR connection handoff: waiting for local player and parent cell before autoconnect");
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
        return;

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

    if (command.Action == CommandAction::Connect && !IsVrPlayerReadyForConnection())
    {
        m_pendingCommand = std::move(command);
        m_hasPendingCommand = true;
        m_commandQueuedThisUpdate = true;
        SetStatus("waiting_for_player");
        spdlog::info("SkyrimTogetherVR connection handoff queued until local player and parent cell are available");
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
        }
        else if (key == "endpoint" || key == "address" || key == "server")
        {
            command.Endpoint = value;
        }
        else if (key == "password")
        {
            command.Password = value;
        }
    }

    if (command.Action == CommandAction::None && !command.Endpoint.empty())
        command.Action = CommandAction::Connect;

    if (command.Action == CommandAction::Connect && command.Endpoint.empty())
        command.Error = "connect command is missing endpoint";
    else if (command.Action == CommandAction::None)
        command.Error = "command file did not contain a supported action";

    return command;
}

void VRConnectionService::TryRunPendingCommand() noexcept
{
    if (!m_hasPendingCommand)
        return;

    if (m_pendingCommand.Action == CommandAction::Connect && !IsVrPlayerReadyForConnection())
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
    m_connectInFlight = true;
    SetStatus("connecting");

    m_world.GetRunner().Queue([endpoint = acEndpoint, password = acPassword]() {
        if (!password.empty())
            World::Get().GetTransport().SetServerPassword(password);

        World::Get().GetTransport().Connect(endpoint);
    });
}

void VRConnectionService::QueueDisconnect() noexcept
{
    spdlog::info("SkyrimTogetherVR queueing disconnect");
    m_hasPendingCommand = false;
    m_pendingCommand = {};
    m_connectInFlight = false;
    SetStatus("disconnecting");
    m_world.GetRunner().Queue([]() { World::Get().GetTransport().Close(); });
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
    file << "commandFile=" << m_commandPath.string() << "\n";
    if (!m_lastError.empty())
        file << "error=" << m_lastError << "\n";

    m_statusDirty = false;
}
