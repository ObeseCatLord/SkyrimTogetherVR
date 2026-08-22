#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "PapyrusBindings.h"

namespace SkyrimTogetherVR::GameplayAdapter::VRControlMenu
{
enum class Page : std::uint8_t
{
    Main,
    Players,
    PlayerActions,
    Party,
    InvitePlayers,
    KickMembers,
    ChangeLeader,
    Invitations,
    InvitationActions,
    Admin,
    Time,
    TeleportToPlayer,
    AdminTeleportToPlayer,
    Chat
};

enum class Action : std::uint8_t
{
    None,
    ConnectConfigured,
    Disconnect,
    CreateParty,
    LeaveParty,
    InvitePlayer,
    AcceptInvite,
    DeclineInvite,
    KickPlayer,
    ChangeLeader,
    SetTime,
    TeleportToPlayer,
    AdminTeleportToPlayer,
    SendChat,
    Close
};

struct Player
{
    std::uint32_t Id{};
    std::string Name;
};

struct Invitation
{
    std::uint32_t InviterId{};
    std::string Name;
};

struct Snapshot
{
    bool Ready{};
    bool Online{};
    bool InParty{};
    bool IsLeader{};
    bool AdminCommandsAvailable{};
    std::uint32_t LocalPlayerId{};
    std::uint32_t LeaderPlayerId{};
    std::uint64_t LifecycleEpoch{};
    std::uint64_t ConnectionGeneration{};
    PapyrusBindingPolicy::CommandIdentity Identity;
    std::string LifecycleState;
    std::vector<Player> Players;
    std::vector<Player> PartyMembers;
    std::vector<Invitation> Invitations;
};

struct State
{
    Page CurrentPage{Page::Main};
    std::size_t PageOffset{};
    std::uint32_t SelectedPlayerId{};
};

struct Option
{
    std::string Label;
    State Next;
    Action Command{Action::None};
    std::uint32_t PlayerId{};
    std::uint8_t Hours{};
    std::uint8_t Minutes{};
    std::string TargetPlayer;
};

struct Model
{
    std::string Message;
    std::vector<Option> Options;
};

namespace Policy
{
constexpr std::size_t kPageSize = 5;
constexpr std::size_t kMaximumButtons = 8;
constexpr std::size_t kMaximumAdminTeleportTargetLength = 128;

// This is deliberately small and pure so terminal UI/keyboard failures can
// share the same generation invalidation rule as the native controller.
struct MenuSession
{
    bool Active{};
    std::uint64_t Generation{};
};

[[nodiscard]] constexpr std::uint64_t NextGeneration(const std::uint64_t a_generation) noexcept
{
    return a_generation == std::numeric_limits<std::uint64_t>::max() ? 1 : a_generation + 1;
}

[[nodiscard]] constexpr MenuSession ReleaseOnTerminalFailure(const MenuSession a_session, const std::uint64_t a_generation) noexcept
{
    if (!a_session.Active || a_session.Generation != a_generation)
        return a_session;
    return {
        false,
        NextGeneration(a_session.Generation),
    };
}

[[nodiscard]] constexpr bool IsValidPlayerId(const std::uint32_t a_playerId) noexcept
{
    return a_playerId != 0;
}

[[nodiscard]] inline bool IsValidAdminTeleportTarget(const std::string_view a_targetPlayer) noexcept
{
    return PapyrusBindingPolicy::IsSafeDisplayText(a_targetPlayer, kMaximumAdminTeleportTargetLength) &&
           a_targetPlayer != "<unknown>" && a_targetPlayer.front() != ' ' && a_targetPlayer.back() != ' ';
}

[[nodiscard]] inline bool IsValidAction(
    const Snapshot& a_snapshot, const Action a_action, const std::uint32_t a_playerId = 0,
    const std::string_view a_targetPlayer = {}) noexcept
{
    if (!a_snapshot.Ready || a_snapshot.LifecycleState.empty() || a_snapshot.Identity.LaunchNonce.empty())
        return false;
    const auto hasPlayer = [&]
    {
        return std::any_of(a_snapshot.Players.begin(), a_snapshot.Players.end(), [&](const Player& a_player) { return a_player.Id == a_playerId; });
    };
    const auto hasExactNamedPlayer = [&]
    {
        return IsValidAdminTeleportTarget(a_targetPlayer) &&
               std::any_of(a_snapshot.Players.begin(), a_snapshot.Players.end(), [&](const Player& a_player)
               { return a_player.Id == a_playerId && a_player.Name == a_targetPlayer; });
    };
    const auto hasMember = [&]
    {
        return std::any_of(a_snapshot.PartyMembers.begin(), a_snapshot.PartyMembers.end(), [&](const Player& a_player) { return a_player.Id == a_playerId; });
    };
    const auto hasInvitation = [&]
    {
        return std::any_of(a_snapshot.Invitations.begin(), a_snapshot.Invitations.end(), [&](const Invitation& a_invitation) { return a_invitation.InviterId == a_playerId; });
    };
    switch (a_action)
    {
    case Action::ConnectConfigured: return !a_snapshot.Online;
    case Action::Disconnect: return a_snapshot.Online && PapyrusBindingPolicy::IsOnlineIdentity(a_snapshot.Identity);
    case Action::CreateParty: return a_snapshot.Online && PapyrusBindingPolicy::IsOnlineIdentity(a_snapshot.Identity) && !a_snapshot.InParty;
    case Action::LeaveParty: return a_snapshot.Online && PapyrusBindingPolicy::IsOnlineIdentity(a_snapshot.Identity) && a_snapshot.InParty;
    case Action::InvitePlayer: return a_snapshot.Online && PapyrusBindingPolicy::IsOnlineIdentity(a_snapshot.Identity) && a_snapshot.InParty && a_snapshot.IsLeader && a_playerId != a_snapshot.LocalPlayerId && hasPlayer() && !hasMember();
    case Action::AcceptInvite:
    case Action::DeclineInvite: return a_snapshot.Online && PapyrusBindingPolicy::IsOnlineIdentity(a_snapshot.Identity) && !a_snapshot.InParty && hasInvitation();
    case Action::KickPlayer:
    case Action::ChangeLeader: return a_snapshot.Online && PapyrusBindingPolicy::IsOnlineIdentity(a_snapshot.Identity) && a_snapshot.InParty && a_snapshot.IsLeader && a_playerId != a_snapshot.LocalPlayerId && hasMember();
    case Action::SetTime: return a_snapshot.Online && PapyrusBindingPolicy::IsOnlineIdentity(a_snapshot.Identity) && a_snapshot.AdminCommandsAvailable;
    case Action::TeleportToPlayer: return a_snapshot.Online && PapyrusBindingPolicy::IsOnlineIdentity(a_snapshot.Identity) && a_playerId <= 0xffff && hasMember();
    case Action::AdminTeleportToPlayer:
        return a_snapshot.Online && PapyrusBindingPolicy::IsOnlineIdentity(a_snapshot.Identity) &&
               a_snapshot.AdminCommandsAvailable && a_playerId != a_snapshot.LocalPlayerId && hasExactNamedPlayer();
    case Action::SendChat: return a_snapshot.Online && PapyrusBindingPolicy::IsOnlineIdentity(a_snapshot.Identity);
    default: return false;
    }
}

[[nodiscard]] constexpr bool IsCompatibleForAction(const Snapshot& a_visible, const Snapshot& a_current) noexcept
{
    return a_visible.Ready && a_current.Ready && !a_visible.LifecycleState.empty() && a_visible.LifecycleState == a_current.LifecycleState &&
           a_visible.Identity.LaunchNonce == a_current.Identity.LaunchNonce &&
           a_visible.Identity.LifecycleEpoch == a_current.Identity.LifecycleEpoch &&
           a_visible.Identity.ConnectionGeneration == a_current.Identity.ConnectionGeneration &&
           a_visible.Identity.SessionId == a_current.Identity.SessionId &&
           a_visible.Identity.ServerInstanceNonce == a_current.Identity.ServerInstanceNonce;
}
[[nodiscard]] constexpr bool IsCurrentCallback(
    const std::uint64_t a_slotGeneration, const std::uint64_t a_visibleGeneration, const std::uint64_t a_activeGeneration, const std::uint8_t a_callbackSlot,
    const std::uint8_t a_visibleSlot) noexcept
{
    return a_slotGeneration != 0 && a_slotGeneration == a_visibleGeneration && a_visibleGeneration == a_activeGeneration && a_callbackSlot == a_visibleSlot;
}
[[nodiscard]] inline std::string BuildCommand(
    const Action a_action, const PapyrusBindingPolicy::CommandIdentity& a_identity, const std::uint32_t a_playerId = 0,
    const std::uint8_t a_hours = 0, const std::uint8_t a_minutes = 0, const std::string_view a_targetPlayer = {})
{
    const auto target = [&a_identity, a_playerId](const std::string_view a_action)
    {
        return PapyrusBindingPolicy::BuildOnlineCommand(a_action, a_identity, "playerId=" + std::to_string(a_playerId) + "\n");
    };
    switch (a_action)
    {
    case Action::Disconnect: return PapyrusBindingPolicy::BuildOnlineCommand("disconnect", a_identity);
    case Action::CreateParty: return PapyrusBindingPolicy::BuildOnlineCommand("party_create", a_identity);
    case Action::LeaveParty: return PapyrusBindingPolicy::BuildOnlineCommand("party_leave", a_identity);
    case Action::InvitePlayer: return target("party_invite");
    case Action::AcceptInvite: return target("party_accept");
    case Action::DeclineInvite: return target("party_decline");
    case Action::KickPlayer: return target("party_kick");
    case Action::ChangeLeader: return target("party_change_leader");
    case Action::SetTime: return PapyrusBindingPolicy::BuildOnlineCommand("set_time", a_identity, "hours=" + std::to_string(a_hours) + "\nminutes=" + std::to_string(a_minutes) + "\n");
    case Action::TeleportToPlayer: return target("teleport_to_player");
    case Action::AdminTeleportToPlayer:
        return IsValidAdminTeleportTarget(a_targetPlayer) ?
                   PapyrusBindingPolicy::BuildOnlineCommand("admin_teleport", a_identity, "targetPlayer=" + std::string(a_targetPlayer) + "\n") :
                   std::string{};
    default: return {};
    }
}

namespace detail
{
[[nodiscard]] inline std::string Label(const Player& a_player)
{
    constexpr std::size_t maximumLabelNameLength = 64;
    return "[" + std::to_string(a_player.Id) + "] " + a_player.Name.substr(0, maximumLabelNameLength);
}
[[nodiscard]] inline std::string Label(const Invitation& a_invitation)
{
    constexpr std::size_t maximumLabelNameLength = 64;
    return "[" + std::to_string(a_invitation.InviterId) + "] " + a_invitation.Name.substr(0, maximumLabelNameLength);
}
inline void AddBack(std::vector<Option>& a_options, State a_state)
{
    a_state.PageOffset = 0;
    a_options.push_back({"Back", a_state});
}
template <class T, class TSelect> inline void AddPaged(std::vector<Option>& a_options, const std::vector<T>& a_entries, const State& a_state, TSelect a_select)
{
    const auto start = std::min(a_state.PageOffset, a_entries.size());
    const auto end = std::min(start + kPageSize, a_entries.size());
    for (auto index = start; index < end; ++index)
        a_options.push_back(a_select(a_entries[index]));
    if (end < a_entries.size())
    {
        auto next = a_state;
        next.PageOffset = end;
        a_options.push_back({"More", next});
    }
    if (start != 0)
    {
        auto previous = a_state;
        previous.PageOffset = start > kPageSize ? start - kPageSize : 0;
        a_options.push_back({"Previous", previous});
    }
}
[[nodiscard]] inline std::string ListMessage(const std::string_view a_title, const std::size_t a_count)
{
    return std::string(a_title) + "\n\n" + (a_count == 0 ? "None available." : "Choose a player.");
}
} // namespace detail

[[nodiscard]] inline Model BuildModel(const Snapshot& a_snapshot, const State& a_state)
{
    Model model;
    const State mainState{};
    switch (a_state.CurrentPage)
    {
    case Page::Main:
        model.Message = "Skyrim Together VR\n\nConnection: " + std::string(a_snapshot.Online ? "online" : "offline") + "\nChoose a controller action.";
        if (a_snapshot.Online) {
            model.Options = {
                {"Disconnect", mainState, Action::Disconnect},
                {"Players", {Page::Players}},
                {"Party", {Page::Party}},
                {"Invitations", {Page::Invitations}},
                {"Admin and travel", {Page::Admin}},
                {"Chat", {Page::Chat}},
                {"Close", mainState, Action::Close}};
        } else {
            model.Options = {
                {"Connect configured", mainState, Action::ConnectConfigured},
                {"Close", mainState, Action::Close}};
        }
        break;
    case Page::Players:
        model.Message = detail::ListMessage("Online players", a_snapshot.Players.size());
        detail::AddPaged(model.Options, a_snapshot.Players, a_state, [](const Player& a_player) { return Option{detail::Label(a_player), {Page::PlayerActions, 0, a_player.Id}}; });
        detail::AddBack(model.Options, mainState);
        break;
    case Page::PlayerActions:
        model.Message = "Player actions\n\nSelected player: " + std::to_string(a_state.SelectedPlayerId);
        if (IsValidAction(a_snapshot, Action::TeleportToPlayer, a_state.SelectedPlayerId))
            model.Options.push_back({"Teleport to party member", mainState, Action::TeleportToPlayer, a_state.SelectedPlayerId});
        model.Options.push_back({"Back", {Page::Players}});
        break;
    case Page::Party:
        model.Message = a_snapshot.InParty ? "Party\n\nParty active." : "Party\n\nNo active party.";
        if (!a_snapshot.InParty)
            model.Options.push_back({"Create party", mainState, Action::CreateParty});
        else
        {
            model.Options.push_back({"Leave party", mainState, Action::LeaveParty});
            if (a_snapshot.IsLeader)
            {
                model.Options.push_back({"Invite player", {Page::InvitePlayers}});
                model.Options.push_back({"Kick member", {Page::KickMembers}});
                model.Options.push_back({"Change leader", {Page::ChangeLeader}});
            }
        }
        detail::AddBack(model.Options, mainState);
        break;
    case Page::InvitePlayers:
    {
        std::vector<Player> candidates;
        candidates.reserve(a_snapshot.Players.size());
        std::copy_if(a_snapshot.Players.begin(), a_snapshot.Players.end(), std::back_inserter(candidates), [&a_snapshot](const Player& a_player) {
            return IsValidAction(a_snapshot, Action::InvitePlayer, a_player.Id);
        });
        model.Message = detail::ListMessage("Invite player", candidates.size());
        detail::AddPaged(model.Options, candidates, a_state, [](const Player& a_player) { return Option{detail::Label(a_player), {}, Action::InvitePlayer, a_player.Id}; });
        detail::AddBack(model.Options, {Page::Party});
        break;
    }
    case Page::KickMembers:
    case Page::ChangeLeader:
        model.Message = detail::ListMessage(a_state.CurrentPage == Page::KickMembers ? "Kick party member" : "Choose new leader", a_snapshot.PartyMembers.size());
        detail::AddPaged(
            model.Options, a_snapshot.PartyMembers, a_state, [&a_state](const Player& a_player)
            { return Option{detail::Label(a_player), {}, a_state.CurrentPage == Page::KickMembers ? Action::KickPlayer : Action::ChangeLeader, a_player.Id}; });
        detail::AddBack(model.Options, {Page::Party});
        break;
    case Page::Invitations:
        model.Message = detail::ListMessage("Pending invitations", a_snapshot.Invitations.size());
        detail::AddPaged(
            model.Options, a_snapshot.Invitations, a_state,
            [](const Invitation& a_invitation) { return Option{detail::Label(a_invitation), {Page::InvitationActions, 0, a_invitation.InviterId}}; });
        detail::AddBack(model.Options, mainState);
        break;
    case Page::InvitationActions:
        model.Message = "Party invitation\n\nSelected inviter: " + std::to_string(a_state.SelectedPlayerId);
        model.Options = {
            {"Accept", mainState, Action::AcceptInvite, a_state.SelectedPlayerId},
            {"Decline", mainState, Action::DeclineInvite, a_state.SelectedPlayerId},
            {"Back", {Page::Invitations}}};
        break;
    case Page::Admin:
        model.Message = "Admin and travel\n\nServer validates admin requests.";
        if (a_snapshot.AdminCommandsAvailable) {
            model.Options.push_back({"Set time", {Page::Time}});
            model.Options.push_back({"Admin teleport to player", {Page::AdminTeleportToPlayer}});
        }
        model.Options.push_back({"Teleport to party member", {Page::TeleportToPlayer}});
        model.Options.push_back({"Back", mainState});
        break;
    case Page::Time:
        model.Message = "Set time\n\nChoose a fixed time preset.";
        model.Options = {
            {"Midnight", mainState, Action::SetTime, 0, 0, 0},
            {"Dawn (06:00)", mainState, Action::SetTime, 0, 6, 0},
            {"Noon (12:00)", mainState, Action::SetTime, 0, 12, 0},
            {"Dusk (18:00)", mainState, Action::SetTime, 0, 18, 0},
            {"Back", {Page::Admin}}};
        break;
    case Page::TeleportToPlayer:
        model.Message = detail::ListMessage("Teleport to party member", a_snapshot.PartyMembers.size());
        detail::AddPaged(
            model.Options, a_snapshot.PartyMembers, a_state, [](const Player& a_player)
            { return Option{detail::Label(a_player), {}, Action::TeleportToPlayer, a_player.Id}; });
        detail::AddBack(model.Options, {Page::Admin});
        break;
    case Page::AdminTeleportToPlayer:
    {
        std::vector<Player> candidates;
        candidates.reserve(a_snapshot.Players.size());
        std::copy_if(
            a_snapshot.Players.begin(), a_snapshot.Players.end(), std::back_inserter(candidates),
            [&a_snapshot](const Player& a_player)
            { return IsValidAction(a_snapshot, Action::AdminTeleportToPlayer, a_player.Id, a_player.Name); });
        model.Message = detail::ListMessage("Admin teleport to player", candidates.size());
        model.Message += "\n\nYou will be teleported to the selected player if the server authorizes this session.";
        detail::AddPaged(
            model.Options, candidates, a_state, [](const Player& a_player)
            {
                return Option{
                    detail::Label(a_player), {}, Action::AdminTeleportToPlayer, a_player.Id, 0, 0, a_player.Name};
            });
        detail::AddBack(model.Options, {Page::Admin});
        break;
    }
    case Page::Chat:
        model.Message = "Chat\n\nEnter a message with the VR runtime keyboard.";
        model.Options = {{"Enter message", mainState, Action::SendChat}, {"Back", mainState}};
        break;
    }
    return model;
}
} // namespace Policy

// Queues the controller-accessible native menu on SKSE's UI task queue.
// It is deliberately fire-and-forget because Papyrus must not retain menu state.
[[nodiscard]] bool Open() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::VRControlMenu
