#!/usr/bin/env python3
"""Source-level contract for bounded two-client runtime diagnostics."""

from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
CLIENT_APP = ROOT / "Code" / "client" / "TiltedOnlineApp.cpp"
CLIENT_TRANSPORT = ROOT / "Code" / "client" / "Services" / "Generic" / "TransportService.cpp"
SERVER = ROOT / "Code" / "server" / "GameServer.cpp"


class TwoClientRuntimeDiagnosticsTests(unittest.TestCase):
    def test_client_bridge_health_is_bounded_and_complete(self) -> None:
        source = CLIENT_APP.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            r"kBridgeHealthSummaryInterval\s*=\s*std::chrono::seconds\(\s*30\s*\)",
        )
        self.assertRegex(
            source,
            r"now\s*-\s*state\.LastPeriodicSummary\s*<\s*kBridgeHealthSummaryInterval",
        )
        self.assertIn('LogBridgeHealthSummary("disconnect", true)', source)
        self.assertIn('LogBridgeHealthSummary("shutdown", true)', source)

        for field in (
            "lifecycleEpoch=",
            "endpointState=",
            "ready=",
            "producedEvents=",
            "consumedEvents=",
            "submittedCommands=",
            "executedCommands=",
            "rejectedCommands=",
            "staleCommands=",
            "discardedEvents=",
            "rejectedSubmissions=",
            "eventRingDrops=",
            "commandRingDrops=",
        ):
            self.assertIn(field, source)

    def test_authentication_milestones_preserve_session_correlation(self) -> None:
        client = CLIENT_TRANSPORT.read_text(encoding="utf-8")
        server = SERVER.read_text(encoding="utf-8")

        self.assertIn("STVR auth accepted:", client)
        for field in (
            "playerId=",
            "protocolRevision=",
            "requestedCapabilities=",
            "negotiatedCapabilities=",
            "serverInstanceNonce=",
            "connectionGeneration=",
            "clientSessionNonce=",
            "connectionAttempt=",
        ):
            self.assertIn(field, client)
        self.assertIn("m_sessionId", client)
        self.assertIn("m_connectionAttemptGeneration", client)

        self.assertIn("STVR auth accepted:", server)
        for field in (
            "playerId=",
            "connectionId=",
            "protocolRevision=",
            "requestedCapabilities=",
            "negotiatedCapabilities=",
            "serverInstanceNonce=",
            "clientSessionNonce=",
            "connectionAttempt=",
            "connectionGeneration=",
        ):
            self.assertIn(field, server)

        auth_milestone = re.search(
            r'STVR auth accepted:.*?connectionGeneration=\{\}"',
            server,
            re.DOTALL,
        )
        self.assertIsNotNone(auth_milestone)
        self.assertNotIn("Token", auth_milestone.group(0))
        self.assertNotIn("remoteAddress", auth_milestone.group(0))
        self.assertNotIn("UserMods", auth_milestone.group(0))
        self.assertNotIn("PrettyPrintModList", server)


if __name__ == "__main__":
    unittest.main()
