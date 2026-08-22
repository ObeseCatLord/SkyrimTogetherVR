#include "PapyrusBindings.h"
#include "VRControlMenu.h"
#include "AvatarManager.h"

#include "pch.h"

#include <vr_common/VRHandoffPath.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr std::size_t kMaximumStatusLength = 16 * 1024;
constexpr std::size_t kMaximumGameplayTelemetryLength = 32 * 1024;
constexpr std::size_t kMaximumControlsLength = 32 * 1024;
constexpr std::size_t kMaximumChatLength = 512;
constexpr std::size_t kMaximumPlayerListLength = 8 * 1024;
constexpr std::size_t kMaximumPlayerNameLength = 128;
constexpr char kPapyrusScript[] = "SkyrimTogetherUtils";
constexpr char kCommandFileName[] = "SkyrimTogetherVR.command";
constexpr char kConfigFileName[] = "SkyrimTogetherVR.connection";
constexpr char kStatusFileName[] = "SkyrimTogetherVR.status";
constexpr char kGameplayTelemetryFileName[] = "SkyrimTogetherVR.gameplay";
constexpr char kRemotePlayersFileName[] = "SkyrimTogetherVR.remoteplayers";
constexpr char kControlsFileName[] = "SkyrimTogetherVR.controls";

[[nodiscard]] std::filesystem::path GetHandoffPath(const char* a_fileName)
{
    return SkyrimTogetherVR::Handoff::GetFile(a_fileName);
}

[[nodiscard]] bool WriteAll(const HANDLE a_file, const std::string_view a_contents) noexcept
{
    const auto* data = a_contents.data();
    auto remaining = a_contents.size();
    while (remaining > 0)
    {
        const auto chunkSize = static_cast<DWORD>(std::min<std::size_t>(remaining, MAXDWORD));
        DWORD written{};
        if (!WriteFile(a_file, data, chunkSize, &written, nullptr) || written != chunkSize)
            return false;
        data += written;
        remaining -= written;
    }
    return true;
}

[[nodiscard]] bool WriteAtomically(
    const std::filesystem::path& a_path,
    const std::string_view a_contents,
    const bool a_replaceExisting = true) noexcept
{
    try
    {
        std::error_code error;
        std::filesystem::create_directories(a_path.parent_path(), error);
        if (error)
            return false;

        static std::atomic_uint64_t nextTemporaryFileId{};
        auto temporaryPath = a_path;
        temporaryPath += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(nextTemporaryFileId.fetch_add(1, std::memory_order_relaxed)) + L".tmp";

        const HANDLE file = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        const bool wrote = WriteAll(file, a_contents) && FlushFileBuffers(file);
        CloseHandle(file);
        if (!wrote)
        {
            DeleteFileW(temporaryPath.c_str());
            return false;
        }

        const auto moveFlags = MOVEFILE_WRITE_THROUGH | (a_replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);
        if (MoveFileExW(temporaryPath.c_str(), a_path.c_str(), moveFlags))
            return true;

        DeleteFileW(temporaryPath.c_str());
        return false;
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool WriteCommandAtomically(const std::string_view a_contents) noexcept
{
    return WriteAtomically(GetHandoffPath(kCommandFileName), a_contents, false);
}

[[nodiscard]] bool ReadBoundedTextFile(const std::filesystem::path& a_path, const std::size_t a_maximumLength, std::string& a_contents) noexcept
{
    try
    {
        std::ifstream file(a_path, std::ios::binary);
        if (!file)
            return false;

        a_contents.clear();
        std::array<char, 256> buffer{};
        while (file)
        {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read = file.gcount();
            if (read <= 0)
                break;

            if (a_contents.size() + static_cast<std::size_t>(read) > a_maximumLength)
                return false;

            a_contents.append(buffer.data(), static_cast<std::size_t>(read));
        }
        return !file.bad();
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool ReadStatusFile(std::string& a_contents) noexcept
{
    return ReadBoundedTextFile(GetHandoffPath(kStatusFileName), kMaximumStatusLength, a_contents);
}

[[nodiscard]] bool ReadControlsFile(std::string& a_contents) noexcept
{
    return ReadBoundedTextFile(GetHandoffPath(kControlsFileName), kMaximumControlsLength, a_contents);
}

[[nodiscard]] bool ReadCurrentCommandIdentity(PapyrusBindingPolicy::CommandIdentity& a_identity, const bool a_requireOnline) noexcept
{
    std::string controls;
    std::string status;
    if (!ReadControlsFile(controls) || !ReadStatusFile(status) ||
        !PapyrusBindingPolicy::ParseConsistentSnapshotIdentity(
            controls, status, SkyrimTogetherVR::Handoff::GetLaunchNonce(), a_identity) ||
        PapyrusBindingPolicy::FindKeyValue(controls, "ready") != "1")
        return false;

    const auto controlsOnline = PapyrusBindingPolicy::FindKeyValue(controls, "online");
    const auto statusOnline = PapyrusBindingPolicy::FindKeyValue(status, "online");
    return a_requireOnline ?
               controlsOnline == "1" && statusOnline == "1" && PapyrusBindingPolicy::IsOnlineIdentity(a_identity) :
               controlsOnline == "0" && statusOnline == "0";
}

[[nodiscard]] bool WriteOnlineCommand(const std::string_view a_action, const std::string_view a_body = {}) noexcept
{
    PapyrusBindingPolicy::CommandIdentity identity;
    if (!ReadCurrentCommandIdentity(identity, true))
        return false;
    const auto command = PapyrusBindingPolicy::BuildOnlineCommand(a_action, identity, a_body);
    return !command.empty() && WriteCommandAtomically(command);
}

[[nodiscard]] bool WriteLaunchBoundConnectCommand(const PapyrusBindingPolicy::ConfiguredConnection& a_connection) noexcept
{
    PapyrusBindingPolicy::CommandIdentity identity;
    if (!ReadCurrentCommandIdentity(identity, false))
        return false;
    const auto command = PapyrusBindingPolicy::BuildLaunchBoundConnectCommand(a_connection, identity.LaunchNonce);
    return !command.empty() && WriteCommandAtomically(command);
}

[[nodiscard]] bool IsValidState(const std::string_view a_state) noexcept
{
    return PapyrusBindingPolicy::IsSafeToken(a_state);
}

[[nodiscard]] std::string GetConnectionState()
{
    std::string contents;
    if (!ReadStatusFile(contents))
        return "offline";

    const auto state = PapyrusBindingPolicy::FindKeyValue(contents, "state");
    return IsValidState(state) ? std::string(state) : "offline";
}

[[nodiscard]] bool IsConnected()
{
    std::string contents;
    return ReadStatusFile(contents) && PapyrusBindingPolicy::FindKeyValue(contents, "online") == "1";
}

[[nodiscard]] bool SendSkyrimTogetherChat(RE::StaticFunctionTag*, std::string a_message)
{
    return !a_message.empty() && a_message.size() <= kMaximumChatLength && !PapyrusBindingPolicy::HasControlCharacter(a_message) &&
           WriteOnlineCommand("chat", "message=" + a_message + "\n");
}

[[nodiscard]] bool IsValidPlayerId(const std::int32_t a_playerId) noexcept
{
    return a_playerId > 0;
}

[[nodiscard]] bool IsValidTeleportPlayerId(const std::int32_t a_playerId) noexcept
{
    return a_playerId > 0 && a_playerId <= 0xffff;
}

[[nodiscard]] bool IsValidPlayerIdText(const std::string_view a_playerId) noexcept
{
    return !a_playerId.empty() && a_playerId.size() <= 10 && std::all_of(a_playerId.begin(), a_playerId.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

[[nodiscard]] bool QueuePartyCommand(const std::string_view a_action) noexcept
{
    return WriteOnlineCommand(a_action);
}

[[nodiscard]] bool QueuePartyTargetCommand(const std::string_view a_action, const std::int32_t a_playerId) noexcept
{
    if (!IsValidPlayerId(a_playerId))
        return false;

    return WriteOnlineCommand(a_action, "playerId=" + std::to_string(a_playerId) + "\n");
}

[[nodiscard]] bool CreateSkyrimTogetherParty(RE::StaticFunctionTag*)
{
    return QueuePartyCommand("party_create");
}

[[nodiscard]] bool LeaveSkyrimTogetherParty(RE::StaticFunctionTag*)
{
    return QueuePartyCommand("party_leave");
}

[[nodiscard]] bool InviteSkyrimTogetherPartyMember(RE::StaticFunctionTag*, const std::int32_t a_playerId)
{
    return QueuePartyTargetCommand("party_invite", a_playerId);
}

[[nodiscard]] bool AcceptSkyrimTogetherPartyInvite(RE::StaticFunctionTag*, const std::int32_t a_inviterId)
{
    return QueuePartyTargetCommand("party_accept", a_inviterId);
}

[[nodiscard]] bool KickSkyrimTogetherPartyMember(RE::StaticFunctionTag*, const std::int32_t a_playerId)
{
    return QueuePartyTargetCommand("party_kick", a_playerId);
}

[[nodiscard]] bool ChangeSkyrimTogetherPartyLeader(RE::StaticFunctionTag*, const std::int32_t a_playerId)
{
    return QueuePartyTargetCommand("party_change_leader", a_playerId);
}

[[nodiscard]] bool DeclineSkyrimTogetherPartyInvite(RE::StaticFunctionTag*, const std::int32_t a_inviterId)
{
    return QueuePartyTargetCommand("party_decline", a_inviterId);
}

[[nodiscard]] bool SetSkyrimTogetherTime(RE::StaticFunctionTag*, const std::int32_t a_hours, const std::int32_t a_minutes)
{
    if (a_hours < 0 || a_hours > 23 || a_minutes < 0 || a_minutes > 59)
        return false;
    return WriteOnlineCommand("set_time", "hours=" + std::to_string(a_hours) + "\nminutes=" + std::to_string(a_minutes) + "\n");
}

[[nodiscard]] bool TeleportSkyrimTogetherToPlayer(RE::StaticFunctionTag*, const std::int32_t a_playerId)
{
    if (!IsValidTeleportPlayerId(a_playerId))
        return false;
    std::string controls;
    PapyrusBindingPolicy::CommandIdentity identity;
    if (!ReadControlsFile(controls) || !ReadCurrentCommandIdentity(identity, true) ||
        PapyrusBindingPolicy::FindKeyValue(controls, "party.member." + std::to_string(a_playerId)) != "1")
        return false;
    return QueuePartyTargetCommand("teleport_to_player", a_playerId);
}

[[nodiscard]] std::string GetSkyrimTogetherPartySummary(RE::StaticFunctionTag*)
{
    std::string contents;
    return ReadControlsFile(contents) ? PapyrusBindingPolicy::BuildPartyReadout(contents) : "Party state unavailable.";
}

[[nodiscard]] std::string GetSkyrimTogetherPlayersSummary(RE::StaticFunctionTag*)
{
    std::string contents;
    return ReadControlsFile(contents) ? PapyrusBindingPolicy::BuildPlayersReadout(contents) : "Players unavailable.";
}

[[nodiscard]] std::string GetSkyrimTogetherInviteList(RE::StaticFunctionTag*)
{
    std::string contents;
    return ReadControlsFile(contents) ? PapyrusBindingPolicy::BuildInviteReadout(contents) : "Invitations unavailable.";
}

[[nodiscard]] std::string GetSkyrimTogetherControlSummary(RE::StaticFunctionTag*)
{
    std::string contents;
    return ReadControlsFile(contents) ? PapyrusBindingPolicy::BuildControlReadout(contents) : "VR controls unavailable.";
}

[[nodiscard]] std::string GetSkyrimTogetherPlayerList(RE::StaticFunctionTag*)
{
    try
    {
        std::string contents;
        if (!ReadBoundedTextFile(GetHandoffPath(kRemotePlayersFileName), kMaximumPlayerListLength, contents))
            return "No remote players";

        constexpr std::string_view kPlayerPrefix{"remotePlayer."};
        constexpr std::string_view kUsernameSeparator{".username="};
        std::string playerList;
        std::size_t offset{};
        while (offset < contents.size())
        {
            const auto lineEnd = contents.find('\n', offset);
            auto line = std::string_view(contents).substr(offset, lineEnd == std::string::npos ? contents.size() - offset : lineEnd - offset);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);

            if (line.starts_with(kPlayerPrefix))
            {
                const auto usernameSeparator = line.find(kUsernameSeparator, kPlayerPrefix.size());
                if (usernameSeparator != std::string_view::npos)
                {
                    const auto playerId = line.substr(kPlayerPrefix.size(), usernameSeparator - kPlayerPrefix.size());
                    const auto username = line.substr(usernameSeparator + kUsernameSeparator.size());
                    if (IsValidPlayerIdText(playerId) && username.size() <= kMaximumPlayerNameLength && !PapyrusBindingPolicy::HasControlCharacter(username))
                    {
                        const std::string entry = "[" + std::string(playerId) + "] " + (username.empty() ? "<unknown>" : std::string(username)) + "\n";
                        if (playerList.size() + entry.size() > kMaximumPlayerListLength)
                            break;
                        playerList += entry;
                    }
                }
            }

            if (lineEnd == std::string::npos)
                break;
            offset = lineEnd + 1;
        }

        return playerList.empty() ? "No remote players" : playerList;
    }
    catch (...)
    {
        return "No remote players";
    }
}

[[nodiscard]] bool ConnectToSkyrimTogether(RE::StaticFunctionTag*, std::string a_endpoint, std::string a_password)
{
    if (!PapyrusBindingPolicy::IsValidEndpoint(a_endpoint) || !PapyrusBindingPolicy::IsValidPassword(a_password))
        return false;

    try
    {
        const std::string config = "endpoint=" + a_endpoint + "\npassword=" + a_password + "\n";
        PapyrusBindingPolicy::ConfiguredConnection connection{std::move(a_endpoint), std::move(a_password)};
        return WriteAtomically(GetHandoffPath(kConfigFileName), config) && WriteLaunchBoundConnectCommand(connection);
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool IsPlayer(RE::StaticFunctionTag*, RE::Actor* a_actor)
{
    return a_actor && a_actor == RE::PlayerCharacter::GetSingleton();
}

[[nodiscard]] bool IsRemotePlayer(RE::StaticFunctionTag*, RE::Actor* a_actor)
{
    return AvatarManager::Get().IsManagedRemoteActor(a_actor);
}

[[nodiscard]] std::string GetConfiguredEndpoint(RE::StaticFunctionTag*)
{
    try
    {
        std::string contents;
        if (!ReadBoundedTextFile(GetHandoffPath(kConfigFileName), PapyrusBindingPolicy::kMaximumConfigurationLength, contents))
            return {};
        const auto value = PapyrusBindingPolicy::FindKeyValue(contents, "endpoint");
        return PapyrusBindingPolicy::IsValidEndpoint(value) ? std::string(value) : std::string{};
    }
    catch (...)
    {
        return {};
    }
}

[[nodiscard]] bool ConnectToConfiguredSkyrimTogether(RE::StaticFunctionTag*)
{
    try
    {
        std::string contents;
        if (!ReadBoundedTextFile(GetHandoffPath(kConfigFileName), PapyrusBindingPolicy::kMaximumConfigurationLength, contents))
            return false;

        PapyrusBindingPolicy::ConfiguredConnection connection;
        return PapyrusBindingPolicy::ParseConfiguredConnection(contents, connection) &&
               WriteLaunchBoundConnectCommand(connection);
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] std::string GetStatusSummary(RE::StaticFunctionTag*)
{
    return GetConnectionState();
}

[[nodiscard]] std::string GetTelemetryReadout(RE::StaticFunctionTag*)
{
    try
    {
        std::string status;
        if (!ReadStatusFile(status))
            return PapyrusBindingPolicy::BuildTelemetryReadout({}, {});

        std::string gameplay;
        if (!ReadBoundedTextFile(GetHandoffPath(kGameplayTelemetryFileName), kMaximumGameplayTelemetryLength, gameplay))
            gameplay.clear();
        return PapyrusBindingPolicy::BuildTelemetryReadout(status, gameplay);
    }
    catch (...)
    {
        return "Telemetry unavailable: connection status is unavailable.";
    }
}

[[nodiscard]] bool DisconnectFromSkyrimTogether(RE::StaticFunctionTag*)
{
    return WriteOnlineCommand("disconnect");
}

[[nodiscard]] bool OpenSkyrimTogetherVRControlMenu(RE::StaticFunctionTag*)
{
    return VRControlMenu::Open();
}

[[nodiscard]] bool IsSkyrimTogetherConnected(RE::StaticFunctionTag*)
{
    return IsConnected();
}

[[nodiscard]] std::string GetSkyrimTogetherConnectionState(RE::StaticFunctionTag*)
{
    return GetConnectionState();
}

[[nodiscard]] bool SetSkyrimTogetherConnectionConfig(RE::StaticFunctionTag*, std::string a_endpoint, std::string a_password)
{
    if (!PapyrusBindingPolicy::IsValidEndpoint(a_endpoint) || !PapyrusBindingPolicy::IsValidPassword(a_password))
        return false;

    try
    {
        const std::string config = "endpoint=" + a_endpoint + "\npassword=" + a_password + "\n";
        return WriteAtomically(GetHandoffPath(kConfigFileName), config);
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* a_vm)
{
    if (!a_vm)
        return false;

    a_vm->RegisterFunction("ConnectToSkyrimTogether", kPapyrusScript, ConnectToSkyrimTogether);
    a_vm->RegisterFunction("IsRemotePlayer", kPapyrusScript, IsRemotePlayer);
    a_vm->RegisterFunction("IsPlayer", kPapyrusScript, IsPlayer);
    a_vm->RegisterFunction("DisconnectFromSkyrimTogether", kPapyrusScript, DisconnectFromSkyrimTogether);
    a_vm->RegisterFunction("OpenSkyrimTogetherVRControlMenu", kPapyrusScript, OpenSkyrimTogetherVRControlMenu);
    a_vm->RegisterFunction("IsSkyrimTogetherConnected", kPapyrusScript, IsSkyrimTogetherConnected);
    a_vm->RegisterFunction("GetSkyrimTogetherConnectionState", kPapyrusScript, GetSkyrimTogetherConnectionState);
    a_vm->RegisterFunction("SetSkyrimTogetherConnectionConfig", kPapyrusScript, SetSkyrimTogetherConnectionConfig);
    a_vm->RegisterFunction("GetSkyrimTogetherConfiguredEndpoint", kPapyrusScript, GetConfiguredEndpoint);
    a_vm->RegisterFunction("ConnectToConfiguredSkyrimTogether", kPapyrusScript, ConnectToConfiguredSkyrimTogether);
    a_vm->RegisterFunction("GetSkyrimTogetherStatusSummary", kPapyrusScript, GetStatusSummary);
    a_vm->RegisterFunction("GetSkyrimTogetherTelemetryReadout", kPapyrusScript, GetTelemetryReadout);
    a_vm->RegisterFunction("SendSkyrimTogetherChat", kPapyrusScript, SendSkyrimTogetherChat);
    a_vm->RegisterFunction("GetSkyrimTogetherPlayerList", kPapyrusScript, GetSkyrimTogetherPlayerList);
    a_vm->RegisterFunction("CreateSkyrimTogetherParty", kPapyrusScript, CreateSkyrimTogetherParty);
    a_vm->RegisterFunction("LeaveSkyrimTogetherParty", kPapyrusScript, LeaveSkyrimTogetherParty);
    a_vm->RegisterFunction("InviteSkyrimTogetherPartyMember", kPapyrusScript, InviteSkyrimTogetherPartyMember);
    a_vm->RegisterFunction("AcceptSkyrimTogetherPartyInvite", kPapyrusScript, AcceptSkyrimTogetherPartyInvite);
    a_vm->RegisterFunction("DeclineSkyrimTogetherPartyInvite", kPapyrusScript, DeclineSkyrimTogetherPartyInvite);
    a_vm->RegisterFunction("KickSkyrimTogetherPartyMember", kPapyrusScript, KickSkyrimTogetherPartyMember);
    a_vm->RegisterFunction("ChangeSkyrimTogetherPartyLeader", kPapyrusScript, ChangeSkyrimTogetherPartyLeader);
    a_vm->RegisterFunction("SetSkyrimTogetherTime", kPapyrusScript, SetSkyrimTogetherTime);
    a_vm->RegisterFunction("TeleportSkyrimTogetherToPlayer", kPapyrusScript, TeleportSkyrimTogetherToPlayer);
    a_vm->RegisterFunction("GetSkyrimTogetherPartySummary", kPapyrusScript, GetSkyrimTogetherPartySummary);
    a_vm->RegisterFunction("GetSkyrimTogetherPlayersSummary", kPapyrusScript, GetSkyrimTogetherPlayersSummary);
    a_vm->RegisterFunction("GetSkyrimTogetherInviteList", kPapyrusScript, GetSkyrimTogetherInviteList);
    a_vm->RegisterFunction("GetSkyrimTogetherControlSummary", kPapyrusScript, GetSkyrimTogetherControlSummary);
    return true;
}
} // namespace

bool RegisterPapyrusBindings() noexcept
{
    try
    {
        const auto* papyrus = SKSE::GetPapyrusInterface();
        return papyrus && papyrus->Register(RegisterPapyrusFunctions);
    }
    catch (...)
    {
        return false;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter
