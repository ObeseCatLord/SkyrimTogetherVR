#include "PapyrusBindings.h"
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
constexpr std::size_t kMaximumEndpointLength = 255;
constexpr std::size_t kMaximumPasswordLength = 256;
constexpr std::size_t kMaximumStatusLength = 16 * 1024;
constexpr std::size_t kMaximumChatLength = 512;
constexpr std::size_t kMaximumPlayerListLength = 8 * 1024;
constexpr std::size_t kMaximumPlayerNameLength = 128;
constexpr char kPapyrusScript[] = "SkyrimTogetherUtils";
constexpr char kCommandFileName[] = "SkyrimTogetherVR.command";
constexpr char kConfigFileName[] = "SkyrimTogetherVR.connection";
constexpr char kStatusFileName[] = "SkyrimTogetherVR.status";
constexpr char kRemotePlayersFileName[] = "SkyrimTogetherVR.remoteplayers";

[[nodiscard]] std::filesystem::path GetHandoffPath(const char* a_fileName)
{
    return SkyrimTogetherVR::Handoff::GetFile(a_fileName);
}

[[nodiscard]] bool HasControlCharacter(const std::string_view a_value) noexcept
{
    for (const auto character : a_value) {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x20 || value == 0x7F)
            return true;
    }
    return false;
}

[[nodiscard]] bool IsValidEndpoint(const std::string_view a_endpoint) noexcept
{
    if (a_endpoint.empty() || a_endpoint.size() > kMaximumEndpointLength || HasControlCharacter(a_endpoint))
        return false;

    for (const auto character : a_endpoint) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0)
            return false;
    }
    return true;
}

[[nodiscard]] bool IsValidPassword(const std::string_view a_password) noexcept
{
    return a_password.size() <= kMaximumPasswordLength && !HasControlCharacter(a_password);
}

[[nodiscard]] bool WriteAll(const HANDLE a_file, const std::string_view a_contents) noexcept
{
    const auto* data = a_contents.data();
    auto remaining = a_contents.size();
    while (remaining > 0) {
        const auto chunkSize = static_cast<DWORD>(std::min<std::size_t>(remaining, MAXDWORD));
        DWORD written{};
        if (!WriteFile(a_file, data, chunkSize, &written, nullptr) || written != chunkSize)
            return false;
        data += written;
        remaining -= written;
    }
    return true;
}

[[nodiscard]] bool WriteAtomically(const std::filesystem::path& a_path, const std::string_view a_contents) noexcept
{
    try {
        std::error_code error;
        std::filesystem::create_directories(a_path.parent_path(), error);
        if (error)
            return false;

        static std::atomic_uint64_t nextTemporaryFileId{};
        auto temporaryPath = a_path;
        temporaryPath += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                         std::to_wstring(nextTemporaryFileId.fetch_add(1, std::memory_order_relaxed)) + L".tmp";

        const HANDLE file = CreateFileW(
            temporaryPath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        const bool wrote = WriteAll(file, a_contents) && FlushFileBuffers(file);
        CloseHandle(file);
        if (!wrote) {
            DeleteFileW(temporaryPath.c_str());
            return false;
        }

        if (MoveFileExW(temporaryPath.c_str(), a_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;

        DeleteFileW(temporaryPath.c_str());
        return false;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool ReadStatusFile(std::string& a_contents) noexcept
{
    try {
        const auto path = GetHandoffPath(kStatusFileName);
        const HANDLE file = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        LARGE_INTEGER size{};
        const bool validSize = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
                               static_cast<std::uint64_t>(size.QuadPart) <= kMaximumStatusLength;
        if (!validSize) {
            CloseHandle(file);
            return false;
        }

        a_contents.assign(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read{};
        const bool readSucceeded = a_contents.empty() ||
                                   (ReadFile(file, a_contents.data(), static_cast<DWORD>(a_contents.size()), &read, nullptr) &&
                                    read == a_contents.size());
        CloseHandle(file);
        return readSucceeded;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool ReadBoundedTextFile(
    const std::filesystem::path& a_path,
    const std::size_t a_maximumLength,
    std::string& a_contents) noexcept
{
    try {
        std::ifstream file(a_path, std::ios::binary);
        if (!file)
            return false;

        a_contents.clear();
        std::array<char, 256> buffer{};
        while (file) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read = file.gcount();
            if (read <= 0)
                break;

            if (a_contents.size() + static_cast<std::size_t>(read) > a_maximumLength)
                return false;

            a_contents.append(buffer.data(), static_cast<std::size_t>(read));
        }
        return !file.bad();
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::string_view GetStatusValue(const std::string_view a_contents, const std::string_view a_key) noexcept
{
    std::size_t offset{};
    while (offset < a_contents.size()) {
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

[[nodiscard]] bool IsValidState(const std::string_view a_state) noexcept
{
    if (a_state.empty() || a_state.size() > 64 || HasControlCharacter(a_state))
        return false;

    for (const auto character : a_state) {
        const auto value = static_cast<unsigned char>(character);
        if (!(std::isalnum(value) != 0 || character == '_' || character == '-'))
            return false;
    }
    return true;
}

[[nodiscard]] std::string GetConnectionState()
{
    std::string contents;
    if (!ReadStatusFile(contents))
        return "offline";

    const auto state = GetStatusValue(contents, "state");
    return IsValidState(state) ? std::string(state) : "offline";
}

[[nodiscard]] bool IsConnected()
{
    std::string contents;
    return ReadStatusFile(contents) && GetStatusValue(contents, "online") == "1";
}

[[nodiscard]] bool SendSkyrimTogetherChat(RE::StaticFunctionTag*, std::string a_message)
{
    return !a_message.empty() && a_message.size() <= kMaximumChatLength && !HasControlCharacter(a_message) &&
           WriteAtomically(GetHandoffPath(kCommandFileName), "action=chat\nmessage=" + a_message + "\n");
}

[[nodiscard]] bool IsValidPlayerId(const std::int32_t a_playerId) noexcept
{
    return a_playerId > 0;
}

[[nodiscard]] bool IsValidPlayerIdText(const std::string_view a_playerId) noexcept
{
    return !a_playerId.empty() && a_playerId.size() <= 10 &&
           std::all_of(a_playerId.begin(), a_playerId.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

[[nodiscard]] bool QueuePartyCommand(const std::string_view a_action) noexcept
{
    return WriteAtomically(GetHandoffPath(kCommandFileName), "action=" + std::string(a_action) + "\n");
}

[[nodiscard]] bool QueuePartyTargetCommand(const std::string_view a_action, const std::int32_t a_playerId) noexcept
{
    if (!IsValidPlayerId(a_playerId))
        return false;

    return WriteAtomically(
        GetHandoffPath(kCommandFileName),
        "action=" + std::string(a_action) + "\nplayerId=" + std::to_string(a_playerId) + "\n");
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

[[nodiscard]] std::string GetSkyrimTogetherPlayerList(RE::StaticFunctionTag*)
{
    try {
        std::string contents;
        if (!ReadBoundedTextFile(GetHandoffPath(kRemotePlayersFileName), kMaximumPlayerListLength, contents))
            return "No remote players";

        constexpr std::string_view kPlayerPrefix{"remotePlayer."};
        constexpr std::string_view kUsernameSeparator{".username="};
        std::string playerList;
        std::size_t offset{};
        while (offset < contents.size()) {
            const auto lineEnd = contents.find('\n', offset);
            auto line = std::string_view(contents).substr(
                offset,
                lineEnd == std::string::npos ? contents.size() - offset : lineEnd - offset);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);

            if (line.starts_with(kPlayerPrefix)) {
                const auto usernameSeparator = line.find(kUsernameSeparator, kPlayerPrefix.size());
                if (usernameSeparator != std::string_view::npos) {
                    const auto playerId = line.substr(kPlayerPrefix.size(), usernameSeparator - kPlayerPrefix.size());
                    const auto username = line.substr(usernameSeparator + kUsernameSeparator.size());
                    if (IsValidPlayerIdText(playerId) && username.size() <= kMaximumPlayerNameLength &&
                        !HasControlCharacter(username)) {
                        const std::string entry = "[" + std::string(playerId) + "] " +
                                                  (username.empty() ? "<unknown>" : std::string(username)) + "\n";
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
    } catch (...) {
        return "No remote players";
    }
}

[[nodiscard]] bool ConnectToSkyrimTogether(
    RE::StaticFunctionTag*,
    std::string a_endpoint,
    std::string a_password)
{
    if (!IsValidEndpoint(a_endpoint) || !IsValidPassword(a_password))
        return false;

    try {
        const std::string config = "endpoint=" + a_endpoint + "\npassword=" + a_password + "\n";
        const std::string command = "action=connect\nendpoint=" + a_endpoint + "\npassword=" + a_password + "\n";
        return WriteAtomically(GetHandoffPath(kConfigFileName), config) &&
               WriteAtomically(GetHandoffPath(kCommandFileName), command);
    } catch (...) {
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
    try {
        std::ifstream file(GetHandoffPath(kConfigFileName));
        std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto value = GetStatusValue(contents, "endpoint");
        return IsValidEndpoint(value) ? std::string(value) : std::string{};
    } catch (...) {
        return {};
    }
}

[[nodiscard]] std::string GetConfiguredPassword(RE::StaticFunctionTag*)
{
    // Keep credentials write-only to Papyrus callers.
    return {};
}

[[nodiscard]] std::string GetStatusSummary(RE::StaticFunctionTag*)
{
    return GetConnectionState();
}

[[nodiscard]] bool DisconnectFromSkyrimTogether(RE::StaticFunctionTag*)
{
    return WriteAtomically(GetHandoffPath(kCommandFileName), "action=disconnect\n");
}

[[nodiscard]] bool IsSkyrimTogetherConnected(RE::StaticFunctionTag*)
{
    return IsConnected();
}

[[nodiscard]] std::string GetSkyrimTogetherConnectionState(RE::StaticFunctionTag*)
{
    return GetConnectionState();
}

[[nodiscard]] bool SetSkyrimTogetherConnectionConfig(
    RE::StaticFunctionTag*,
    std::string a_endpoint,
    std::string a_password)
{
    if (!IsValidEndpoint(a_endpoint) || !IsValidPassword(a_password))
        return false;

    try {
        const std::string config = "endpoint=" + a_endpoint + "\npassword=" + a_password + "\n";
        return WriteAtomically(GetHandoffPath(kConfigFileName), config);
    } catch (...) {
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
    a_vm->RegisterFunction("IsSkyrimTogetherConnected", kPapyrusScript, IsSkyrimTogetherConnected);
    a_vm->RegisterFunction("GetSkyrimTogetherConnectionState", kPapyrusScript, GetSkyrimTogetherConnectionState);
    a_vm->RegisterFunction("SetSkyrimTogetherConnectionConfig", kPapyrusScript, SetSkyrimTogetherConnectionConfig);
    a_vm->RegisterFunction("GetSkyrimTogetherConfiguredEndpoint", kPapyrusScript, GetConfiguredEndpoint);
    a_vm->RegisterFunction("GetSkyrimTogetherConfiguredPassword", kPapyrusScript, GetConfiguredPassword);
    a_vm->RegisterFunction("GetSkyrimTogetherStatusSummary", kPapyrusScript, GetStatusSummary);
    a_vm->RegisterFunction("GetSkyrimTogetherTelemetryReadout", kPapyrusScript, GetStatusSummary);
    a_vm->RegisterFunction("SendSkyrimTogetherChat", kPapyrusScript, SendSkyrimTogetherChat);
    a_vm->RegisterFunction("GetSkyrimTogetherPlayerList", kPapyrusScript, GetSkyrimTogetherPlayerList);
    a_vm->RegisterFunction("CreateSkyrimTogetherParty", kPapyrusScript, CreateSkyrimTogetherParty);
    a_vm->RegisterFunction("LeaveSkyrimTogetherParty", kPapyrusScript, LeaveSkyrimTogetherParty);
    a_vm->RegisterFunction("InviteSkyrimTogetherPartyMember", kPapyrusScript, InviteSkyrimTogetherPartyMember);
    a_vm->RegisterFunction("AcceptSkyrimTogetherPartyInvite", kPapyrusScript, AcceptSkyrimTogetherPartyInvite);
    a_vm->RegisterFunction("KickSkyrimTogetherPartyMember", kPapyrusScript, KickSkyrimTogetherPartyMember);
    a_vm->RegisterFunction("ChangeSkyrimTogetherPartyLeader", kPapyrusScript, ChangeSkyrimTogetherPartyLeader);
    return true;
}
} // namespace

bool RegisterPapyrusBindings() noexcept
{
    try {
        const auto* papyrus = SKSE::GetPapyrusInterface();
        return papyrus && papyrus->Register(RegisterPapyrusFunctions);
    } catch (...) {
        return false;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter
