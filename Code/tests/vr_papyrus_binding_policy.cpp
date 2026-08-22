#include <catch2/catch.hpp>

#include "../vr_gameplay_bridge/PapyrusBindings.h"

namespace
{
using namespace SkyrimTogetherVR::GameplayAdapter::PapyrusBindingPolicy;
}

TEST_CASE("configured Papyrus connection parsing keeps credentials internal", "[skyrim-vr][papyrus]")
{
    ConfiguredConnection connection;
    REQUIRE(ParseConfiguredConnection("endpoint=example.invalid:10578\npassword=stored-secret\n", connection));
    REQUIRE(connection.Endpoint == "example.invalid:10578");
    REQUIRE(connection.Password == "stored-secret");
    REQUIRE(BuildLaunchBoundConnectCommand(connection, "0123456789abcdef0123456789abcdef") ==
            "action=connect\nenvelope=launch_bound_connect\nlaunchNonce=0123456789abcdef0123456789abcdef\nlifecycleEpoch=0\nconnectionGeneration=0\nsessionId=0\nserverInstanceNonce=0\nendpoint=example.invalid:10578\npassword=stored-secret\n");

    REQUIRE_FALSE(ParseConfiguredConnection("endpoint=example.invalid:10578\n", connection));
    REQUIRE_FALSE(ParseConfiguredConnection("endpoint=one:1\nendpoint=two:2\npassword=stored-secret\n", connection));
    REQUIRE_FALSE(ParseConfiguredConnection("endpoint=example.invalid:10578\npassword=bad\tvalue\n", connection));
}

TEST_CASE("Papyrus command envelopes require one mutually consistent online identity", "[skyrim-vr][papyrus]")
{
    const std::string controls =
        "launchNonce=0123456789abcdef0123456789abcdef\nlifecycleEpoch=8\nconnectionGeneration=12\nsessionId=77\nserverInstanceNonce=99\n";
    const std::string status = controls + "online=1\n";
    CommandIdentity identity;
    REQUIRE(ParseConsistentSnapshotIdentity(controls, status, "0123456789abcdef0123456789abcdef", identity));
    REQUIRE(IsOnlineIdentity(identity));
    REQUIRE(BuildOnlineCommand("chat", identity, "message=hello\n") ==
            "action=chat\nenvelope=online\nlaunchNonce=0123456789abcdef0123456789abcdef\nlifecycleEpoch=8\nconnectionGeneration=12\nsessionId=77\nserverInstanceNonce=99\nmessage=hello\n");

    REQUIRE_FALSE(ParseConsistentSnapshotIdentity(controls, status + "sessionId=78\n", "0123456789abcdef0123456789abcdef", identity));
    REQUIRE_FALSE(ParseConsistentSnapshotIdentity(controls, "launchNonce=fedcba9876543210fedcba9876543210\nlifecycleEpoch=8\nconnectionGeneration=12\nsessionId=77\nserverInstanceNonce=99\n", "0123456789abcdef0123456789abcdef", identity));
    REQUIRE_FALSE(ParseConsistentSnapshotIdentity(controls, status, "fedcba9876543210fedcba9876543210", identity));
    identity.SessionId = 0;
    REQUIRE_FALSE(IsOnlineIdentity(identity));
    REQUIRE(BuildOnlineCommand("chat", identity, "message=hello\n").empty());
}

TEST_CASE("Papyrus telemetry is allowlisted, bounded, and credential-free", "[skyrim-vr][papyrus]")
{
    const auto readout = BuildTelemetryReadout(
        "state=online\nonline=1\ntransportOnline=1\nplayerId=42\nsessionId=100\nconnectionGeneration=7\n"
        "lifecycleState=ready\nlifecycleEpoch=9\nrehydrationState=ready\nclientVersion=abc123\n"
        "serverVersion=abc123\ngameplayProtocolRevision=20\npassword=stored-secret\nerror=untrusted arbitrary text\n",
        "bridge.ready=1\nbridge.endpointState=Ready\nbridge.activeCapabilities=123\nbridge.producedEvents=8\n"
        "bridge.consumedEvents=7\nbridge.rejectedCommands=1\nbridge.discardedEvents=0\npassword=stored-secret\n");

    REQUIRE(readout.find("Connection: online") != std::string::npos);
    REQUIRE(readout.find("Session: player=42 id=100 generation=7") != std::string::npos);
    REQUIRE(readout.find("Lifecycle: ready epoch=9 rehydration=ready") != std::string::npos);
    REQUIRE(readout.find("Bridge: ready=1 state=Ready capabilities=123") != std::string::npos);
    REQUIRE(readout.find("stored-secret") == std::string::npos);
    REQUIRE(readout.find("untrusted arbitrary text") == std::string::npos);
    REQUIRE(BuildTelemetryReadout({}, {}).find("Telemetry unavailable") != std::string::npos);
}

TEST_CASE("VR control snapshots expose valid party and invitation state only", "[skyrim-vr][papyrus]")
{
    const std::string snapshot =
        "ready=1\nonline=1\nlocalPlayerId=42\nchat.available=1\nadmin.enforcement=server_authoritative\n"
        "party.inParty=1\nparty.isLeader=1\nparty.leaderPlayerId=42\nplayer.7.name=Companion\n"
        "party.member.7=1\ninvite.8.expiryTick=123456\ninvite.8.name=Inviter\npassword=stored-secret\n";

    const auto players = BuildPlayersReadout(snapshot);
    const auto party = BuildPartyReadout(snapshot);
    const auto invites = BuildInviteReadout(snapshot);
    const auto controls = BuildControlReadout(snapshot);

    REQUIRE(players.find("[7] Companion") != std::string::npos);
    REQUIRE(party.find("Leader player ID: 42 (you)") != std::string::npos);
    REQUIRE(party.find("[7] Companion") != std::string::npos);
    REQUIRE(invites.find("[8] Inviter (expires tick 123456)") != std::string::npos);
    REQUIRE(controls.find("Admin commands: available; server authorizes each request.") != std::string::npos);
    REQUIRE(controls.find("stored-secret") == std::string::npos);
}

TEST_CASE("VR control snapshots reject malformed and unavailable entries", "[skyrim-vr][papyrus]")
{
    REQUIRE(BuildPartyReadout("ready=0\n") == "Party state unavailable.");
    REQUIRE(BuildInviteReadout("ready=1\ninvite.bad.expiryTick=1\n").find("<None>") != std::string::npos);
    REQUIRE(BuildPlayersReadout("ready=1\nplayer.5.name=bad\tname\n").find("<None>") != std::string::npos);
    REQUIRE(BuildControlReadout("ready=1\nonline=0\nadmin.enforcement=unexpected\n").find("Admin commands: unavailable.") != std::string::npos);
}

TEST_CASE("VR party control pages follow invitation, disconnect, and leader state transitions", "[skyrim-vr][papyrus]")
{
    const std::string invitationReceived =
        "ready=1\nonline=1\nparty.inParty=0\ninvite.12.expiryTick=9000\ninvite.12.name=Host\n";
    const std::string invitationExpiredOrDeclined = "ready=1\nonline=1\nparty.inParty=0\ninvite.count=0\n";
    const std::string invitationAccepted =
        "ready=1\nonline=1\nparty.inParty=1\nparty.isLeader=0\nparty.leaderPlayerId=12\nplayer.12.name=Host\nparty.member.12=1\n";
    const std::string leaderChanged =
        "ready=1\nonline=1\nparty.inParty=1\nparty.isLeader=1\nparty.leaderPlayerId=42\nplayer.12.name=Host\nparty.member.12=1\n";
    const std::string disconnected = "ready=1\nonline=0\nparty.inParty=0\ninvite.count=0\n";

    REQUIRE(BuildInviteReadout(invitationReceived).find("[12] Host") != std::string::npos);
    REQUIRE(BuildInviteReadout(invitationExpiredOrDeclined).find("<None>") != std::string::npos);
    REQUIRE(BuildPartyReadout(invitationAccepted).find("Leader player ID: 12") != std::string::npos);
    REQUIRE(BuildPartyReadout(leaderChanged).find("Leader player ID: 42 (you)") != std::string::npos);
    REQUIRE(BuildControlReadout(disconnected).find("Connection: offline") != std::string::npos);
}
