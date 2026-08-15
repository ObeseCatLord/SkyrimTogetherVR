#!/usr/bin/env python3
"""CLI behavior coverage for bounded administrative VR handoff commands."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
HANDOFF_TOOL = ROOT / "Tools" / "SkyrimVR" / "vr_handoff.py"
CONNECTION_SERVICE = ROOT / "Code" / "client" / "Services" / "Generic" / "VRConnectionService.cpp"


class VrHandoffAdminCommandTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory(prefix="stvr-admin-command-test-")
        self.handoff_dir = pathlib.Path(self.temp_dir.name)
        self.command_path = self.handoff_dir / "SkyrimTogetherVR.command"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def run_cli(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(HANDOFF_TOOL),
                "--handoff-dir",
                str(self.handoff_dir),
                *arguments,
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_valid_actions_write_distinct_canonical_payloads(self) -> None:
        set_time = self.run_cli("set-time", "0", "59")
        self.assertEqual(set_time.returncode, 0, set_time.stderr)
        self.assertEqual(
            self.command_path.read_text(encoding="utf-8"),
            "action=set_time\nhours=0\nminutes=59\n",
        )

        teleport_to_player = self.run_cli("teleport-to-player", "65535")
        self.assertEqual(teleport_to_player.returncode, 0, teleport_to_player.stderr)
        self.assertEqual(
            self.command_path.read_text(encoding="utf-8"),
            "action=teleport_to_player\nplayerId=65535\n",
        )

        admin_teleport = self.run_cli("admin-teleport", "Remote Tester")
        self.assertEqual(admin_teleport.returncode, 0, admin_teleport.stderr)
        self.assertEqual(
            self.command_path.read_text(encoding="utf-8"),
            "action=admin_teleport\ntargetPlayer=Remote Tester\n",
        )

    def test_invalid_arguments_fail_without_replacing_the_current_command(self) -> None:
        original = "action=disconnect\n"
        self.handoff_dir.mkdir(parents=True, exist_ok=True)
        self.command_path.write_text(original, encoding="utf-8")

        invalid_commands = (
            ("set-time", "24", "0"),
            ("set-time", "0", "60"),
            ("set-time", "+1", "0"),
            ("teleport-to-player",),
            ("teleport-to-player", "0"),
            ("teleport-to-player", "65536"),
            ("teleport-to-player", "3.5"),
            ("admin-teleport", "\x7f"),
        )
        for command in invalid_commands:
            with self.subTest(command=command):
                result = self.run_cli(*command)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(self.command_path.read_text(encoding="utf-8"), original)

    def test_client_does_not_claim_server_processing_after_transport_send(self) -> None:
        source = CONNECTION_SERVICE.read_text(encoding="utf-8")
        self.assertIn('ArchiveCommandFile(".sent")', source)
        self.assertNotIn('ArchiveCommandFile(".processed")', source)


if __name__ == "__main__":
    unittest.main()
