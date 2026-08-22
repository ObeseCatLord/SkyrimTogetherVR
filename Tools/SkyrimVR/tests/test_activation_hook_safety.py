#!/usr/bin/env python3
"""Source-contract tests for the high-risk native activation detour."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
CAPTURE = ROOT / "Code" / "vr_gameplay_bridge" / "LocalGameplayCapture.cpp"
HOOK = ROOT / "Code" / "vr_gameplay_bridge" / "ActivationHooks.cpp"


class ActivationHookSafetyTests(unittest.TestCase):
    def test_non_player_gate_precedes_every_native_activation_read(self) -> None:
        source = CAPTURE.read_text(encoding="utf-8")
        function = source.split("PreActivationCaptureResult CapturePreActivation(", 1)[1].split(
            "PostActivationCaptureResult CapturePostActivation(", 1
        )[0]

        gate = "if (!player || std::addressof(a_activator) != player)"
        gate_index = function.index(gate)
        for native_read in (
            "a_activator.As<RE::Actor>()",
            "a_target.GetFormID()",
            "a_target.GetBaseObject()",
            "a_target.GetParentCell()",
            "a_target.GetWorldspace()",
            "a_target.GetPosition()",
            "BGSOpenCloseForm::GetOpenState(&a_target)",
        ):
            with self.subTest(native_read=native_read):
                self.assertGreater(function.index(native_read), gate_index)

    def test_detour_calls_original_once_and_advertises_safe_scope(self) -> None:
        source = HOOK.read_text(encoding="utf-8")
        function = source.split("bool HookActivateRef(", 1)[1].split("\n}\n} // namespace", 1)[0]

        self.assertEqual(function.count("original(a_target"), 1)
        self.assertIn("MustPublishAfterSuccessfulOriginal(accepted)", function)
        self.assertIn("scope=local-player-only", source)


if __name__ == "__main__":
    unittest.main()
