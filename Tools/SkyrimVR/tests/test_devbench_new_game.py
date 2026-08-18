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

    def test_launch_nonce_must_match_when_required(self) -> None:
        avatar = ready_avatar_assignment()
        avatar["launchNonce"] = "a" * 32
        online = {"playerId": "3", "connectionGeneration": "7"}

        self.assertTrue(DEV_BENCH.avatar_assignment_ready(avatar, online, "11", "a" * 32))
        self.assertFalse(DEV_BENCH.avatar_assignment_ready(avatar, online, "11", "b" * 32))


class ReleaseAdmissionTests(unittest.TestCase):
    def test_exact_engine_reported_plugin_order_is_required(self) -> None:
        mods = {
            "plugins": [{"name": name} for name in DEV_BENCH.RELEASE_ACTIVE_PLUGIN_ORDER],
            "lightPlugins": [],
        }

        self.assertEqual(
            DEV_BENCH.require_release_active_plugin_order(mods),
            DEV_BENCH.RELEASE_ACTIVE_PLUGIN_ORDER,
        )

    def test_unexpected_or_reordered_engine_plugins_fail_closed(self) -> None:
        expected = list(DEV_BENCH.RELEASE_ACTIVE_PLUGIN_ORDER)
        cases = (
            expected + ["Unrelated.esp"],
            [expected[1], expected[0], *expected[2:]],
        )
        for names in cases:
            with self.subTest(names=names):
                with self.assertRaisesRegex(DEV_BENCH.AutomationError, "isolated release lane"):
                    DEV_BENCH.require_release_active_plugin_order(
                        {"plugins": [{"name": name} for name in names], "lightPlugins": []}
                    )

    def test_active_light_plugin_fails_closed(self) -> None:
        mods = {
            "plugins": [{"name": name} for name in DEV_BENCH.RELEASE_ACTIVE_PLUGIN_ORDER],
            "lightPlugins": [{"name": "Unexpected.esl", "index": 0}],
        }
        with self.assertRaisesRegex(DEV_BENCH.AutomationError, "lightPluginCount=1"):
            DEV_BENCH.require_release_active_plugin_order(mods)

    def test_online_status_requires_nonce_versions_protocol_and_server_nonce(self) -> None:
        status = {
            "state": "online",
            "online": "1",
            "playerId": "1",
            "sessionId": "session",
            "launchNonce": "a" * 32,
            "clientVersion": "1.0.0",
            "serverVersion": "1.0.0",
            "gameplayProtocolRevision": "14",
            "serverInstanceNonce": "1",
            "connectionGeneration": "2",
        }

        self.assertTrue(DEV_BENCH.online_status_ready(status, "session", 1, "a" * 32))
        for key, value in (
            ("serverVersion", "other"),
            ("gameplayProtocolRevision", "13"),
            ("serverInstanceNonce", "0"),
            ("launchNonce", "b" * 32),
        ):
            with self.subTest(key=key):
                rejected = {**status, key: value}
                self.assertFalse(DEV_BENCH.online_status_ready(rejected, "session", 1, "a" * 32))

    def test_nonce_bound_error_is_terminal_and_bounded(self) -> None:
        error = {"state": "error", "launchNonce": "a" * 32, "error": "wrong_version " * 40}
        with self.assertRaisesRegex(DEV_BENCH.TerminalAutomationError, "wrong_version") as raised:
            DEV_BENCH.online_status_ready(error, "session", 0, "a" * 32)
        self.assertLessEqual(len(str(raised.exception)), 256 + 64)

    def test_usable_menu_requires_a_temporally_separated_non_modal_observation(self) -> None:
        usable = {"openMenus": ["HUD Menu"], "messageBoxOpen": False}
        blocked = {"openMenus": ["HUD Menu", "Loading Menu"], "messageBoxOpen": False}
        self.assertTrue(DEV_BENCH.usable_stable_menu_state(usable))
        self.assertFalse(DEV_BENCH.usable_stable_menu_state(blocked))

        observations = []
        sleeps = []
        clock = [0.0]

        def waits(description, timeout, poll, predicate):
            observations.append(description)
            value = poll()
            self.assertTrue(predicate(value))
            return value

        def advance(duration):
            sleeps.append(duration)
            clock[0] += duration

        result = DEV_BENCH.wait_for_usable_stable_menu(
            state_reader=lambda: usable,
            fader_closer=lambda: self.fail("no fader should be closed"),
            timeout=1.0,
            wait_for_state=waits,
            monotonic=lambda: clock[0],
            sleep=advance,
        )
        self.assertIs(result, usable)
        self.assertEqual(observations, ["USABLE_STABLE menu state"])
        self.assertGreaterEqual(sum(sleeps), DEV_BENCH.USABLE_STABLE_MENU_MINIMUM_INTERVAL)

    def test_usable_menu_rejects_an_absent_hud(self) -> None:
        self.assertFalse(
            DEV_BENCH.usable_stable_menu_state(
                {"openMenus": [], "messageBoxOpen": False}
            )
        )

    def test_hud_disappearance_resets_the_stability_interval(self) -> None:
        usable = {"openMenus": ["HUD Menu"], "messageBoxOpen": False}
        hud_missing = {"openMenus": [], "messageBoxOpen": False}
        states = iter((usable, usable, hud_missing, usable, usable, usable, usable, usable))
        observations = []
        clock = [0.0]

        def waits(description, timeout, poll, predicate):
            observations.append(description)
            value = poll()
            self.assertTrue(predicate(value))
            return value

        def advance(duration):
            clock[0] += duration

        result = DEV_BENCH.wait_for_usable_stable_menu(
            state_reader=lambda: next(states),
            fader_closer=lambda: self.fail("no fader should be closed"),
            timeout=2.0,
            wait_for_state=waits,
            monotonic=lambda: clock[0],
            sleep=advance,
        )

        self.assertIs(result, usable)
        self.assertEqual(
            observations,
            ["USABLE_STABLE menu state", "USABLE_STABLE menu state after transition"],
        )
        self.assertGreaterEqual(
            clock[0],
            DEV_BENCH.USABLE_STABLE_MENU_MINIMUM_INTERVAL
            + DEV_BENCH.USABLE_STABLE_MENU_POLL_INTERVAL,
        )

    def test_modal_loading_or_fader_transition_resets_the_stability_interval(self) -> None:
        usable = {"openMenus": ["HUD Menu"], "messageBoxOpen": False}
        transitions = (
            {"openMenus": ["HUD Menu", "Loading Menu"], "messageBoxOpen": False},
            {"openMenus": ["HUD Menu", "Fader Menu"], "messageBoxOpen": False},
            {"openMenus": ["HUD Menu", "MessageBoxMenu"], "messageBoxOpen": True},
        )

        for transition in transitions:
            with self.subTest(transition=transition):
                states = iter((usable, usable, transition, usable, usable, usable, usable, usable))
                observations = []
                clock = [0.0]

                def waits(description, timeout, poll, predicate):
                    observations.append(description)
                    value = poll()
                    self.assertTrue(predicate(value))
                    return value

                def advance(duration):
                    clock[0] += duration

                result = DEV_BENCH.wait_for_usable_stable_menu(
                    state_reader=lambda: next(states),
                    fader_closer=lambda: self.fail("no fader should be closed"),
                    timeout=2.0,
                    wait_for_state=waits,
                    monotonic=lambda: clock[0],
                    sleep=advance,
                )

                self.assertIs(result, usable)
                self.assertEqual(
                    observations,
                    ["USABLE_STABLE menu state", "USABLE_STABLE menu state after transition"],
                )
                self.assertGreaterEqual(
                    clock[0],
                    DEV_BENCH.USABLE_STABLE_MENU_MINIMUM_INTERVAL
                    + DEV_BENCH.USABLE_STABLE_MENU_POLL_INTERVAL,
                )


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
