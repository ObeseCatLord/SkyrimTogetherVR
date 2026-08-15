#!/usr/bin/env python3
"""Source-level behavioral guard for the disabled VR quest synchronization lane."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
BRIDGE = ROOT / "Code" / "vr_gameplay_bridge"
PROTOCOL = ROOT / "Code" / "vr_common" / "VRGameplayBridge.h"


class QuestSynchronizationFailClosedTests(unittest.TestCase):
    def test_quest_mutation_is_not_advertised_but_dialogue_and_party_are(self) -> None:
        endpoint = (BRIDGE / "BridgeEndpoint.cpp").read_text(encoding="utf-8")
        available = endpoint.split("constexpr CapabilityMask kAvailableCapabilities =", 1)[1].split(
            "constexpr CapabilityMask kOptionalCapabilities", 1
        )[0]

        self.assertIn("Capability::QuestAndDialogue", available)
        self.assertNotIn("Capability::QuestMutation", available)

    def test_quest_uses_its_unrequested_mutation_capability(self) -> None:
        protocol = PROTOCOL.read_text(encoding="utf-8")

        self.assertIn("QuestMutation = 1ull << 23", protocol)
        self.assertIn("inline constexpr std::uint32_t kCapabilityRevision = 31", protocol)
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
        self.assertNotIn("Capability::QuestMutation", initial)

    def test_inbound_quest_actions_are_rejected_before_execution(self) -> None:
        executor = (BRIDGE / "CommandExecutor.cpp").read_text(encoding="utf-8")
        validation = executor.split("CommandStatus ValidateGameplayCommand(", 1)[1].split(
            "CommandStatus ExecuteGameplayAction(", 1
        )[0]

        self.assertIn(
            "if (domain == GameplayDomain::Quest)\n        return QuestDialogueManager::QuestSynchronizationStatus();",
            validation,
        )
        self.assertLess(
            validation.index("domain == GameplayDomain::Quest"),
            validation.index("const auto capability = CapabilityForDomain(domain);"),
        )

    def test_quest_dispatch_cannot_publish_success(self) -> None:
        manager = (BRIDGE / "QuestDialogueManager.cpp").read_text(encoding="utf-8")
        capture = (BRIDGE / "LocalGameplayCapture.cpp").read_text(encoding="utf-8")

        self.assertIn(
            "case GameplayDomain::Quest:\n            return QuestSynchronizationStatus();",
            manager,
        )
        self.assertNotIn("DispatchMethodCall(", manager)
        self.assertNotIn("ArmQuest", manager)
        self.assertNotIn("quest->Start()", manager)
        self.assertNotIn("quest->Stop()", manager)
        can_publish = capture.split("bool CanPublish(", 1)[1].split("void RecordPeriodicPublication", 1)[0]
        self.assertIn("CapabilityForDomain(a_domain)", can_publish)
        publish = capture.split("bool Publish(", 1)[1].split("void RecordPeriodicPublication", 1)[0]
        self.assertIn("!CanPublish(a_domain)", publish)


if __name__ == "__main__":
    unittest.main()
