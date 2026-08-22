#!/usr/bin/env python3
"""Source-level guard for synchronous native Skyrim VR quest mutation."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
BRIDGE = ROOT / "Code" / "vr_gameplay_bridge"
PROTOCOL = ROOT / "Code" / "vr_common" / "VRGameplayBridge.h"
WORLD_REPLICATION = ROOT / "Code" / "client" / "Services" / "Generic" / "VRWorldReplicationService.cpp"
SERVER_QUEST_SERVICE = ROOT / "Code" / "server" / "Services" / "QuestService.cpp"
CAPABILITIES = ROOT / "Code" / "encoding" / "Structs" / "GameplayCapabilities.h"
VR_LOCAL_GAMEPLAY = ROOT / "Code" / "client" / "Services" / "Generic" / "VRLocalGameplayService.cpp"
LEGACY_QUEST_SERVICE = ROOT / "Code" / "client" / "Services" / "Generic" / "QuestService.cpp"


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
        self.assertIn("inline constexpr std::uint32_t kCapabilityRevision = 34", protocol)
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

        mandatory = protocol.split("inline constexpr CapabilityMask kMandatoryNativeGameplayCoreCapabilities =", 1)[1].split(
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
        native_access = (BRIDGE / "QuestNativeAccess.cpp").read_text(encoding="utf-8")
        capture = (BRIDGE / "LocalGameplayCapture.cpp").read_text(encoding="utf-8")

        self.assertIn("QuestNativeAccess::SetStage(a_quest, a_stage)", manager)
        self.assertIn("QuestNativeAccess::SetActive(*quest, false);\n            quest->Stop();", manager)
        self.assertIn("QuestNativeAccess::SetActive(*quest, true);", manager)
        self.assertIn("bool SetStage(RE::TESQuest& a_quest, const std::uint16_t a_stage) noexcept", native_access)
        self.assertIn("void SetActive(RE::TESQuest& a_quest, const bool a_active) noexcept", native_access)
        self.assertIn("ArmQuestStageSuppression", manager)
        self.assertIn("ArmQuestStartStopSuppression", manager)
        self.assertIn("CommandStatus::Degraded", manager)
        self.assertIn("CancelQuestSuppressions(stageResult)", manager)
        self.assertIn("ReconcilePartialQuestMutation", manager)
        self.assertNotIn("DispatchMethodCall(", manager)
        can_publish = capture.split("bool CanPublish(", 1)[1].split("void RecordPeriodicPublication", 1)[0]
        self.assertIn("CapabilityForDomain(a_domain)", can_publish)
        publish = capture.split("bool Publish(", 1)[1].split("void RecordPeriodicPublication", 1)[0]
        self.assertIn("!CanPublish(a_domain)", publish)

        self.assertIn("Capability::LocalCaptureSinks", capture)
        self.assertIn(
            "g_scriptSinkRegistration.State == SinkRegistrationState::Registered",
            capture,
        )
        self.assertIn(
            "g_animationSinkRegistration.State == SinkRegistrationState::Registered",
            capture,
        )
        self.assertIn("PublishLocalCaptureSinkReadiness", capture)
        self.assertIn("g_initialized = false;", capture)
        capture_init = capture.split("bool InitializeLocalCaptureSinksUnlocked()", 1)[1].split(
            "void CaptureAppearance", 1
        )[0]
        no_player = capture_init.split("if (!player) {", 1)[1].split("}", 1)[0]
        self.assertNotIn("g_animationSinkRegistered = false", no_player)
        self.assertNotIn("g_animationSinkPlayer = nullptr", no_player)
        self.assertIn("PublishQuestReconciliation", capture)

    def test_owner_scoped_recovery_uses_the_originating_party_member(self) -> None:
        server = SERVER_QUEST_SERVICE.read_text(encoding="utf-8")
        client = WORLD_REPLICATION.read_text(encoding="utf-8")
        capabilities = CAPABILITIES.read_text(encoding="utf-8")

        resync_handler = server.split("void QuestService::OnQuestResyncRequest", 1)[1].split(
            "void QuestService::OnQuestChanges", 1
        )[0]
        self.assertIn("GetById(request.OwnerPlayerId)", resync_handler)
        self.assertIn("CanAuthorizeQuestOwner", resync_handler)
        self.assertIn("owner->GetQuestLogComponent().QuestContent", resync_handler)
        self.assertIn("response.OwnerPlayerId = owner->GetId()", resync_handler)
        self.assertIn("response.HasParty = requesterParty.has_value()", resync_handler)
        self.assertIn("response.PartyId = requesterParty.value_or(0)", resync_handler)

        updates = server.split("void QuestService::OnQuestChanges", 1)[1]
        self.assertIn("message.OwnerPlayerId != 0 && message.OwnerPlayerId != pPlayer->GetId()", updates)
        self.assertIn("notify.OwnerPlayerId = pPlayer->GetId()", updates)
        self.assertIn("notify.CanonicalRevision = revision", updates)

        self.assertIn("RequestQuestResync(acMessage.OwnerPlayerId)", client)
        self.assertIn("acMessage.OwnerPlayerId, acMessage.Id", client)
        self.assertIn("CanonicalOwnerPlayerId", client)
        self.assertIn("RequestQuestResync(acPending.CanonicalOwnerPlayerId)", client)
        self.assertIn("DoesQuestUpdateSupersedeSnapshot", client)
        self.assertIn("IsQuestRecoveryCurrent", client)
        self.assertIn("CanCommitQuestSnapshot", client)

        self.assertIn("CanAuthorizeQuestOwner", capabilities)
        self.assertIn("DoesQuestUpdateSupersedeSnapshot", capabilities)
        self.assertIn("CanCommitQuestSnapshot", capabilities)

    def test_every_local_quest_update_uses_the_authenticated_player_owner(self) -> None:
        vr_source = VR_LOCAL_GAMEPLAY.read_text(encoding="utf-8")
        vr_quest_path = vr_source.split("case GameplayBridge::GameplayAction::SetQuestState:", 1)[1].split(
            "default:", 1
        )[0]
        self.assertIn("const auto ownerPlayerId = m_transport.GetLocalPlayerId();", vr_quest_path)
        self.assertIn("if (ownerPlayerId == 0)\n            return;", vr_quest_path)
        self.assertIn("request.OwnerPlayerId = ownerPlayerId;", vr_quest_path)

        legacy_source = LEGACY_QUEST_SERVICE.read_text(encoding="utf-8")
        producers = legacy_source.split("void QuestService::OnQuestUpdate", 1)[0]
        self.assertEqual(producers.count("RequestQuestUpdate update;"), 2)
        self.assertEqual(
            producers.count("const auto ownerPlayerId = m_world.GetTransport().GetLocalPlayerId();"),
            2,
        )
        self.assertEqual(producers.count("if (ownerPlayerId == 0)\n                        return;"), 2)
        self.assertEqual(producers.count("update.OwnerPlayerId = ownerPlayerId;"), 2)


if __name__ == "__main__":
    unittest.main()
