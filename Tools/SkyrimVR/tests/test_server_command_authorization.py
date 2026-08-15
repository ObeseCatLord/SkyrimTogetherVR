#!/usr/bin/env python3
"""Source-level authorization contracts for server time and teleport commands."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
COMMAND_SERVICE = ROOT / "Code" / "server" / "Services" / "CommandService.cpp"
OVERLAY_SERVICE = ROOT / "Code" / "server" / "Services" / "OverlayService.cpp"


class ServerCommandAuthorizationTests(unittest.TestCase):
    def test_set_time_uses_sender_connection_not_spoofable_packet_player_id(self) -> None:
        source = COMMAND_SERVICE.read_text(encoding="utf-8")
        handler = source.split("void CommandService::OnSetTimeCommand", 1)[1].split(
            "void CommandService::OnTeleportCommandRequest", 1
        )[0]

        self.assertIn("if (!acMessage.pPlayer)", handler)
        self.assertIn("IsAdminSession(GameServer::Get(), acMessage.pPlayer)", handler)
        self.assertNotIn("Packet.PlayerId", handler)
        self.assertNotIn("cPlayerId", handler)
        self.assertIn("apPlayer->GetConnectionId()", source)
        self.assertIn("GetAdminSessions()", source)

    def test_set_time_allows_only_the_sender_party_leader_on_private_servers(self) -> None:
        source = COMMAND_SERVICE.read_text(encoding="utf-8")
        handler = source.split("void CommandService::OnSetTimeCommand", 1)[1].split(
            "void CommandService::OnTeleportCommandRequest", 1
        )[0]

        party_gate = "pPartyService->IsPlayerLeader(acMessage.pPlayer) && !bAnnounceServer"
        self.assertIn(party_gate, handler)
        self.assertLess(handler.index(party_gate), handler.index("kNoPermission"))

    def test_authorized_set_time_reports_invalid_input_when_calendar_rejects_it(self) -> None:
        source = COMMAND_SERVICE.read_text(encoding="utf-8")
        handler = source.split("void CommandService::OnSetTimeCommand", 1)[1].split(
            "void CommandService::OnTeleportCommandRequest", 1
        )[0]

        result_assignment = (
            "response.Result = timeSetSuccessfully ? NotifySetTimeResult::SetTimeResult::kSuccess\n"
            "                                              : NotifySetTimeResult::SetTimeResult::kInvalidInput;"
        )
        self.assertEqual(handler.count("const bool timeSetSuccessfully ="), 2)
        self.assertEqual(handler.count(result_assignment), 2)
        self.assertIn("kInvalidInput", (ROOT / "Code" / "encoding" / "Messages" / "NotifySetTimeResult.h").read_text(encoding="utf-8"))

    def test_ordinary_teleport_denies_self_partyless_and_cross_party_requests(self) -> None:
        source = OVERLAY_SERVICE.read_text(encoding="utf-8")
        handler = source.split("void OverlayService::OnTeleport", 1)[1].split(
            "void OverlayService::OnPlayerHealthUpdate", 1
        )[0]

        self.assertIn("pTargetPlayer != pSendingPlayer", handler)
        self.assertIn("partyService.IsPlayerInParty(pSendingPlayer)", handler)
        self.assertIn("partyService.IsPlayerInParty(pTargetPlayer)", handler)
        self.assertIn("pSendingPlayer->GetParty().JoinedPartyId", handler)
        self.assertIn("pTargetPlayer->GetParty().JoinedPartyId", handler)
        self.assertIn("if (!isSameParty)", handler)
        self.assertIn("return;", handler.split("if (!isSameParty)", 1)[1].split("NotifyTeleport response{}", 1)[0])

    def test_ordinary_teleport_sends_locations_only_after_same_party_gate(self) -> None:
        source = OVERLAY_SERVICE.read_text(encoding="utf-8")
        handler = source.split("void OverlayService::OnTeleport", 1)[1].split(
            "void OverlayService::OnPlayerHealthUpdate", 1
        )[0]

        self.assertLess(handler.index("if (!isSameParty)"), handler.index("NotifyTeleport response{}"))
        self.assertLess(handler.index("NotifyTeleport response{}"), handler.index("pSendingPlayer->Send(response)"))

    def test_command_teleport_requires_an_admin_sender_before_target_lookup(self) -> None:
        source = COMMAND_SERVICE.read_text(encoding="utf-8")
        handler = source.split("void CommandService::OnTeleportCommandRequest", 1)[1]

        gate = "if (!IsAdminSession(GameServer::Get(), acMessage.pPlayer))"
        self.assertIn("if (!acMessage.pPlayer)", handler)
        self.assertIn(gate, handler)
        self.assertIn("return;", handler.split(gate, 1)[1].split("Player* pTargetPlayer", 1)[0])
        self.assertLess(handler.index(gate), handler.index("Player* pTargetPlayer"))
        self.assertLess(handler.index("Player* pTargetPlayer"), handler.index("acMessage.pPlayer->Send(response)"))


if __name__ == "__main__":
    unittest.main()
