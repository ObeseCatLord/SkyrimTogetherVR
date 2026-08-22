#include "pch.h"

#include "VRControlMenu.h"

#include "PapyrusBindings.h"
#include "VRTextInput.h"

#include <RE/M/MessageBoxMenu.h>
#include <SKSE/API.h>
#include <vr_common/VRHandoffPath.h>

#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace SkyrimTogetherVR::GameplayAdapter::VRControlMenu
{
namespace
{
constexpr char kCommandFileName[] = "SkyrimTogetherVR.command";
constexpr char kConfigFileName[] = "SkyrimTogetherVR.connection";
constexpr char kControlsFileName[] = "SkyrimTogetherVR.controls";
constexpr char kStatusFileName[] = "SkyrimTogetherVR.status";
constexpr std::size_t kMaximumSnapshotBytes = 32 * 1024;
constexpr std::size_t kMaximumStatusBytes = 16 * 1024;
constexpr std::size_t kMaximumEntries = 64;
constexpr std::size_t kCallbackSlots = 32;

[[nodiscard]] bool ReadBounded(const std::filesystem::path& a_path, const std::size_t a_limit, std::string& a_out) noexcept
{
    try
    {
        std::ifstream file(a_path, std::ios::binary);
        if (!file)
            return false;

        a_out.clear();
        std::array<char, 256> buffer{};
        while (file)
        {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read = file.gcount();
            if (read <= 0)
                break;
            if (a_out.size() + static_cast<std::size_t>(read) > a_limit)
                return false;
            a_out.append(buffer.data(), static_cast<std::size_t>(read));
        }
        return !file.bad();
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool WriteAtomically(const std::filesystem::path& a_path, const std::string_view a_contents) noexcept
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

        const auto close = [&]()
        {
            CloseHandle(file);
        };
        const auto wrote = [&]()
        {
            const char* data = a_contents.data();
            std::size_t remaining = a_contents.size();
            while (remaining != 0)
            {
                const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, MAXDWORD));
                DWORD written{};
                if (!WriteFile(file, data, chunk, &written, nullptr) || written != chunk)
                    return false;
                data += written;
                remaining -= written;
            }
            return FlushFileBuffers(file) != FALSE;
        }();
        close();
        if (!wrote)
        {
            DeleteFileW(temporaryPath.c_str());
            return false;
        }
        // There is exactly one client-owned command slot. Never erase an
        // unconsumed action; report busy so the UI can retry after the poll.
        if (MoveFileExW(temporaryPath.c_str(), a_path.c_str(), MOVEFILE_WRITE_THROUGH))
            return true;
        DeleteFileW(temporaryPath.c_str());
    }
    catch (...)
    {
    }
    return false;
}

[[nodiscard]] bool ParseUnsigned(const std::string_view a_value, std::uint64_t& a_out) noexcept
{
    if (a_value.empty() || a_value.size() > 20 || !PapyrusBindingPolicy::IsDecimalValue(a_value))
        return false;
    const auto [position, error] = std::from_chars(a_value.data(), a_value.data() + a_value.size(), a_out);
    return error == std::errc{} && position == a_value.data() + a_value.size();
}

[[nodiscard]] std::string DisplayName(const std::string_view a_name) noexcept
{
    if (!PapyrusBindingPolicy::IsSafeDisplayText(a_name))
        return "<unknown>";
    return std::string(a_name);
}

[[nodiscard]] Snapshot ReadSnapshot() noexcept
{
    Snapshot snapshot;
    std::string controls;
    std::string status;
    if (!ReadBounded(SkyrimTogetherVR::Handoff::GetFile(kControlsFileName), kMaximumSnapshotBytes, controls) ||
        !ReadBounded(SkyrimTogetherVR::Handoff::GetFile(kStatusFileName), kMaximumStatusBytes, status))
        return snapshot;
    if (!PapyrusBindingPolicy::ParseConsistentSnapshotIdentity(
            controls, status, SkyrimTogetherVR::Handoff::GetLaunchNonce(), snapshot.Identity))
        return snapshot;

    const auto value = [&controls](const std::string_view a_key)
    {
        return PapyrusBindingPolicy::FindKeyValue(controls, a_key);
    };
    snapshot.Ready = value("ready") == "1";
    const auto controlsOnline = value("online");
    snapshot.InParty = value("party.inParty") == "1";
    snapshot.IsLeader = value("party.isLeader") == "1";
    snapshot.AdminCommandsAvailable = value("admin.enforcement") == "server_authoritative";

    std::uint64_t parsed{};
    if (ParseUnsigned(value("localPlayerId"), parsed) && parsed <= UINT32_MAX)
        snapshot.LocalPlayerId = static_cast<std::uint32_t>(parsed);
    if (ParseUnsigned(value("party.leaderPlayerId"), parsed) && parsed <= UINT32_MAX)
        snapshot.LeaderPlayerId = static_cast<std::uint32_t>(parsed);

    const auto statusValue = [&status](const std::string_view a_key)
    {
        return PapyrusBindingPolicy::FindKeyValue(status, a_key);
    };
    if ((controlsOnline != "0" && controlsOnline != "1") || statusValue("online") != controlsOnline)
        return Snapshot{};
    snapshot.Online = controlsOnline == "1";
    snapshot.LifecycleState = PapyrusBindingPolicy::IsSafeToken(statusValue("lifecycleState")) ? std::string(statusValue("lifecycleState")) : std::string{};
    snapshot.LifecycleEpoch = snapshot.Identity.LifecycleEpoch;
    snapshot.ConnectionGeneration = snapshot.Identity.ConnectionGeneration;

    std::size_t offset{};
    while (offset < controls.size() && snapshot.Players.size() < kMaximumEntries)
    {
        const auto end = controls.find('\n', offset);
        auto line = std::string_view(controls).substr(offset, end == std::string::npos ? controls.size() - offset : end - offset);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        constexpr std::string_view prefix{"player."};
        constexpr std::string_view suffix{".name="};
        if (line.starts_with(prefix))
        {
            const auto separator = line.find(suffix, prefix.size());
            if (separator != std::string_view::npos)
            {
                std::uint64_t id{};
                if (ParseUnsigned(line.substr(prefix.size(), separator - prefix.size()), id) && id <= UINT32_MAX && Policy::IsValidPlayerId(static_cast<std::uint32_t>(id)))
                    snapshot.Players.push_back({static_cast<std::uint32_t>(id), DisplayName(line.substr(separator + suffix.size()))});
            }
        }
        if (end == std::string::npos)
            break;
        offset = end + 1;
    }
    std::sort(snapshot.Players.begin(), snapshot.Players.end(), [](const Player& a_left, const Player& a_right) { return a_left.Id < a_right.Id; });
    snapshot.Players.erase(
        std::unique(snapshot.Players.begin(), snapshot.Players.end(), [](const Player& a_left, const Player& a_right) { return a_left.Id == a_right.Id; }), snapshot.Players.end());

    offset = 0;
    while (offset < controls.size() && snapshot.PartyMembers.size() < kMaximumEntries)
    {
        const auto end = controls.find('\n', offset);
        auto line = std::string_view(controls).substr(offset, end == std::string::npos ? controls.size() - offset : end - offset);
        constexpr std::string_view prefix{"party.member."};
        if (line.starts_with(prefix) && line.ends_with("=1"))
        {
            std::uint64_t id{};
            const auto idText = line.substr(prefix.size(), line.size() - prefix.size() - 2);
            if (ParseUnsigned(idText, id) && id <= UINT32_MAX && Policy::IsValidPlayerId(static_cast<std::uint32_t>(id)) &&
                static_cast<std::uint32_t>(id) != snapshot.LocalPlayerId)
                snapshot.PartyMembers.push_back({static_cast<std::uint32_t>(id), DisplayName(value("player." + std::string(idText) + ".name"))});
        }
        if (end == std::string::npos)
            break;
        offset = end + 1;
    }
    std::sort(snapshot.PartyMembers.begin(), snapshot.PartyMembers.end(), [](const Player& a_left, const Player& a_right) { return a_left.Id < a_right.Id; });

    offset = 0;
    while (offset < controls.size() && snapshot.Invitations.size() < kMaximumEntries)
    {
        const auto end = controls.find('\n', offset);
        auto line = std::string_view(controls).substr(offset, end == std::string::npos ? controls.size() - offset : end - offset);
        constexpr std::string_view prefix{"invite."};
        constexpr std::string_view suffix{".expiryTick="};
        if (line.starts_with(prefix))
        {
            const auto separator = line.find(suffix, prefix.size());
            std::uint64_t id{};
            std::uint64_t expiry{};
            if (separator != std::string_view::npos && ParseUnsigned(line.substr(prefix.size(), separator - prefix.size()), id) && id <= UINT32_MAX &&
                ParseUnsigned(line.substr(separator + suffix.size()), expiry) && Policy::IsValidPlayerId(static_cast<std::uint32_t>(id)))
                snapshot.Invitations.push_back({static_cast<std::uint32_t>(id), DisplayName(value("invite." + std::to_string(id) + ".name"))});
        }
        if (end == std::string::npos)
            break;
        offset = end + 1;
    }
    std::sort(snapshot.Invitations.begin(), snapshot.Invitations.end(), [](const Invitation& a_left, const Invitation& a_right) { return a_left.InviterId < a_right.InviterId; });
    return snapshot;
}

[[nodiscard]] bool QueueNativeCommand(
    const Action a_action, const PapyrusBindingPolicy::CommandIdentity& a_identity, const std::uint32_t a_playerId,
    const std::uint8_t a_hours, const std::uint8_t a_minutes, const std::string_view a_targetPlayer) noexcept
{
    try
    {
        const auto commandPath = SkyrimTogetherVR::Handoff::GetFile(kCommandFileName);
        if (a_action == Action::ConnectConfigured)
        {
            std::string config;
            PapyrusBindingPolicy::ConfiguredConnection connection;
            return ReadBounded(SkyrimTogetherVR::Handoff::GetFile(kConfigFileName), PapyrusBindingPolicy::kMaximumConfigurationLength, config) &&
                   PapyrusBindingPolicy::ParseConfiguredConnection(config, connection) &&
                   WriteAtomically(commandPath, PapyrusBindingPolicy::BuildLaunchBoundConnectCommand(connection, a_identity.LaunchNonce));
        }
        const auto command = Policy::BuildCommand(a_action, a_identity, a_playerId, a_hours, a_minutes, a_targetPlayer);
        return !command.empty() && WriteAtomically(commandPath, command);
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool QueueChatCommand(const std::string_view a_message, const PapyrusBindingPolicy::CommandIdentity& a_identity) noexcept
{
    if (a_message.empty() || a_message.size() > VRTextInput::kChatMaximumText || PapyrusBindingPolicy::HasControlCharacter(a_message))
        return false;
    const auto command = PapyrusBindingPolicy::BuildOnlineCommand("chat", a_identity, "message=" + std::string(a_message) + "\n");
    return !command.empty() && WriteAtomically(SkyrimTogetherVR::Handoff::GetFile(kCommandFileName), command);
}

class Controller
{
public:
    [[nodiscard]] bool Open() noexcept
    {
        std::uint64_t generation{};
        {
            std::lock_guard lock(m_lock);
            // MessageBoxMenu's legacy callback carries only one byte. Keep a
            // strict single-outstanding-menu invariant so a callback can never
            // survive long enough for its 5-bit slot to be reused.
            if (m_active)
                return false;
            m_active = true;
            m_state = {};
            generation = m_generation = Policy::NextGeneration(m_generation);
        }
        SKSE::log::info("SkyrimTogetherVR control menu open requested");
        return QueueRender(generation);
    }

    void Render(const std::uint64_t a_generation) noexcept
    {
        State state;
        {
            std::lock_guard lock(m_lock);
            if (!m_active || a_generation != m_generation)
                return;
            state = m_state;
        }

        const auto snapshot = ReadSnapshot();
        auto model = Policy::BuildModel(snapshot, state);
        if (model.Options.empty() || model.Options.size() > Policy::kMaximumButtons)
        {
            SKSE::log::warn("SkyrimTogetherVR control menu rejected an invalid page model");
            Invalidate(a_generation);
            return;
        }

        const auto slot = static_cast<std::uint8_t>(a_generation % kCallbackSlots);
        {
            std::lock_guard lock(m_lock);
            if (!m_active || a_generation != m_generation)
                return;
            m_visibleSnapshot = snapshot;
            m_visibleOptions = model.Options;
            m_visibleGeneration = a_generation;
            m_visibleSlot = slot;
            m_slotGenerations[slot] = a_generation;
        }

        const auto offset = static_cast<std::uint8_t>(slot * Policy::kMaximumButtons);
        bool created{};
        try
        {
            const auto& options = model.Options;
            switch (options.size())
            {
            case 1: created = RE::MessageBoxMenu::Create(model.Message.c_str(), &Controller::OnButton, offset, 0, 10, options[0].Label.c_str()); break;
            case 2: created = RE::MessageBoxMenu::Create(model.Message.c_str(), &Controller::OnButton, offset, 0, 10, options[0].Label.c_str(), options[1].Label.c_str()); break;
            case 3:
                created = RE::MessageBoxMenu::Create(
                    model.Message.c_str(), &Controller::OnButton, offset, 0, 10, options[0].Label.c_str(), options[1].Label.c_str(), options[2].Label.c_str());
                break;
            case 4:
                created = RE::MessageBoxMenu::Create(
                    model.Message.c_str(), &Controller::OnButton, offset, 0, 10, options[0].Label.c_str(), options[1].Label.c_str(), options[2].Label.c_str(),
                    options[3].Label.c_str());
                break;
            case 5:
                created = RE::MessageBoxMenu::Create(
                    model.Message.c_str(), &Controller::OnButton, offset, 0, 10, options[0].Label.c_str(), options[1].Label.c_str(), options[2].Label.c_str(),
                    options[3].Label.c_str(), options[4].Label.c_str());
                break;
            case 6:
                created = RE::MessageBoxMenu::Create(
                    model.Message.c_str(), &Controller::OnButton, offset, 0, 10, options[0].Label.c_str(), options[1].Label.c_str(), options[2].Label.c_str(),
                    options[3].Label.c_str(), options[4].Label.c_str(), options[5].Label.c_str());
                break;
            case 7:
                created = RE::MessageBoxMenu::Create(
                    model.Message.c_str(), &Controller::OnButton, offset, 0, 10, options[0].Label.c_str(), options[1].Label.c_str(), options[2].Label.c_str(),
                    options[3].Label.c_str(), options[4].Label.c_str(), options[5].Label.c_str(), options[6].Label.c_str());
                break;
            case 8:
                created = RE::MessageBoxMenu::Create(
                    model.Message.c_str(), &Controller::OnButton, offset, 0, 10, options[0].Label.c_str(), options[1].Label.c_str(), options[2].Label.c_str(),
                    options[3].Label.c_str(), options[4].Label.c_str(), options[5].Label.c_str(), options[6].Label.c_str(), options[7].Label.c_str());
                break;
            default: break;
            }
        }
        catch (...)
        {
            created = false;
        }
        if (!created)
        {
            Invalidate(a_generation);
            SKSE::log::warn("SkyrimTogetherVR control menu failed to create a message box");
        }
    }

private:
    static void OnButton(const std::uint8_t a_encoded) noexcept
    {
        Get().HandleButton(static_cast<std::uint8_t>(a_encoded / Policy::kMaximumButtons), static_cast<std::uint8_t>(a_encoded % Policy::kMaximumButtons));
    }

    void HandleButton(const std::uint8_t a_slot, const std::uint8_t a_index) noexcept
    {
        Option option;
        Snapshot visible;
        std::uint64_t generation{};
        {
            std::lock_guard lock(m_lock);
            if (!m_active || a_slot >= m_slotGenerations.size() ||
                !Policy::IsCurrentCallback(m_slotGenerations[a_slot], m_visibleGeneration, m_generation, a_slot, m_visibleSlot) || a_index >= m_visibleOptions.size())
                return;
            option = m_visibleOptions[a_index];
            visible = m_visibleSnapshot;
            generation = m_visibleGeneration;
        }

        if (option.Command == Action::Close)
        {
            VRTextInput::RequestContext chatContext;
            {
                std::lock_guard lock(m_lock);
                if (m_active && m_generation == generation && m_visibleGeneration == generation)
                {
                    chatContext = {m_chatGeneration, m_chatSnapshot.Identity.SessionId};
                    m_active = false;
                    m_generation = Policy::NextGeneration(m_generation);
                    m_chatGeneration = 0;
                    m_chatSnapshot = {};
                    SKSE::log::info("SkyrimTogetherVR control menu closed");
                }
            }
            if (chatContext.Generation != 0)
                VRTextInput::Get().CancelQueued(chatContext);
            return;
        }

        if (option.Command == Action::None)
        {
            std::uint64_t nextGeneration{};
            {
                std::lock_guard lock(m_lock);
                if (!m_active || m_generation != generation || m_visibleGeneration != generation)
                    return;
                m_state = option.Next;
                nextGeneration = m_generation = Policy::NextGeneration(m_generation);
            }
            QueueRender(nextGeneration);
            return;
        }

        const auto current = ReadSnapshot();
        if (!Policy::IsCompatibleForAction(visible, current) ||
            !Policy::IsValidAction(current, option.Command, option.PlayerId, option.TargetPlayer))
        {
            SKSE::log::warn("SkyrimTogetherVR control menu rejected a stale or unauthorized action");
            ReopenMain(generation);
            return;
        }

        if (option.Command == Action::SendChat)
        {
            std::uint64_t chatGeneration{};
            {
                std::lock_guard lock(m_lock);
                if (!m_active || m_generation != generation || m_visibleGeneration != generation)
                    return;
                chatGeneration = m_generation = Policy::NextGeneration(m_generation);
                m_slotGenerations.fill(0);
                m_visibleSnapshot = {};
                m_visibleOptions.clear();
                m_visibleGeneration = 0;
                m_visibleSlot = 0;
                // A MessageBox callback is not trusted to be the command-pump
                // owner. It only reserves work; Pump starts the OpenVR call.
                m_chatGeneration = chatGeneration;
                m_chatSnapshot = current;
                m_chatStartPending = true;
            }
            const auto callback = [chatGeneration](VRTextInput::Completion a_completion)
            {
                Get().HandleChatCompletion(chatGeneration, std::move(a_completion));
            };
            const auto request = VRTextInput::RequestContext{chatGeneration, current.Identity.SessionId};
            const auto requested = VRTextInput::Get().QueueText(
                "Skyrim Together VR chat", {}, VRTextInput::kChatMaximumText, request, callback);
            {
                std::lock_guard lock(m_lock);
                if (m_chatGeneration == chatGeneration)
                    m_chatStartPending = false;
            }
            if (!requested)
            {
                SKSE::log::warn("SkyrimTogetherVR control menu could not start VR chat text input");
                Invalidate(chatGeneration);
            }
            return;
        }

        const auto accepted = QueueNativeCommand(
            option.Command, current.Identity, option.PlayerId, option.Hours, option.Minutes, option.TargetPlayer);
        if (accepted)
            SKSE::log::info("SkyrimTogetherVR control menu queued action {}", static_cast<unsigned>(option.Command));
        else
            SKSE::log::warn("SkyrimTogetherVR control menu action {} was not queued", static_cast<unsigned>(option.Command));
        ReopenMain(generation);
    }

    void HandleChatCompletion(const std::uint64_t a_generation, VRTextInput::Completion a_completion) noexcept
    {
        Snapshot initiatingSnapshot;
        {
            std::lock_guard lock(m_lock);
            if (!m_active || m_generation != a_generation || m_chatGeneration != a_generation || m_chatStartPending)
                return;
            initiatingSnapshot = m_chatSnapshot;
        }

        if (a_completion.Status == VRTextInput::Result::Completed)
        {
            const auto current = ReadSnapshot();
            if (Policy::IsCompatibleForAction(initiatingSnapshot, current) && Policy::IsValidAction(current, Action::SendChat) &&
                QueueChatCommand(a_completion.Text, initiatingSnapshot.Identity))
                SKSE::log::info("SkyrimTogetherVR control menu queued chat text");
            else
                SKSE::log::warn("SkyrimTogetherVR control menu rejected completed chat text");
        }
        else if (a_completion.Status != VRTextInput::Result::Cancelled)
        {
            SKSE::log::warn(
                "SkyrimTogetherVR control menu VR chat input ended with status {}",
                static_cast<unsigned>(a_completion.Status));
        }
        ReopenMain(a_generation);
    }

    void ReopenMain(const std::uint64_t a_generation) noexcept
    {
        std::uint64_t nextGeneration{};
        {
            std::lock_guard lock(m_lock);
            if (!m_active || m_generation != a_generation || (m_visibleGeneration != a_generation && m_chatGeneration != a_generation))
                return;
            m_state = {};
            m_chatGeneration = 0;
            m_chatSnapshot = {};
            m_chatStartPending = false;
            nextGeneration = m_generation = Policy::NextGeneration(m_generation);
        }
        QueueRender(nextGeneration);
    }

    void Invalidate(const std::uint64_t a_generation) noexcept
    {
        VRTextInput::RequestContext chatContext;
        {
            std::lock_guard lock(m_lock);
            const auto released = Policy::ReleaseOnTerminalFailure({m_active, m_generation}, a_generation);
            if (released.Active == m_active && released.Generation == m_generation)
                return;
            m_active = released.Active;
            m_generation = released.Generation;
            m_slotGenerations.fill(0);
            m_visibleSnapshot = {};
            m_visibleOptions.clear();
            m_visibleGeneration = 0;
            m_visibleSlot = 0;
            m_state = {};
            chatContext = {m_chatGeneration, m_chatSnapshot.Identity.SessionId};
            m_chatGeneration = 0;
            m_chatSnapshot = {};
            m_chatStartPending = false;
        }
        if (chatContext.Generation != 0)
            VRTextInput::Get().CancelQueued(chatContext);
    }

    [[nodiscard]] bool QueueRender(const std::uint64_t a_generation) noexcept
    {
        try
        {
            const auto* task = SKSE::GetTaskInterface();
            if (!task)
            {
                SKSE::log::warn("SkyrimTogetherVR control menu has no SKSE task interface");
                Invalidate(a_generation);
                return false;
            }
            task->AddUITask([a_generation] { Get().Render(a_generation); });
            return true;
        }
        catch (...)
        {
            SKSE::log::warn("SkyrimTogetherVR control menu could not queue a UI task");
            Invalidate(a_generation);
            return false;
        }
    }

public:
    static Controller& Get() noexcept
    {
        static Controller controller;
        return controller;
    }

private:
    std::mutex m_lock;
    std::array<std::uint64_t, kCallbackSlots> m_slotGenerations{};
    Snapshot m_visibleSnapshot;
    std::vector<Option> m_visibleOptions;
    State m_state;
    std::uint64_t m_generation{};
    std::uint64_t m_visibleGeneration{};
    std::uint64_t m_chatGeneration{};
    std::uint8_t m_visibleSlot{};
    Snapshot m_chatSnapshot;
    bool m_chatStartPending{};
    bool m_active{};
};

[[nodiscard]] Controller& GetController() noexcept
{
    return Controller::Get();
}
} // namespace

bool Open() noexcept
{
    return GetController().Open();
}
} // namespace SkyrimTogetherVR::GameplayAdapter::VRControlMenu
