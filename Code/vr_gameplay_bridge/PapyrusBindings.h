#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace PapyrusBindingPolicy
{
constexpr std::size_t kMaximumEndpointLength = 255;
constexpr std::size_t kMaximumPasswordLength = 256;
constexpr std::size_t kMaximumConfigurationLength = sizeof("endpoint=") - 1 + kMaximumEndpointLength + 1 + sizeof("password=") - 1 + kMaximumPasswordLength + 1;

struct ConfiguredConnection
{
    std::string Endpoint;
    std::string Password;
};

struct CommandIdentity
{
    std::string LaunchNonce;
    std::uint64_t LifecycleEpoch{};
    std::uint64_t ConnectionGeneration{};
    std::uint64_t SessionId{};
    std::uint64_t ServerInstanceNonce{};
};

[[nodiscard]] inline bool HasControlCharacter(const std::string_view a_value) noexcept
{
    for (const auto character : a_value)
    {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x20 || value == 0x7F)
            return true;
    }
    return false;
}

[[nodiscard]] inline bool IsValidEndpoint(const std::string_view a_endpoint) noexcept
{
    if (a_endpoint.empty() || a_endpoint.size() > kMaximumEndpointLength || HasControlCharacter(a_endpoint))
        return false;

    for (const auto character : a_endpoint)
    {
        if (std::isspace(static_cast<unsigned char>(character)) != 0)
            return false;
    }
    return true;
}

[[nodiscard]] inline bool IsValidPassword(const std::string_view a_password) noexcept
{
    return a_password.size() <= kMaximumPasswordLength && !HasControlCharacter(a_password);
}

[[nodiscard]] inline std::string_view FindKeyValue(const std::string_view a_contents, const std::string_view a_key) noexcept
{
    std::size_t offset{};
    while (offset < a_contents.size())
    {
        const auto lineEnd = a_contents.find('\n', offset);
        auto line = a_contents.substr(offset, lineEnd == std::string_view::npos ? a_contents.size() - offset : lineEnd - offset);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        if (line.starts_with(a_key) && line.size() > a_key.size() && line[a_key.size()] == '=')
            return line.substr(a_key.size() + 1);

        if (lineEnd == std::string_view::npos)
            break;
        offset = lineEnd + 1;
    }
    return {};
}

[[nodiscard]] inline bool FindUniqueKeyValue(const std::string_view a_contents, const std::string_view a_key, std::string_view& a_value) noexcept
{
    bool found{};
    std::size_t offset{};
    while (offset < a_contents.size())
    {
        const auto lineEnd = a_contents.find('\n', offset);
        auto line = a_contents.substr(offset, lineEnd == std::string_view::npos ? a_contents.size() - offset : lineEnd - offset);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        if (line.starts_with(a_key) && line.size() > a_key.size() && line[a_key.size()] == '=')
        {
            if (found)
                return false;
            found = true;
            a_value = line.substr(a_key.size() + 1);
        }

        if (lineEnd == std::string_view::npos)
            break;
        offset = lineEnd + 1;
    }
    return found;
}

[[nodiscard]] inline bool ParseConfiguredConnection(const std::string_view a_contents, ConfiguredConnection& a_connection) noexcept
{
    if (a_contents.size() > kMaximumConfigurationLength)
        return false;

    std::string_view endpoint;
    std::string_view password;
    if (!FindUniqueKeyValue(a_contents, "endpoint", endpoint) || !FindUniqueKeyValue(a_contents, "password", password) || !IsValidEndpoint(endpoint) || !IsValidPassword(password))
        return false;

    a_connection.Endpoint.assign(endpoint);
    a_connection.Password.assign(password);
    return true;
}

[[nodiscard]] inline bool IsDecimalValue(const std::string_view a_value) noexcept
{
    return !a_value.empty() && a_value.size() <= 20 && [&a_value]
    {
        for (const auto character : a_value)
        {
            if (std::isdigit(static_cast<unsigned char>(character)) == 0)
                return false;
        }
        return true;
    }();
}

[[nodiscard]] inline bool ParseUnsignedValue(const std::string_view a_value, std::uint64_t& a_out) noexcept
{
    if (!IsDecimalValue(a_value))
        return false;

    std::uint64_t value{};
    for (const auto character : a_value)
    {
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    a_out = value;
    return true;
}

[[nodiscard]] inline bool IsLaunchNonce(const std::string_view a_value) noexcept
{
    if (a_value.size() != 32)
        return false;
    return std::all_of(a_value.begin(), a_value.end(), [](const unsigned char a_character)
    {
        return std::isdigit(a_character) != 0 || (a_character >= 'a' && a_character <= 'f');
    });
}

[[nodiscard]] inline bool NormalizeLaunchNonce(const std::string_view a_value, std::string& a_normalized) noexcept
{
    a_normalized.clear();
    if (a_value.size() != 32)
        return false;

    a_normalized.reserve(32);
    for (const auto character : a_value)
    {
        if (character >= '0' && character <= '9')
            a_normalized.push_back(character);
        else if (character >= 'a' && character <= 'f')
            a_normalized.push_back(character);
        else if (character >= 'A' && character <= 'F')
            a_normalized.push_back(static_cast<char>(character - 'A' + 'a'));
        else
        {
            a_normalized.clear();
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool ParseSnapshotIdentity(const std::string_view a_contents, CommandIdentity& a_identity) noexcept
{
    std::string_view launchNonce;
    std::string_view lifecycleEpoch;
    std::string_view connectionGeneration;
    std::string_view sessionId;
    std::string_view serverInstanceNonce;
    if (!FindUniqueKeyValue(a_contents, "launchNonce", launchNonce) || !FindUniqueKeyValue(a_contents, "lifecycleEpoch", lifecycleEpoch) ||
        !FindUniqueKeyValue(a_contents, "connectionGeneration", connectionGeneration) || !FindUniqueKeyValue(a_contents, "sessionId", sessionId) ||
        !FindUniqueKeyValue(a_contents, "serverInstanceNonce", serverInstanceNonce) || !NormalizeLaunchNonce(launchNonce, a_identity.LaunchNonce) ||
        !ParseUnsignedValue(lifecycleEpoch, a_identity.LifecycleEpoch) || !ParseUnsignedValue(connectionGeneration, a_identity.ConnectionGeneration) ||
        !ParseUnsignedValue(sessionId, a_identity.SessionId) || !ParseUnsignedValue(serverInstanceNonce, a_identity.ServerInstanceNonce))
    {
        a_identity = {};
        return false;
    }
    return true;
}

[[nodiscard]] inline bool ParseConsistentSnapshotIdentity(
    const std::string_view a_controls, const std::string_view a_status, const std::string_view a_currentLaunchNonce,
    CommandIdentity& a_identity) noexcept
{
    CommandIdentity controls;
    CommandIdentity status;
    std::string currentLaunchNonce;
    if (!ParseSnapshotIdentity(a_controls, controls) || !ParseSnapshotIdentity(a_status, status) ||
        !NormalizeLaunchNonce(a_currentLaunchNonce, currentLaunchNonce) ||
        controls.LaunchNonce != status.LaunchNonce || controls.LifecycleEpoch != status.LifecycleEpoch ||
        controls.ConnectionGeneration != status.ConnectionGeneration || controls.SessionId != status.SessionId ||
        controls.ServerInstanceNonce != status.ServerInstanceNonce || controls.LaunchNonce != currentLaunchNonce)
    {
        a_identity = {};
        return false;
    }

    a_identity = std::move(controls);
    return true;
}

[[nodiscard]] constexpr bool IsOnlineIdentity(const CommandIdentity& a_identity) noexcept
{
    return !a_identity.LaunchNonce.empty() && a_identity.LifecycleEpoch != 0 && a_identity.ConnectionGeneration != 0 &&
           a_identity.SessionId != 0 && a_identity.ServerInstanceNonce != 0;
}

[[nodiscard]] constexpr bool IsExplicitOfflineIdentity(const CommandIdentity& a_identity) noexcept
{
    return !a_identity.LaunchNonce.empty() && a_identity.LifecycleEpoch == 0 && a_identity.ConnectionGeneration == 0 &&
           a_identity.SessionId == 0 && a_identity.ServerInstanceNonce == 0;
}

[[nodiscard]] inline CommandIdentity MakeOfflineConnectIdentity(const std::string_view a_launchNonce)
{
    CommandIdentity identity;
    NormalizeLaunchNonce(a_launchNonce, identity.LaunchNonce);
    return identity;
}

[[nodiscard]] inline std::string BuildOnlineCommand(
    const std::string_view a_action, const CommandIdentity& a_identity, const std::string_view a_body = {})
{
    if (a_action.empty() || !IsOnlineIdentity(a_identity))
        return {};
    return "action=" + std::string(a_action) + "\nenvelope=online\nlaunchNonce=" + a_identity.LaunchNonce +
           "\nlifecycleEpoch=" + std::to_string(a_identity.LifecycleEpoch) +
           "\nconnectionGeneration=" + std::to_string(a_identity.ConnectionGeneration) +
           "\nsessionId=" + std::to_string(a_identity.SessionId) +
           "\nserverInstanceNonce=" + std::to_string(a_identity.ServerInstanceNonce) + "\n" + std::string(a_body);
}

[[nodiscard]] inline std::string BuildLaunchBoundConnectCommand(const ConfiguredConnection& a_connection, const std::string_view a_launchNonce)
{
    const auto identity = MakeOfflineConnectIdentity(a_launchNonce);
    if (!IsExplicitOfflineIdentity(identity))
        return {};
    return "action=connect\nenvelope=launch_bound_connect\nlaunchNonce=" + identity.LaunchNonce +
           "\nlifecycleEpoch=0\nconnectionGeneration=0\nsessionId=0\nserverInstanceNonce=0\nendpoint=" + a_connection.Endpoint +
           "\npassword=" + a_connection.Password + "\n";
}

[[nodiscard]] inline bool IsSafeToken(const std::string_view a_value, const std::size_t a_maximumLength = 64) noexcept
{
    if (a_value.empty() || a_value.size() > a_maximumLength || HasControlCharacter(a_value))
        return false;

    for (const auto character : a_value)
    {
        const auto value = static_cast<unsigned char>(character);
        if (!(std::isalnum(value) != 0 || character == '_' || character == '-' || character == '.'))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool IsSafeDisplayText(const std::string_view a_value, const std::size_t a_maximumLength = 128) noexcept
{
    return !a_value.empty() && a_value.size() <= a_maximumLength && !HasControlCharacter(a_value);
}

[[nodiscard]] inline bool IsPlayerId(const std::string_view a_value) noexcept
{
    return !a_value.empty() && a_value.size() <= 10 && [&a_value]
    {
        for (const auto character : a_value)
        {
            if (std::isdigit(static_cast<unsigned char>(character)) == 0)
                return false;
        }
        return true;
    }();
}

[[nodiscard]] inline std::string BuildPartyReadout(const std::string_view a_contents)
{
    if (FindKeyValue(a_contents, "ready") != "1")
        return "Party state unavailable.";

    const auto inParty = FindKeyValue(a_contents, "party.inParty") == "1";
    if (!inParty)
        return "Party: none\nCreate a party to invite online players.";

    std::string readout = "Party: active\nLeader player ID: ";
    const auto leader = FindKeyValue(a_contents, "party.leaderPlayerId");
    readout += IsPlayerId(leader) ? std::string(leader) : "unavailable";
    readout += FindKeyValue(a_contents, "party.isLeader") == "1" ? " (you)\nMembers:\n" : "\nMembers:\n";

    std::size_t offset{};
    bool found{};
    constexpr std::string_view prefix{"party.member."};
    while (offset < a_contents.size())
    {
        const auto lineEnd = a_contents.find('\n', offset);
        const auto line = a_contents.substr(offset, lineEnd == std::string_view::npos ? a_contents.size() - offset : lineEnd - offset);
        if (line.starts_with(prefix) && line.ends_with("=1"))
        {
            const auto playerId = line.substr(prefix.size(), line.size() - prefix.size() - 2);
            if (IsPlayerId(playerId))
            {
                const auto name = FindKeyValue(a_contents, "player." + std::string(playerId) + ".name");
                readout += "[" + std::string(playerId) + "] " + (IsSafeDisplayText(name) ? std::string(name) : "<unknown>") + "\n";
                found = true;
            }
        }
        if (lineEnd == std::string_view::npos)
            break;
        offset = lineEnd + 1;
    }
    return found ? readout : readout + "<No remote members>\n";
}

[[nodiscard]] inline std::string BuildPlayersReadout(const std::string_view a_contents)
{
    if (FindKeyValue(a_contents, "ready") != "1")
        return "Players unavailable.";

    std::string readout = "Online players:\n";
    std::size_t offset{};
    bool found{};
    constexpr std::string_view prefix{"player."};
    constexpr std::string_view suffix{".name="};
    while (offset < a_contents.size())
    {
        const auto lineEnd = a_contents.find('\n', offset);
        const auto line = a_contents.substr(offset, lineEnd == std::string_view::npos ? a_contents.size() - offset : lineEnd - offset);
        if (line.starts_with(prefix))
        {
            const auto separator = line.find(suffix, prefix.size());
            if (separator != std::string_view::npos)
            {
                const auto playerId = line.substr(prefix.size(), separator - prefix.size());
                const auto name = line.substr(separator + suffix.size());
                if (IsPlayerId(playerId) && IsSafeDisplayText(name))
                {
                    readout += "[" + std::string(playerId) + "] " + std::string(name) + "\n";
                    found = true;
                }
            }
        }
        if (lineEnd == std::string_view::npos)
            break;
        offset = lineEnd + 1;
    }
    return found ? readout : readout + "<None>\n";
}

[[nodiscard]] inline std::string BuildInviteReadout(const std::string_view a_contents)
{
    if (FindKeyValue(a_contents, "ready") != "1")
        return "Invitations unavailable.";

    std::string readout = "Pending invitations:\n";
    std::size_t offset{};
    bool found{};
    constexpr std::string_view prefix{"invite."};
    constexpr std::string_view suffix{".expiryTick="};
    while (offset < a_contents.size())
    {
        const auto lineEnd = a_contents.find('\n', offset);
        const auto line = a_contents.substr(offset, lineEnd == std::string_view::npos ? a_contents.size() - offset : lineEnd - offset);
        if (line.starts_with(prefix))
        {
            const auto separator = line.find(suffix, prefix.size());
            if (separator != std::string_view::npos)
            {
                const auto playerId = line.substr(prefix.size(), separator - prefix.size());
                const auto expiry = line.substr(separator + suffix.size());
                if (IsPlayerId(playerId) && IsDecimalValue(expiry))
                {
                    const auto name = FindKeyValue(a_contents, "invite." + std::string(playerId) + ".name");
                    readout += "[" + std::string(playerId) + "] " + (IsSafeDisplayText(name) ? std::string(name) : "<unknown>") +
                               " (expires tick " + std::string(expiry) + ")\n";
                    found = true;
                }
            }
        }
        if (lineEnd == std::string_view::npos)
            break;
        offset = lineEnd + 1;
    }
    return found ? readout : readout + "<None>\n";
}

[[nodiscard]] inline std::string BuildControlReadout(const std::string_view a_contents)
{
    if (FindKeyValue(a_contents, "ready") != "1")
        return "VR controls unavailable.";

    std::string readout = "Connection: ";
    readout += FindKeyValue(a_contents, "online") == "1" ? "online\n" : "offline\n";
    readout += "Chat: native send available; vanilla VR has no text-entry field.\n";
    readout += "Admin commands: ";
    readout += FindKeyValue(a_contents, "admin.enforcement") == "server_authoritative" ?
                   "available; server authorizes each request.\n" :
                   "unavailable.\n";
    return readout + BuildPlayersReadout(a_contents) + BuildPartyReadout(a_contents) + BuildInviteReadout(a_contents);
}

[[nodiscard]] inline std::string BuildTelemetryReadout(const std::string_view a_statusContents, const std::string_view a_gameplayContents)
{
    if (a_statusContents.empty())
        return "Telemetry unavailable: connection status is unavailable.";

    const auto token = [](const std::string_view a_contents, const std::string_view a_key)
    {
        const auto value = FindKeyValue(a_contents, a_key);
        return IsSafeToken(value) ? std::string(value) : "unavailable";
    };
    const auto decimal = [](const std::string_view a_contents, const std::string_view a_key)
    {
        const auto value = FindKeyValue(a_contents, a_key);
        return IsDecimalValue(value) ? std::string(value) : "unavailable";
    };
    const auto boolean = [](const std::string_view a_contents, const std::string_view a_key)
    {
        const auto value = FindKeyValue(a_contents, a_key);
        return value == "0" || value == "1" ? std::string(value) : "unavailable";
    };

    std::string readout = "Connection: " + token(a_statusContents, "state") + " (online=" + boolean(a_statusContents, "online") +
                          ", transport=" + boolean(a_statusContents, "transportOnline") + ")\n";
    readout += "Session: player=" + decimal(a_statusContents, "playerId") + " id=" + decimal(a_statusContents, "sessionId") +
               " generation=" + decimal(a_statusContents, "connectionGeneration") + "\n";
    readout += "Lifecycle: " + token(a_statusContents, "lifecycleState") + " epoch=" + decimal(a_statusContents, "lifecycleEpoch") +
               " rehydration=" + token(a_statusContents, "rehydrationState") + "\n";
    readout += "Network: client=" + token(a_statusContents, "clientVersion") + " server=" + token(a_statusContents, "serverVersion") +
               " protocol=" + decimal(a_statusContents, "gameplayProtocolRevision") + "\n";

    if (a_gameplayContents.empty())
        return readout + "Bridge: unavailable.";

    readout += "Bridge: ready=" + boolean(a_gameplayContents, "bridge.ready") + " state=" + token(a_gameplayContents, "bridge.endpointState") +
               " capabilities=" + decimal(a_gameplayContents, "bridge.activeCapabilities") + "\n";
    readout += "Bridge events: produced=" + decimal(a_gameplayContents, "bridge.producedEvents") + " consumed=" + decimal(a_gameplayContents, "bridge.consumedEvents") +
               " rejected=" + decimal(a_gameplayContents, "bridge.rejectedCommands") + " discarded=" + decimal(a_gameplayContents, "bridge.discardedEvents");
    return readout;
}
} // namespace PapyrusBindingPolicy

// Register the CommonLib Papyrus callback after SKSE::Init succeeds. The callback
// provides the VR-safe connection, chat, player-list, and party command surface.
[[nodiscard]] bool RegisterPapyrusBindings() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter
