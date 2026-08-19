#!/usr/bin/env python3
"""Source-level guard for synchronous native Skyrim VR quest mutation."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
BRIDGE = ROOT / "Code" / "vr_gameplay_bridge"
PROTOCOL = ROOT / "Code" / "vr_common" / "VRGameplayBridge.h"


class QuestSynchronizationNativeTests(unittest.TestCase):
    def test_quest_mutation_and_event_sinks_are_advertised(self) -> None:
        endpoint = (BRIDGE / "BridgeEndpoint.cpp").read_text(encoding="utf-8")
        available = endpoint.split("constexpr CapabilityMask kAvailableCapabilities =", 1)[1].split(
            "constexpr CapabilityMask kOptionalCapabilities", 1
        )[0]

        self.assertIn("Capability::QuestAndDialogue", available)
        self.assertIn("Capability::QuestMutation", available)
        optional = endpoint.split("constexpr CapabilityMask kOptionalCapabilities =", 1)[1].split(
            "constexpr CapabilityMask kAllowedCapabilities", 1
        )[0]
        self.assertIn("Capability::LocalEventSinks", optional)
        self.assertIn("Capability::LocalCaptureSinks", optional)

    def test_quest_uses_its_unrequested_mutation_capability(self) -> None:
        protocol = PROTOCOL.read_text(encoding="utf-8")

        self.assertIn("QuestMutation = 1ull << 23", protocol)
        self.assertIn("inline constexpr std::uint32_t kCapabilityRevision = 33", protocol)
        mapping = protocol.split("constexpr Capability CapabilityForDomain", 1)[1].split(
            "constexpr bool IsActionInDomain", 1
        )[0]
        self.assertIn(
            "case GameplayDomain::Quest:\n        return Capability::QuestMutation;",
            mapping,
        )
        self.assertIn(
            "case GameplayDomain::Dialogue:\n    case GameplayDomain::Party:\n        return Capability::QuestAndDialogue;",
            mapping,
        )
        initial = protocol.split("inline constexpr CapabilityMask kInitialCapabilities =", 1)[1].split(
            "constexpr bool HasCapability", 1
        )[0]
        self.assertIn("Capability::QuestAndDialogue", initial)
        self.assertIn("Capability::QuestMutation", initial)
        self.assertIn("Capability::LocalEventSinks", initial)
        self.assertIn("Capability::LocalCaptureSinks", initial)

        mandatory = protocol.split("inline constexpr CapabilityMask kMandatoryNativeParityCapabilities =", 1)[1].split(
            "constexpr bool HasCapability", 1
        )[0]
        self.assertIn("Capability::LocalCaptureSinks", mandatory)
        self.assertNotIn("Capability::LocalEventSinks", mandatory)

    def test_inbound_quest_actions_use_the_normal_capability_gate(self) -> None:
        executor = (BRIDGE / "CommandExecutor.cpp").read_text(encoding="utf-8")
        validation = executor.split("CommandStatus ValidateGameplayCommand(", 1)[1].split(
            "CommandStatus ExecuteGameplayAction(", 1
        )[0]

        self.assertNotIn("QuestSynchronizationStatus", validation)
        self.assertIn("const auto capability = CapabilityForDomain(domain);", validation)

    def test_quest_dispatch_is_synchronous_and_suppressed(self) -> None:
        manager = (BRIDGE / "QuestDialogueManager.cpp").read_text(encoding="utf-8")
        capture = (BRIDGE / "LocalGameplayCapture.cpp").read_text(encoding="utf-8")

        self.assertIn("a_quest.SetStage(a_stage)", manager)
        self.assertIn("ArmQuestStageSuppression", manager)
        self.assertIn("ArmQuestStartStopSuppression", manager)
        self.assertIn("quest->SetActive(false);\n        quest->Stop();", manager)
        self.assertIn("quest->SetActive(true);", manager)
        self.assertIn("CommandStatus::Degraded", manager)
        self.assertIn("CancelQuestSuppressions(stageResult)", manager)
        self.assertIn("ReconcilePartialQuestMutation", manager)
        self.assertIn("runtime-unverified", manager)
        self.assertNotIn("DispatchMethodCall(", manager)
        can_publish = capture.split("bool CanPublish(", 1)[1].split("void RecordPeriodicPublication", 1)[0]
        self.assertIn("CapabilityForDomain(a_domain)", can_publish)
        publish = capture.split("bool Publish(", 1)[1].split("void RecordPeriodicPublication", 1)[0]
        self.assertIn("!CanPublish(a_domain)", publish)

        self.assertIn("Capability::LocalCaptureSinks", capture)
        self.assertIn("g_scriptSinksRegistered && g_animationSinkRegistered", capture)
        self.assertIn("g_initialized = false;", capture)
        capture_init = capture.split("bool InitializeLocalCaptureSinksUnlocked()", 1)[1].split(
            "void CaptureAppearance", 1
        )[0]
        no_player = capture_init.split("if (!player) {", 1)[1].split("}", 1)[0]
        self.assertNotIn("g_animationSinkRegistered = false", no_player)
        self.assertNotIn("g_animationSinkPlayer = nullptr", no_player)
        self.assertIn("PublishQuestReconciliation", capture)


if __name__ == "__main__":
    unittest.main()
