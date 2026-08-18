#!/usr/bin/env python3
"""Focused state-gating tests for Skyrim VR new-game automation."""

from __future__ import annotations

import importlib.util
import pathlib
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
TOOLS = ROOT / "Tools" / "SkyrimVR"
SCRIPT = TOOLS / "devbench_new_game.py"
sys.path.insert(0, str(TOOLS))


def load_script():
    spec = importlib.util.spec_from_file_location("stvr_devbench_new_game", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


DEV_BENCH = load_script()
RACESEX_OPEN = {"openMenus": ["RaceSex Menu"], "messageBoxOpen": False}


def ready_avatar_assignment() -> dict[str, str]:
    return {
        "ready": "1",
        "connected": "1",
        "localServerAssigned": "1",
        "localServerId": "2097153",
        "transportConnectionGeneration": "7",
        "lifecycleEpoch": "11",
        "assignmentGate": "assigned",
        "assignmentBootstrapGate": "bootstrap_ready",
        "assignmentBootstrapReady": "1",
        "assignmentBootstrapFailure": "none",
        "localAssignmentRejected": "0",
        "assignmentPending": "0",
        "assignmentBootstrapPending": "0",
        "assignmentBootstrapRetryScheduled": "0",
        "assignmentBootstrapActive": "0",
        "assignmentBootstrapPermanentFailure": "0",
        "assignmentBootstrapFailureCount": "0",
        "assignmentBootstrapEndFailureMask": "0",
        "assignmentBootstrapAppearanceValidationFailureMask": "0",
    }


class AvatarAssignmentReadyTests(unittest.TestCase):
    def test_stability_window_covers_two_periodic_checkpoints_with_margin(self) -> None:
        self.assertEqual(DEV_BENCH.TASK_RUN_CHECKPOINT_INTERVAL, 5.0)
        self.assertGreaterEqual(
            DEV_BENCH.ASSIGNMENT_STABILITY_WINDOW,
            2 * DEV_BENCH.TASK_RUN_CHECKPOINT_INTERVAL + 2.0,
        )

    def test_distinct_canonical_server_id_and_transport_player_id_pass(self) -> None:
        avatar = ready_avatar_assignment()
        online = {"playerId": "3", "connectionGeneration": "7"}

        self.assertTrue(DEV_BENCH.avatar_assignment_ready(avatar, online, "11"))

    def test_zero_canonical_server_id_fails(self) -> None:
        avatar = ready_avatar_assignment()
        avatar["localServerId"] = "0"
        online = {"playerId": "3", "connectionGeneration": "7"}

        self.assertFalse(DEV_BENCH.avatar_assignment_ready(avatar, online, "11"))

    def test_wrong_generation_or_epoch_still_fails(self) -> None:
        online = {"playerId": "3", "connectionGeneration": "7"}
        cases = (
            ({**ready_avatar_assignment(), "transportConnectionGeneration": "8"}, "11"),
            (ready_avatar_assignment(), "12"),
        )

        for avatar, epoch in cases:
            with self.subTest(avatar=avatar, epoch=epoch):
                self.assertFalse(DEV_BENCH.avatar_assignment_ready(avatar, online, epoch))


class RaceSexNameStageTests(unittest.TestCase):
    def test_already_closed_sends_no_name_stage_pulse(self) -> None:
        pulses = []
        closed = {"openMenus": [], "messageBoxOpen": False}

        result = DEV_BENCH.complete_racesex_name_stage(
            closed,
            state_reader=lambda: self.fail("state must not be read after RaceSex closes"),
            trigger_publisher=lambda: pulses.append("trigger"),
            timeout=1.0,
        )

        self.assertIs(result, closed)
        self.assertEqual(pulses, [])

    def test_stranded_name_stage_sends_one_pulse_then_requires_closure(self) -> None:
        pulses = []
        closed = {"openMenus": [], "messageBoxOpen": False}

        def closes(description, timeout, poll, predicate):
            self.assertIn("name/finalization", description)
            self.assertEqual(timeout, 1.0)
            self.assertTrue(predicate(closed))
            return closed

        result = DEV_BENCH.complete_racesex_name_stage(
            RACESEX_OPEN,
            state_reader=lambda: closed,
            trigger_publisher=lambda: pulses.append("trigger"),
            timeout=1.0,
            wait_for_close=closes,
        )

        self.assertIs(result, closed)
        self.assertEqual(pulses, ["trigger"])

    def test_reopened_message_box_blocks_name_stage_without_pulse(self) -> None:
        pulses = []
        blocked = {"openMenus": ["RaceSex Menu", "MessageBoxMenu"], "messageBoxOpen": True}

        with self.assertRaisesRegex(DEV_BENCH.AutomationError, "not actionable"):
            DEV_BENCH.complete_racesex_name_stage(
                blocked,
                state_reader=lambda: self.fail("blocked state must not be polled"),
                trigger_publisher=lambda: pulses.append("trigger"),
                timeout=1.0,
            )

        self.assertEqual(pulses, [])

    def test_pulse_that_does_not_close_racesex_fails_without_retrying(self) -> None:
        pulses = []

        def does_not_close(description, timeout, poll, predicate):
            self.assertFalse(predicate(RACESEX_OPEN))
            raise DEV_BENCH.AutomationError("timed out waiting for closure")

        with self.assertRaisesRegex(DEV_BENCH.AutomationError, "timed out"):
            DEV_BENCH.complete_racesex_name_stage(
                RACESEX_OPEN,
                state_reader=lambda: RACESEX_OPEN,
                trigger_publisher=lambda: pulses.append("trigger"),
                timeout=1.0,
                wait_for_close=does_not_close,
            )

        self.assertEqual(pulses, ["trigger"])


class RealmLorkhanFaderTests(unittest.TestCase):
    def test_absent_fader_does_nothing(self) -> None:
        closes = []
        hud_only = {"openMenus": ["HUD Menu"], "messageBoxOpen": False}

        result = DEV_BENCH.drain_stale_realm_lorkhan_fader(
            state_reader=lambda: hud_only,
            fader_closer=lambda: closes.append("Fader Menu"),
            timeout=1.0,
        )

        self.assertIs(result, hud_only)
        self.assertEqual(closes, [])

    def test_exact_stale_fader_closes_once_and_must_disappear(self) -> None:
        closes = []
        stale = {"openMenus": ["HUD Menu", "Fader Menu"], "messageBoxOpen": False}
        hud_only = {"openMenus": ["HUD Menu"], "messageBoxOpen": False}
        reads = iter((stale, hud_only))

        def closes_fader(description, timeout, poll, predicate):
            self.assertIn("RealmLorkhan Fader Menu", description)
            self.assertEqual(timeout, 1.0)
            self.assertFalse(predicate(stale))
            state = poll()
            self.assertTrue(predicate(state))
            return state

        result = DEV_BENCH.drain_stale_realm_lorkhan_fader(
            state_reader=lambda: next(reads),
            fader_closer=lambda: closes.append("Fader Menu"),
            timeout=1.0,
            wait_for_close=closes_fader,
        )

        self.assertIs(result, hud_only)
        self.assertEqual(closes, ["Fader Menu"])

    def test_unsafe_mixed_fader_state_fails_without_close(self) -> None:
        for blocker in (
            "Main Menu",
            "RaceSex Menu",
            "MessageBoxMenu",
            "Loading Menu",
            "Unexpected Menu",
        ):
            with self.subTest(blocker=blocker):
                closes = []
                unsafe = {
                    "openMenus": ["HUD Menu", "Fader Menu", blocker],
                    "messageBoxOpen": blocker == "MessageBoxMenu",
                }

                with self.assertRaisesRegex(DEV_BENCH.AutomationError, "unsafe"):
                    DEV_BENCH.drain_stale_realm_lorkhan_fader(
                        state_reader=lambda: unsafe,
                        fader_closer=lambda: closes.append("Fader Menu"),
                        timeout=1.0,
                    )

                self.assertEqual(closes, [])


if __name__ == "__main__":
    unittest.main()
