#!/usr/bin/env python3
"""Focused identity and freshness gates for live SkyrimTogetherVR evidence."""

from __future__ import annotations

import os
import pathlib
import sys
import tempfile
import time
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
TOOLS = ROOT / "Tools" / "SkyrimVR"
sys.path.insert(0, str(TOOLS))

import vr_handoff  # noqa: E402


NONCE = "0123456789abcdef0123456789abcdef"


class RuntimeIdentityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="stvr-runtime-identity-")
        self.root = pathlib.Path(self.temp.name)
        self.game = self.root / "SkyrimVR"
        self.handoff = self.game / "Data" / "SkyrimTogetherReborn"
        self.handoff.mkdir(parents=True)
        self.write_readouts()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def write_readouts(
        self,
        *,
        lifecycle_nonce: str = NONCE,
        avatar_process: int = 42,
        client_version: str = "fixture",
        server_version: str = "fixture",
        protocol: int = 17,
        game_root: pathlib.Path | None = None,
    ) -> None:
        root = game_root or self.game
        common = f"launchNonce={NONCE}\nprocessId=42\n"
        payloads = {
            "status": (
                common
                + f"online=1\nclientVersion={client_version}\nserverVersion={server_version}\n"
                + f"gameplayProtocolRevision={protocol}\nserverInstanceNonce=7\nsessionId=8\n"
                + f"connectionGeneration=9\ngamePath={root}\n"
            ),
            "lifecycle": f"launchNonce={lifecycle_nonce}\nprocessId=42\ngamePath={root}\n",
            "playercell": common + f"gamePath={root}\n",
            "avatar": f"launchNonce={NONCE}\nprocessId={avatar_process}\ngamePath={root}\n",
            "gameplay": vr_handoff.gameplay_snapshot_fixture(
                root,
                launch_nonce=NONCE,
                process_id=42,
                session_id=8,
                server_instance_nonce=7,
                connection_generation=9,
                lifecycle_epoch=3,
            ),
        }
        for name, text in payloads.items():
            (self.handoff / vr_handoff.READOUT_FILES[name]).write_text(text, encoding="utf-8")

    def evaluate(self, **kwargs: object) -> dict[str, object]:
        return vr_handoff.evaluate_runtime_identity(
            vr_handoff.read_readouts(self.handoff),
            self.handoff,
            self.game,
            max_age_seconds=30,
            **kwargs,
        )

    def assert_rejects(self, text: str, **kwargs: object) -> None:
        result = self.evaluate(**kwargs)
        self.assertFalse(result["ok"])
        self.assertIn(text, "; ".join(result["reasons"]))

    def test_clean_identity_passes(self) -> None:
        self.assertTrue(self.evaluate()["ok"])

    def test_gameplay_snapshot_requires_every_mandatory_canonical_domain(self) -> None:
        path = self.handoff / vr_handoff.READOUT_FILES["gameplay"]
        path.write_text(
            path.read_text(encoding="utf-8").replace(
                "domain.movement.state=active", "domain.movement.state=blocked_capability"
            ),
            encoding="utf-8",
        )
        self.assert_rejects("mandatory domain movement state=blocked_capability")

    def test_gameplay_snapshot_rejects_mismatched_session_identity(self) -> None:
        path = self.handoff / vr_handoff.READOUT_FILES["gameplay"]
        path.write_text(
            path.read_text(encoding="utf-8").replace("session.id=8", "session.id=99"),
            encoding="utf-8",
        )
        self.assert_rejects("gameplay session.id does not match status sessionId")

    def test_not_ready_gameplay_snapshot_is_valid_for_non_gameplay_profile(self) -> None:
        path = self.handoff / vr_handoff.READOUT_FILES["gameplay"]
        path.write_text(
            path.read_text(encoding="utf-8")
            .replace("ready=1", "ready=0", 1)
            .replace("state=ready", "state=unavailable", 1),
            encoding="utf-8",
        )
        self.assertTrue(self.evaluate()["ok"])

    def test_not_ready_gameplay_snapshot_is_rejected_for_gameplay_profile(self) -> None:
        path = self.handoff / vr_handoff.READOUT_FILES["gameplay"]
        path.write_text(
            path.read_text(encoding="utf-8")
            .replace("ready=1", "ready=0", 1)
            .replace("state=ready", "state=unavailable", 1),
            encoding="utf-8",
        )
        self.assert_rejects("gameplay snapshot is not ready", require_gameplay_ready=True)

    def test_stale_readout_is_rejected(self) -> None:
        path = self.handoff / vr_handoff.READOUT_FILES["avatar"]
        old = time.time() - 31
        os.utime(path, (old, old))
        self.assert_rejects("avatar readout is stale")

    def test_readout_predating_run_start_marker_is_rejected(self) -> None:
        marker = self.root / "run-start.marker"
        marker.write_text("fixture\n", encoding="utf-8")
        future = time.time() + 2
        os.utime(marker, (future, future))
        self.assert_rejects("predates run-start marker", run_start_marker=marker)

    def test_nonce_mismatch_is_rejected(self) -> None:
        self.write_readouts(lifecycle_nonce="fedcba9876543210fedcba9876543210")
        self.assert_rejects("launchNonce differs")

    def test_process_mismatch_is_rejected(self) -> None:
        self.write_readouts(avatar_process=43)
        self.assert_rejects("processId differs")

    def test_build_mismatch_is_rejected(self) -> None:
        self.write_readouts(server_version="other")
        self.assert_rejects("clientVersion/serverVersion")

    def test_protocol_mismatch_is_rejected(self) -> None:
        self.write_readouts(protocol=13)
        self.assert_rejects("gameplayProtocolRevision is not 17")

    def test_wrong_root_is_rejected(self) -> None:
        self.write_readouts(game_root=self.root / "OtherSkyrimVR")
        self.assert_rejects("does not match requested game root")

    def test_each_identity_readout_requires_game_path(self) -> None:
        for name in vr_handoff.RUNTIME_IDENTITY_READOUTS:
            with self.subTest(name=name):
                self.write_readouts()
                path = self.handoff / vr_handoff.READOUT_FILES[name]
                path.write_text(
                    "\n".join(line for line in path.read_text(encoding="utf-8").splitlines() if not line.startswith("gamePath="))
                    + "\n",
                    encoding="utf-8",
                )
                self.assert_rejects(f"{name} gamePath is missing")

    def test_package_network_version_mismatch_is_rejected(self) -> None:
        result = vr_handoff.evaluate_runtime_identity(
            vr_handoff.read_readouts(self.handoff),
            self.handoff,
            self.game,
            max_age_seconds=30,
            expected_network_version="other",
        )
        self.assertFalse(result["ok"])
        self.assertIn("status clientVersion does not match package networkVersion", result["reasons"])

    def test_wine_z_drive_root_matches_linux_game_path(self) -> None:
        status_path = self.handoff / vr_handoff.READOUT_FILES["status"]
        status = status_path.read_text(encoding="utf-8")
        wine_root = "Z:" + str(self.game).replace("/", "\\")
        status = status.replace(f"gamePath={self.game}", f"gamePath={wine_root}")
        status_path.write_text(status, encoding="utf-8")
        self.assertTrue(self.evaluate()["ok"])


if __name__ == "__main__":
    unittest.main()
