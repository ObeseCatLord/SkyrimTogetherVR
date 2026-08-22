#include <catch2/catch.hpp>

#include "../vr_gameplay_bridge/VRControlMenu.h"

namespace
{
using namespace SkyrimTogetherVR::GameplayAdapter::VRControlMenu;
using namespace SkyrimTogetherVR::GameplayAdapter::VRControlMenu::Policy;

Snapshot OnlineLeader()
{
    Snapshot snapshot;
    snapshot.Ready = true;
    snapshot.Online = true;
    snapshot.InParty = true;
    snapshot.IsLeader = true;
    snapshot.AdminCommandsAvailable = true;
    snapshot.LocalPlayerId = 1;
    snapshot.LifecycleState = "ready";
    snapshot.LifecycleEpoch = 8;
    snapshot.ConnectionGeneration = 12;
    snapshot.Identity = {"0123456789abcdef0123456789abcdef", 8, 12, 77, 99};
    snapshot.Players = {{2, "Aela"}, {3, "Farkas"}, {4, "Vilkas"}, {5, "Lydia"}, {6, "Erik"}, {7, "Jenassa"}};
    snapshot.PartyMembers = {{2, "Aela"}, {3, "Farkas"}};
    snapshot.Invitations = {{4, "Vilkas"}};
    return snapshot;
}
} // namespace

TEST_CASE("VR control menu routes only validated native commands", "[skyrim-vr][control-menu]")
{
    auto snapshot = OnlineLeader();
    REQUIRE(IsValidAction(snapshot, Action::InvitePlayer, 4));
    REQUIRE_FALSE(IsValidAction(snapshot, Action::InvitePlayer, 2));
    REQUIRE_FALSE(IsValidAction(snapshot, Action::InvitePlayer, 1));
    REQUIRE(IsValidAction(snapshot, Action::KickPlayer, 2));
    REQUIRE_FALSE(IsValidAction(snapshot, Action::KickPlayer, 4));
    REQUIRE(IsValidAction(snapshot, Action::TeleportToPlayer, 2));
    REQUIRE_FALSE(IsValidAction(snapshot, Action::TeleportToPlayer, 7));
    REQUIRE_FALSE(IsValidAction(snapshot, Action::TeleportToPlayer, 0));
    REQUIRE(IsValidAction(snapshot, Action::AdminTeleportToPlayer, 7, "Jenassa"));
    REQUIRE_FALSE(IsValidAction(snapshot, Action::AdminTeleportToPlayer, 7, "Lydia"));
    REQUIRE_FALSE(IsValidAction(snapshot, Action::AdminTeleportToPlayer, 1, "Local Player"));
    REQUIRE(BuildCommand(Action::InvitePlayer, snapshot.Identity, 4) ==
            "action=party_invite\nenvelope=online\nlaunchNonce=0123456789abcdef0123456789abcdef\nlifecycleEpoch=8\nconnectionGeneration=12\nsessionId=77\nserverInstanceNonce=99\nplayerId=4\n");
    REQUIRE(BuildCommand(Action::SetTime, snapshot.Identity, 0, 18, 0).find("action=set_time\nenvelope=online\n") == 0);
    REQUIRE(BuildCommand(Action::TeleportToPlayer, snapshot.Identity, 2).find("action=teleport_to_player\n") == 0);
    REQUIRE(BuildCommand(Action::AdminTeleportToPlayer, snapshot.Identity, 7, 0, 0, "Jenassa") ==
            "action=admin_teleport\nenvelope=online\nlaunchNonce=0123456789abcdef0123456789abcdef\nlifecycleEpoch=8\nconnectionGeneration=12\nsessionId=77\nserverInstanceNonce=99\ntargetPlayer=Jenassa\n");
    REQUIRE(BuildCommand(Action::AdminTeleportToPlayer, snapshot.Identity, 7, 0, 0, "bad\nname").empty());
    REQUIRE(BuildCommand(
                Action::AdminTeleportToPlayer, snapshot.Identity, 7, 0, 0,
                std::string(kMaximumAdminTeleportTargetLength + 1, 'x')).empty());
}

TEST_CASE("VR control menu bounds controller list pages and retains back navigation", "[skyrim-vr][control-menu]")
{
    const auto snapshot = OnlineLeader();
    const auto first = BuildModel(snapshot, {Page::Players});
    REQUIRE(first.Options.size() == kPageSize + 2);
    REQUIRE(first.Options[0].Label == "[2] Aela");
    REQUIRE(first.Options[kPageSize].Label == "More");
    REQUIRE(first.Options.back().Label == "Back");

    const auto second = BuildModel(snapshot, {Page::Players, kPageSize});
    REQUIRE(second.Options[0].Label == "[7] Jenassa");
    REQUIRE(second.Options[1].Label == "Previous");
    REQUIRE(second.Options.back().Label == "Back");
    REQUIRE(second.Options.size() <= kMaximumButtons);
}

TEST_CASE("VR control menu rejects stale lifecycle snapshots and invalid invitation transitions", "[skyrim-vr][control-menu]")
{
    const auto visible = OnlineLeader();
    auto changedLifecycle = visible;
    ++changedLifecycle.LifecycleEpoch;
    ++changedLifecycle.Identity.LifecycleEpoch;
    REQUIRE_FALSE(IsCompatibleForAction(visible, changedLifecycle));

    auto changedConnection = visible;
    ++changedConnection.ConnectionGeneration;
    ++changedConnection.Identity.ConnectionGeneration;
    REQUIRE_FALSE(IsCompatibleForAction(visible, changedConnection));
    auto reconnectedWithReusedPlayerId = visible;
    ++reconnectedWithReusedPlayerId.Identity.SessionId;
    REQUIRE_FALSE(IsCompatibleForAction(visible, reconnectedWithReusedPlayerId));
    auto changedServer = visible;
    ++changedServer.Identity.ServerInstanceNonce;
    REQUIRE_FALSE(IsCompatibleForAction(visible, changedServer));
    auto changedLaunch = visible;
    changedLaunch.Identity.LaunchNonce = "fedcba9876543210fedcba9876543210";
    REQUIRE_FALSE(IsCompatibleForAction(visible, changedLaunch));

    auto renamedPlayer = visible;
    renamedPlayer.Players.back().Name = "ReusedIdAfterRename";
    REQUIRE(IsCompatibleForAction(visible, renamedPlayer));
    REQUIRE_FALSE(IsValidAction(renamedPlayer, Action::AdminTeleportToPlayer, 7, "Jenassa"));

    const auto chatEnvelope = PapyrusBindingPolicy::BuildOnlineCommand("chat", visible.Identity, "message=delayed\n");
    REQUIRE(chatEnvelope.find("sessionId=77\n") != std::string::npos);
    REQUIRE_FALSE(IsCompatibleForAction(visible, reconnectedWithReusedPlayerId));
    REQUIRE(IsCompatibleForAction(visible, visible));
    REQUIRE(IsCurrentCallback(15, 15, 15, 3, 3));
    REQUIRE_FALSE(IsCurrentCallback(14, 15, 15, 3, 3));
    REQUIRE_FALSE(IsCurrentCallback(15, 15, 16, 3, 3));
    REQUIRE_FALSE(IsCurrentCallback(15, 15, 15, 2, 3));

    auto noParty = visible;
    noParty.InParty = false;
    noParty.IsLeader = false;
    REQUIRE(IsValidAction(noParty, Action::AcceptInvite, 4));
    noParty.Invitations.clear();
    REQUIRE_FALSE(IsValidAction(noParty, Action::AcceptInvite, 4));
}

TEST_CASE("VR control menu terminal failures release the current session and reject its callbacks", "[skyrim-vr][control-menu]")
{
    const MenuSession active{true, 41};
    const auto released = ReleaseOnTerminalFailure(active, 41);
    REQUIRE_FALSE(released.Active);
    REQUIRE(released.Generation == 42);
    REQUIRE_FALSE(IsCurrentCallback(41, 41, released.Generation, 9, 9));

    REQUIRE(ReleaseOnTerminalFailure(active, 40).Active);
    REQUIRE(ReleaseOnTerminalFailure(active, 40).Generation == 41);
    REQUIRE(ReleaseOnTerminalFailure(released, 41).Generation == 42);

    const auto wrapped = ReleaseOnTerminalFailure({true, std::numeric_limits<std::uint64_t>::max()}, std::numeric_limits<std::uint64_t>::max());
    REQUIRE_FALSE(wrapped.Active);
    REQUIRE(wrapped.Generation == 1);
}

TEST_CASE("VR control menu exposes a bounded main route and native VR chat input", "[skyrim-vr][control-menu]")
{
    const auto snapshot = OnlineLeader();
    const auto main = BuildModel(snapshot, {});
    REQUIRE(main.Options.size() == 7);
    REQUIRE(main.Options[0].Command == Action::Disconnect);
    REQUIRE(std::none_of(main.Options.begin(), main.Options.end(), [](const Option& a_option) {
        return a_option.Command == Action::ConnectConfigured;
    }));

    const auto chat = BuildModel(snapshot, {Page::Chat});
    REQUIRE(chat.Message.find("VR runtime keyboard") != std::string::npos);
    REQUIRE(chat.Options.size() == 2);
    REQUIRE(chat.Options.front().Command == Action::SendChat);
    REQUIRE(IsValidAction(snapshot, Action::SendChat));
    auto offline = snapshot;
    offline.Online = false;
    REQUIRE_FALSE(IsValidAction(offline, Action::SendChat));
    const auto offlineMain = BuildModel(offline, {});
    REQUIRE(offlineMain.Options.size() == 2);
    REQUIRE(offlineMain.Options[0].Command == Action::ConnectConfigured);
    REQUIRE(offlineMain.Options[1].Command == Action::Close);
}

TEST_CASE("VR control menu hides server-admin operations without validated permission", "[skyrim-vr][control-menu]")
{
    auto snapshot = OnlineLeader();
    snapshot.AdminCommandsAvailable = false;
    const auto admin = BuildModel(snapshot, {Page::Admin});
    REQUIRE(admin.Options.size() == 2);
    REQUIRE(admin.Options[0].Next.CurrentPage == Page::TeleportToPlayer);
    REQUIRE(admin.Options[1].Label == "Back");
    REQUIRE(admin.Options[0].Label == "Teleport to party member");
    REQUIRE(std::none_of(admin.Options.begin(), admin.Options.end(), [](const Option& a_option) {
        return a_option.Label.find("player to me") != std::string::npos;
    }));
}

TEST_CASE("VR control menu keeps party travel distinct from server-authorized admin teleport", "[skyrim-vr][control-menu]")
{
    const auto snapshot = OnlineLeader();
    const auto admin = BuildModel(snapshot, {Page::Admin});
    REQUIRE(admin.Options.size() == 4);
    REQUIRE(admin.Options[0].Next.CurrentPage == Page::Time);
    REQUIRE(admin.Options[1].Label == "Admin teleport to player");
    REQUIRE(admin.Options[1].Next.CurrentPage == Page::AdminTeleportToPlayer);
    REQUIRE(admin.Options[2].Label == "Teleport to party member");
    REQUIRE(admin.Options[2].Next.CurrentPage == Page::TeleportToPlayer);

    const auto adminTargets = BuildModel(snapshot, {Page::AdminTeleportToPlayer});
    REQUIRE(adminTargets.Options.front().Command == Action::AdminTeleportToPlayer);
    REQUIRE(adminTargets.Options.front().PlayerId == 2);
    REQUIRE(adminTargets.Options.front().TargetPlayer == "Aela");
    REQUIRE(adminTargets.Message.find("server authorizes") != std::string::npos);

    const auto partyTargets = BuildModel(snapshot, {Page::TeleportToPlayer});
    REQUIRE(partyTargets.Options.front().Command == Action::TeleportToPlayer);
    REQUIRE(partyTargets.Options.front().TargetPlayer.empty());
    const auto partyMemberActions = BuildModel(snapshot, {Page::PlayerActions, 0, 2});
    REQUIRE(partyMemberActions.Options.front().Label == "Teleport to party member");
    REQUIRE(std::none_of(admin.Options.begin(), admin.Options.end(), [](const Option& a_option) {
        return a_option.Label.find("player to me") != std::string::npos;
    }));
}
