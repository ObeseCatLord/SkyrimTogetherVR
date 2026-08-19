#!/usr/bin/env python3
"""Focused CLI and package-identity tests for runtime evidence scopes."""

from __future__ import annotations

import contextlib
import datetime as dt
import importlib.util
import io
import json
import pathlib
import sys
import tempfile
import time
import unittest
import zipfile
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[3]
TOOLS = ROOT / "Tools" / "SkyrimVR"
sys.path.insert(0, str(TOOLS))


def load_module(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


COLLECT = load_module("collect_runtime_evidence", TOOLS / "collect_runtime_evidence.py")
ARCHIVE_AUDIT = load_module("stvr_audit_runtime_evidence_zip", TOOLS / "audit_runtime_evidence_zip.py")


class RuntimeEvidenceScopeTests(unittest.TestCase):
    @staticmethod
    def package_manifest(*, gameplay: bool = False) -> dict[str, object]:
        return {
            "schema": COLLECT.BUILD_MANIFEST_SCHEMA,
            "mode": "releasedbg",
            "platform": "windows",
            "arch": "x64",
            "avatarSync": False,
            "gameplay": gameplay,
            "packageFlavor": "gameplay" if gameplay else "default",
            "targets": list(COLLECT.GAMEPLAY_EXPECTED_MANIFEST_TARGETS if gameplay else COLLECT.DEFAULT_EXPECTED_MANIFEST_TARGETS),
            "copiedArtifacts": list(COLLECT.GAMEPLAY_EXPECTED_RUNTIME_ARTIFACTS if gameplay else COLLECT.DEFAULT_EXPECTED_RUNTIME_ARTIFACTS),
            "stagedGameFiles": True,
            "companionPanel": True,
            "buildVersion": "fixture",
            "networkVersion": "fixture",
            "sourceRevision": "0" * 40,
            "sourceProvenance": {
                "revision": "0" * 40,
                "sourceTreeSha256": "1" * 64,
                "dirty": False,
                "dirtyApproved": False,
            },
        }

    @staticmethod
    def paired_session_evidence() -> tuple[
        dict[str, str],
        dict[str, str],
        dict[str, str],
        dict[str, str],
    ]:
        game = pathlib.Path("/fixture")
        primary = COLLECT.vr_handoff.parse_key_value_text(
            COLLECT.vr_handoff.gameplay_snapshot_fixture(game)
        )
        peer = COLLECT.vr_handoff.parse_key_value_text(
            COLLECT.vr_handoff.gameplay_snapshot_fixture(
                game,
                launch_nonce="fedcba9876543210fedcba9876543210",
                process_id=43,
                session_id=456,
                connection_generation=2,
            )
        )
        primary_status = {
            "online": "1",
            "playerId": "4",
            "launchNonce": primary["launchNonce"],
            "processId": primary["processId"],
            "sessionId": primary["session.id"],
            "serverInstanceNonce": primary["session.serverInstanceNonce"],
            "connectionGeneration": primary["session.connectionGeneration"],
        }
        peer_status = {
            "online": "1",
            "playerId": "5",
            "launchNonce": peer["launchNonce"],
            "processId": peer["processId"],
            "sessionId": peer["session.id"],
            "serverInstanceNonce": peer["session.serverInstanceNonce"],
            "connectionGeneration": peer["session.connectionGeneration"],
        }
        return primary, peer, primary_status, peer_status

    def explicit_scenario(self, domains: tuple[str, ...]) -> tuple[
        dict[str, object],
        dict[str, str],
        dict[str, str],
        dict[str, str],
        dict[str, str],
    ]:
        primary, peer, primary_status, peer_status = self.paired_session_evidence()
        scenario = ARCHIVE_AUDIT.scenario_evidence_fixture(
            domains,
            primary=ARCHIVE_AUDIT._expected_scenario_client(primary, primary_status),
            peer=ARCHIVE_AUDIT._expected_scenario_client(peer, peer_status),
            server_instance_nonce=primary["session.serverInstanceNonce"],
            now=dt.datetime.now(dt.timezone.utc),
        )
        return scenario, primary, peer, primary_status, peer_status

    def test_collector_gameplay_bootstrap_propagates_only_local_requirements(self) -> None:
        with mock.patch.object(COLLECT, "collect", return_value=pathlib.Path("fixture.zip")) as collect:
            with mock.patch.object(sys, "argv", ["collect_runtime_evidence.py", "--gameplay-bootstrap"]):
                self.assertEqual(COLLECT.main(), 0)

        args = collect.call_args.args[0]
        self.assertTrue(args.gameplay_bootstrap)
        self.assertFalse(args.gameplay)
        self.assertTrue(args.require_connected)
        self.assertTrue(args.require_vrik)
        self.assertTrue(args.require_higgs)
        self.assertFalse(args.require_remote_player)
        self.assertFalse(args.require_gameplay_relays)

    def test_collector_gameplay_remains_strict(self) -> None:
        with mock.patch.object(COLLECT, "collect", return_value=pathlib.Path("fixture.zip")) as collect:
            with mock.patch.object(sys, "argv", ["collect_runtime_evidence.py", "--gameplay"]):
                self.assertEqual(COLLECT.main(), 0)

        args = collect.call_args.args[0]
        self.assertTrue(args.require_remote_player)
        self.assertTrue(args.require_gameplay_relays)
        self.assertTrue(args.require_movement_relay)
        self.assertTrue(args.require_saveload_observer)
        self.assertFalse(args.require_grab_relay)
        self.assertFalse(args.require_higgs_relay)

    def test_full_gameplay_paired_requirements_cover_every_canonical_domain(self) -> None:
        canonical, optional = ARCHIVE_AUDIT.paired_domain_requirements(
            require_gameplay=True,
            require_movement_relay=False,
            require_equipment_relay=False,
            require_activation_relay=False,
            require_magic_relay=False,
            require_combat_relay=False,
            require_projectile_relay=False,
            require_grab_relay=False,
            require_higgs_relay=False,
            require_saveload_observer=False,
        )
        self.assertEqual(canonical, COLLECT.vr_handoff.GAMEPLAY_MANDATORY_CANONICAL_DOMAINS)
        self.assertEqual(optional, ())
        scenario, primary, peer, primary_status, peer_status = self.explicit_scenario(canonical)
        # Activity counts remain diagnostics. Mutating unrelated aggregate
        # counts cannot make or break action-correlated scenario validation.
        primary["domain.quest.captured"] = "900"
        peer["domain.quest.applied"] = "900"
        failures = ARCHIVE_AUDIT.validate_scenario_evidence(
            scenario,
            expected_domains=canonical,
            primary_snapshot=primary,
            peer_snapshot=peer,
            primary_status=primary_status,
            peer_status=peer_status,
            collection_created_utc=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        )
        self.assertEqual(failures, [])

        no_scenario_failures = ARCHIVE_AUDIT.validate_scenario_evidence(
            {},
            expected_domains=canonical,
            primary_snapshot=primary,
            peer_snapshot=peer,
            primary_status=primary_status,
            peer_status=peer_status,
            collection_created_utc=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        )
        self.assertIn(
            "scenario evidence is missing required domain quest",
            no_scenario_failures,
        )

    def test_paired_save_load_requires_explicit_receiver_post_state(self) -> None:
        scenario, primary, peer, primary_status, peer_status = self.explicit_scenario(("save_load",))
        self.assertEqual(primary["domain.save_load.sent"], "0")
        for action in scenario["domains"][0]["actions"]:
            action["receiverObservation"]["postState"] = {}
        failures = ARCHIVE_AUDIT.validate_scenario_evidence(
            scenario,
            expected_domains=("save_load",),
            primary_snapshot=primary,
            peer_snapshot=peer,
            primary_status=primary_status,
            peer_status=peer_status,
            collection_created_utc=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        )
        self.assertTrue(any("receiverObservation.postState" in failure for failure in failures))

    def test_explicit_scenario_evidence_validates_all_required_bindings(self) -> None:
        domains = COLLECT.vr_handoff.GAMEPLAY_MANDATORY_CANONICAL_DOMAINS
        scenario, primary, peer, primary_status, peer_status = self.explicit_scenario(domains)
        failures = ARCHIVE_AUDIT.validate_scenario_evidence(
            scenario,
            expected_domains=domains,
            primary_snapshot=primary,
            peer_snapshot=peer,
            primary_status=primary_status,
            peer_status=peer_status,
            collection_created_utc=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        )
        self.assertEqual(failures, [])

        body_actions = next(row for row in scenario["domains"] if row["domain"] == "vr_body_pose")["actions"]
        self.assertTrue(all("manualHumanObservation" in action for action in body_actions))

    def test_explicit_scenario_evidence_rejects_malformed_replayed_and_cross_session_records(self) -> None:
        domain = "magic"
        scenario, primary, peer, primary_status, peer_status = self.explicit_scenario((domain,))

        malformed = json.loads(json.dumps(scenario))
        malformed["domains"][0]["actions"][0]["target"] = None
        malformed_failures = ARCHIVE_AUDIT.validate_scenario_evidence(
            malformed,
            expected_domains=(domain,),
            primary_snapshot=primary,
            peer_snapshot=peer,
            primary_status=primary_status,
            peer_status=peer_status,
            collection_created_utc=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        )
        self.assertTrue(any("requires a target identity" in failure for failure in malformed_failures))

        replayed = json.loads(json.dumps(scenario))
        replayed["domains"][0]["actions"][1]["correlationToken"] = replayed["domains"][0]["actions"][0]["correlationToken"]
        replayed_failures = ARCHIVE_AUDIT.validate_scenario_evidence(
            replayed,
            expected_domains=(domain,),
            primary_snapshot=primary,
            peer_snapshot=peer,
            primary_status=primary_status,
            peer_status=peer_status,
            collection_created_utc=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        )
        self.assertTrue(any("replays a correlationToken" in failure for failure in replayed_failures))

        cross_session_peer = dict(peer)
        cross_session_peer_status = dict(peer_status)
        cross_session_peer["session.serverInstanceNonce"] = "100"
        cross_session_peer_status["serverInstanceNonce"] = "100"
        cross_session_failures = ARCHIVE_AUDIT.validate_scenario_evidence(
            scenario,
            expected_domains=(domain,),
            primary_snapshot=primary,
            peer_snapshot=cross_session_peer,
            primary_status=primary_status,
            peer_status=cross_session_peer_status,
            collection_created_utc=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        )
        self.assertIn(
            "scenario evidence serverInstanceNonce does not match peer archived session",
            cross_session_failures,
        )

    def test_scenario_shape_validation_rejects_stale_and_mislabeled_engine_proof(self) -> None:
        domain = "movement"
        stale_time = dt.datetime.now(dt.timezone.utc) - dt.timedelta(minutes=10)
        scenario, primary, peer, primary_status, peer_status = self.explicit_scenario((domain,))
        scenario = ARCHIVE_AUDIT.scenario_evidence_fixture(
            (domain,),
            primary=ARCHIVE_AUDIT._expected_scenario_client(primary, primary_status),
            peer=ARCHIVE_AUDIT._expected_scenario_client(peer, peer_status),
            server_instance_nonce=primary["session.serverInstanceNonce"],
            now=stale_time,
        )
        stale_failures = ARCHIVE_AUDIT.validate_scenario_evidence(
            scenario,
            expected_domains=(domain,),
            primary_snapshot=primary,
            peer_snapshot=peer,
            primary_status=primary_status,
            peer_status=peer_status,
            collection_created_utc=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        )
        self.assertIn("scenario evidence is stale relative to collection createdUtc", stale_failures)

        scenario, primary, peer, primary_status, peer_status = self.explicit_scenario((domain,))
        legacy = json.loads(json.dumps(scenario))
        for action in legacy["domains"][0]["actions"]:
            action["proof"]["kind"] = "engine_correlated"
        legacy_failures = ARCHIVE_AUDIT.validate_scenario_evidence(
            legacy,
            expected_domains=(domain,),
            primary_snapshot=primary,
            peer_snapshot=peer,
            primary_status=primary_status,
            peer_status=peer_status,
            collection_created_utc=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        )
        self.assertTrue(any("cannot claim native engine correlation" in failure for failure in legacy_failures))

        for action in scenario["domains"][0]["actions"]:
            action["proof"] = {"kind": "manual_unproven", "reason": "no engine action ID in this tranche"}
        manual_failures = ARCHIVE_AUDIT.validate_scenario_evidence(
            scenario,
            expected_domains=(domain,),
            primary_snapshot=primary,
            peer_snapshot=peer,
            primary_status=primary_status,
            peer_status=peer_status,
            collection_created_utc=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        )
        self.assertEqual(manual_failures, [])

    def test_strict_paired_scenario_certification_requires_native_trace_corroboration(self) -> None:
        canonical = COLLECT.vr_handoff.GAMEPLAY_MANDATORY_CANONICAL_DOMAINS
        for expected_domains in (canonical, ("movement",)):
            with self.subTest(expected_domains=expected_domains):
                scenario, primary, peer, primary_status, peer_status = self.explicit_scenario(expected_domains)
                payload = json.dumps(scenario, sort_keys=True).encode("utf-8")
                with tempfile.TemporaryDirectory(prefix="stvr-native-trace-") as temp:
                    root = pathlib.Path(temp)
                    primary_archive = root / "primary.zip"
                    peer_archive = root / "peer.zip"
                    for archive in (primary_archive, peer_archive):
                        with zipfile.ZipFile(archive, "w") as zf:
                            zf.writestr(ARCHIVE_AUDIT.SCENARIO_EVIDENCE_ARCHIVE_ENTRY, payload)
                    failures: list[str] = []
                    with zipfile.ZipFile(primary_archive) as primary_zf, zipfile.ZipFile(peer_archive) as peer_zf:
                        ARCHIVE_AUDIT.require_paired_scenario_evidence(
                            primary_zf,
                            set(primary_zf.namelist()),
                            peer_zf,
                            set(peer_zf.namelist()),
                            primary_snapshot=primary,
                            peer_snapshot=peer,
                            primary_status=primary_status,
                            peer_status=peer_status,
                            primary_manifest={
                                "createdUtc": dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")
                            },
                            expected_domains=expected_domains,
                            failures=failures,
                        )
                self.assertIn(ARCHIVE_AUDIT.NATIVE_TRACE_CORROBORATION_MISSING, failures)

    def test_paired_counters_accept_distinct_clients_with_shared_server(self) -> None:
        primary, peer, primary_status, peer_status = self.paired_session_evidence()
        self.assertEqual(primary["session.connectionGeneration"], "1")
        self.assertEqual(peer["session.connectionGeneration"], "2")
        failures: list[str] = []
        ARCHIVE_AUDIT.require_paired_session_evidence(
            primary,
            peer,
            primary_status,
            peer_status,
            failures,
        )
        self.assertEqual(failures, [])

    def test_paired_counters_reject_reused_client_identity(self) -> None:
        status_field_by_snapshot_field = {
            "session.id": "sessionId",
            "launchNonce": "launchNonce",
            "processId": "processId",
            "session.connectionGeneration": "connectionGeneration",
        }
        for snapshot_field, status_field in status_field_by_snapshot_field.items():
            with self.subTest(snapshot_field=snapshot_field):
                primary, peer, primary_status, peer_status = self.paired_session_evidence()
                peer[snapshot_field] = primary[snapshot_field]
                peer_status[status_field] = primary_status[status_field]
                failures: list[str] = []
                ARCHIVE_AUDIT.require_paired_session_evidence(
                    primary,
                    peer,
                    primary_status,
                    peer_status,
                    failures,
                )
                self.assertIn(
                    f"paired evidence must originate from distinct clients ({snapshot_field})",
                    failures,
                )

        primary, peer, primary_status, peer_status = self.paired_session_evidence()
        peer_status["playerId"] = primary_status["playerId"]
        failures = []
        ARCHIVE_AUDIT.require_paired_session_evidence(
            primary,
            peer,
            primary_status,
            peer_status,
            failures,
        )
        self.assertIn("paired evidence must originate from distinct players", failures)

    def test_paired_counters_require_generation_to_match_own_nonzero_status(self) -> None:
        primary, peer, primary_status, peer_status = self.paired_session_evidence()
        peer["session.connectionGeneration"] = "0"
        peer_status["connectionGeneration"] = "0"
        failures: list[str] = []
        ARCHIVE_AUDIT.require_paired_session_evidence(
            primary,
            peer,
            primary_status,
            peer_status,
            failures,
        )
        self.assertIn(
            "peer gameplay session.connectionGeneration is missing or zero",
            failures,
        )

        primary, peer, primary_status, peer_status = self.paired_session_evidence()
        peer["session.connectionGeneration"] = "3"
        failures = []
        ARCHIVE_AUDIT.require_paired_session_evidence(
            primary,
            peer,
            primary_status,
            peer_status,
            failures,
        )
        self.assertIn(
            "peer gameplay session.connectionGeneration does not match status connectionGeneration",
            failures,
        )

    def test_paired_counters_require_one_nonzero_shared_server_instance(self) -> None:
        primary, peer, primary_status, peer_status = self.paired_session_evidence()
        peer["session.serverInstanceNonce"] = "100"
        peer_status["serverInstanceNonce"] = "100"
        failures: list[str] = []
        ARCHIVE_AUDIT.require_paired_session_evidence(
            primary,
            peer,
            primary_status,
            peer_status,
            failures,
        )
        self.assertIn("paired evidence session.serverInstanceNonce differs", failures)

        primary, peer, primary_status, peer_status = self.paired_session_evidence()
        primary["session.serverInstanceNonce"] = "0"
        primary_status["serverInstanceNonce"] = "0"
        failures = []
        ARCHIVE_AUDIT.require_paired_session_evidence(
            primary,
            peer,
            primary_status,
            peer_status,
            failures,
        )
        self.assertIn(
            "primary paired evidence lacks nonzero session.serverInstanceNonce",
            failures,
        )

    def test_strict_gameplay_loss_counters_reject_harmful_loss(self) -> None:
        fixture = COLLECT.vr_handoff.gameplay_snapshot_fixture(pathlib.Path("/fixture"))
        for field, label in (
            ("bridge.eventRingDroppedPushes", "event-ring dropped pushes"),
            ("bridge.commandRingDroppedPushes", "command-ring dropped pushes"),
            ("bridge.rejectedCommands", "rejected commands"),
            ("bridge.discardedEvents", "discarded events"),
            ("bridge.rejectedSubmissions", "rejected submissions"),
        ):
            with self.subTest(field=field):
                values = COLLECT.vr_handoff.parse_key_value_text(fixture)
                values[field] = "1"
                failures: list[str] = []
                warnings: list[str] = []
                ARCHIVE_AUDIT.audit_gameplay_loss_counters(
                    values,
                    failures,
                    warnings,
                    label="primary",
                    strict=True,
                )
                self.assertEqual(warnings, [])
                self.assertIn(
                    f"primary gameplay snapshot reports harmful {label}: 1 unclassified of 1",
                    failures,
                )

    def test_strict_gameplay_loss_counters_require_nonnegative_values(self) -> None:
        fixture = COLLECT.vr_handoff.gameplay_snapshot_fixture(pathlib.Path("/fixture"))
        for value, detail in ((None, "missing or invalid"), ("-1", "missing or invalid")):
            with self.subTest(value=value):
                values = COLLECT.vr_handoff.parse_key_value_text(fixture)
                if value is None:
                    values.pop("bridge.commandRingDroppedPushes")
                else:
                    values["bridge.commandRingDroppedPushes"] = value
                failures: list[str] = []
                warnings: list[str] = []
                ARCHIVE_AUDIT.audit_gameplay_loss_counters(
                    values,
                    failures,
                    warnings,
                    label="primary",
                    strict=True,
                )
                self.assertEqual(warnings, [])
                self.assertIn(
                    f"primary gameplay snapshot bridge.commandRingDroppedPushes is {detail}",
                    failures,
                )

    def test_loss_counters_exempt_only_explicit_lifecycle_work(self) -> None:
        values = COLLECT.vr_handoff.parse_key_value_text(
            COLLECT.vr_handoff.gameplay_snapshot_fixture(pathlib.Path("/fixture"))
        )
        values.update(
            {
                "bridge.discardedEvents": "3",
                "bridge.discardedEvents.preReady": "1",
                "bridge.discardedEvents.lifecycleRetired": "2",
                "bridge.rejectedSubmissions": "4",
                "bridge.rejectedSubmissions.preReady": "3",
                "bridge.rejectedSubmissions.lifecycleRetired": "1",
            }
        )
        failures: list[str] = []
        warnings: list[str] = []
        ARCHIVE_AUDIT.audit_gameplay_loss_counters(
            values,
            failures,
            warnings,
            label="primary",
            strict=True,
        )
        self.assertEqual(failures, [])
        self.assertEqual(warnings, [])

        values["bridge.discardedEvents.lifecycleRetired"] = "3"
        ARCHIVE_AUDIT.audit_gameplay_loss_counters(
            values,
            failures,
            warnings,
            label="primary",
            strict=True,
        )
        self.assertIn(
            "primary gameplay snapshot bridge.discardedEvents lifecycle exclusions exceed the total (4>3)",
            failures,
        )

    def test_non_strict_gameplay_loss_counters_warn_without_failing(self) -> None:
        values = COLLECT.vr_handoff.parse_key_value_text(
            COLLECT.vr_handoff.gameplay_snapshot_fixture(pathlib.Path("/fixture"))
        )
        values["bridge.eventRingDroppedPushes"] = "2"
        failures: list[str] = []
        warnings: list[str] = []
        ARCHIVE_AUDIT.audit_gameplay_loss_counters(
            values,
            failures,
            warnings,
            label="primary",
            strict=False,
        )
        self.assertEqual(failures, [])
        self.assertIn(
            "primary gameplay snapshot reports harmful event-ring dropped pushes: 2 unclassified of 2",
            warnings,
        )

    def test_bootstrap_accepts_live_startup_breadcrumbs_without_shutdown(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-live-log-") as temp:
            log = pathlib.Path(temp) / "tp_client.log"
            breadcrumbs = [
                line
                for line in COLLECT.audit_runtime_handoff.LOG_BREADCRUMBS
                if "lifecycle shutdown hook reached" not in line
            ]
            log.write_text("\n".join(breadcrumbs), encoding="utf-8")

            self.assertFalse(COLLECT.log_breadcrumb_detail(log, skip_log=False)[0])
            self.assertTrue(COLLECT.log_breadcrumb_detail(log, skip_log=False, require_shutdown=False)[0])

    def test_local_avatar_bootstrap_requires_no_remote_avatar_rows(self) -> None:
        avatar = {
            "schema": "commonlib_bridge_v2",
            "ready": "1",
            "connected": "1",
            "bridgeReady": "1",
            "actorTargetsEnabled": "1",
            "animationGraphEnabled": "1",
            "localAnimationGraphReady": "1",
            "localSnapshotReady": "1",
            "localServerAssigned": "1",
            "actorSkeletonWritesEnabled": "0",
            "cleanupRequired": "0",
            "visualPolicy": "player_template_fallback",
            "lifecycleEpoch": "3",
            "localServerId": "7",
        }
        self.assertTrue(COLLECT.local_avatar_bootstrap_detail(avatar)[0])

    def test_collector_gameplay_identity_rejects_non_gameplay_flavor(self) -> None:
        manifest = self.package_manifest(gameplay=True)
        manifest["packageFlavor"] = "avatar-sync"
        ok, detail = COLLECT.validate_build_manifest_data(manifest, avatar_sync=False, gameplay=True)
        self.assertFalse(ok)
        self.assertIn("packageFlavor='avatar-sync' expected='gameplay'", detail)

    def test_archive_auditor_propagates_gameplay_bootstrap_scope(self) -> None:
        with mock.patch.object(ARCHIVE_AUDIT, "audit_archive", return_value=0) as audit:
            with mock.patch.object(
                sys,
                "argv",
                ["audit_runtime_evidence_zip.py", "fixture.zip", "--require-gameplay-bootstrap"],
            ):
                self.assertEqual(ARCHIVE_AUDIT.main(), 0)

        kwargs = audit.call_args.kwargs
        self.assertTrue(kwargs["require_gameplay_bootstrap"])
        self.assertFalse(kwargs["require_avatar_sync"])
        self.assertFalse(kwargs["require_gameplay"])
        self.assertFalse(kwargs["require_remote_player"])
        self.assertFalse(kwargs["require_movement_relay"])
        self.assertFalse(kwargs["require_higgs_relay"])

    def test_generic_and_crash_only_collections_are_untrusted(self) -> None:
        for crash_evidence in (False, True):
            with self.subTest(crash_evidence=crash_evidence), tempfile.TemporaryDirectory(prefix="stvr-untrusted-evidence-") as temp:
                root = pathlib.Path(temp)
                game = root / "SkyrimVR"
                handoff = game / "Data" / "SkyrimTogetherReborn"
                handoff.mkdir(parents=True)
                (game / COLLECT.BUILD_MANIFEST_NAME).write_text(
                    json.dumps(self.package_manifest()), encoding="utf-8"
                )
                args = COLLECT.build_collection_args(
                    game_path=game,
                    handoff_dir=handoff,
                    out=root / "evidence.zip",
                    crash_evidence=crash_evidence,
                    no_audit=True,
                )
                trusted_identity = {"schema": "skyrim_together_vr_runtime_identity_v1", "ok": True, "reasons": []}
                with mock.patch.object(COLLECT.vr_handoff, "evaluate_runtime_identity", return_value=trusted_identity):
                    archive = COLLECT.collect(args)
                with zipfile.ZipFile(archive) as zf:
                    manifest = json.loads(zf.read("manifest.json"))
                self.assertFalse(manifest["liveAdmissionRequested"])
                self.assertEqual(manifest["runtimeEvidenceTrust"], "untrusted")

    def test_collector_copies_precreated_scenario_evidence_without_rewriting_it(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-scenario-collector-") as temp:
            root = pathlib.Path(temp)
            game = root / "SkyrimVR"
            handoff = game / "Data" / "SkyrimTogetherReborn"
            handoff.mkdir(parents=True)
            (game / COLLECT.BUILD_MANIFEST_NAME).write_text(
                json.dumps(self.package_manifest()), encoding="utf-8"
            )
            source = root / "prepared-scenario.json"
            payload = b'{"precreated":"collector must not rewrite this"}\n'
            source.write_bytes(payload)
            args = COLLECT.build_collection_args(
                game_path=game,
                handoff_dir=handoff,
                out=root / "evidence.zip",
                scenario_evidence=source,
                no_audit=True,
            )
            identity = {"schema": "skyrim_together_vr_runtime_identity_v1", "ok": False, "reasons": ["fixture"]}
            with mock.patch.object(COLLECT.vr_handoff, "evaluate_runtime_identity", return_value=identity):
                archive = COLLECT.collect(args)
            with zipfile.ZipFile(archive) as zf:
                self.assertEqual(zf.read(ARCHIVE_AUDIT.SCENARIO_EVIDENCE_ARCHIVE_ENTRY), payload)
                manifest = json.loads(zf.read("manifest.json"))
                self.assertTrue(manifest["scenarioEvidence"]["provided"])

    def test_failed_live_admission_returns_nonzero_and_preserves_archive(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-live-admission-cli-") as temp:
            archive = pathlib.Path(temp) / "failed.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr(
                    "manifest.json",
                    json.dumps({"liveAdmissionRequested": True, "runtimeEvidenceTrust": "untrusted"}),
                )
            with mock.patch.object(COLLECT, "collect", return_value=archive):
                with mock.patch.object(sys, "argv", ["collect_runtime_evidence.py", "--require-connected"]):
                    self.assertEqual(COLLECT.main(), 1)
            self.assertTrue(archive.is_file())

    def test_offline_identity_audit_rejects_package_network_version_mismatch(self) -> None:
        now_ns = time.time_ns()
        identity_readouts = {
            "status": (
                "online=1\nlaunchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\n"
                "clientVersion=other\nserverVersion=other\ngameplayProtocolRevision=17\n"
                "serverInstanceNonce=1\nsessionId=1\nconnectionGeneration=1\ngamePath=/fixture\n"
            ),
            "lifecycle": "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\ngamePath=/fixture\n",
            "playercell": "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\ngamePath=/fixture\n",
            "avatar": "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\ngamePath=/fixture\n",
            "gameplay": COLLECT.vr_handoff.gameplay_snapshot_fixture(
                pathlib.Path("/fixture"), session_id=1, server_instance_nonce=1, connection_generation=1
            ),
        }
        with tempfile.TemporaryDirectory(prefix="stvr-offline-network-version-") as temp:
            archive = pathlib.Path(temp) / "evidence.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                for name, contents in identity_readouts.items():
                    zf.writestr(f"handoff/{COLLECT.vr_handoff.READOUT_FILES[name]}", contents)
                failures: list[str] = []
                ARCHIVE_AUDIT.audit_archived_runtime_identity(
                    zf,
                    set(zf.namelist()),
                    {"gamePath": "/fixture", "files": []},
                    {
                        "maxReadoutAgeSeconds": 30,
                        "evaluatedAtNs": now_ns,
                        "readouts": {
                            name: {"mtimeNs": now_ns, "present": True}
                            for name in COLLECT.vr_handoff.RUNTIME_IDENTITY_READOUTS
                        },
                    },
                    "fixture",
                    failures,
                )
        self.assertIn("status clientVersion does not match package networkVersion", "\n".join(failures))

    def test_sparse_one_client_gameplay_bootstrap_collects_and_audits(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-sparse-bootstrap-") as temp:
            root = pathlib.Path(temp)
            game = root / "SkyrimVR"
            handoff = game / "Data" / "SkyrimTogetherReborn"
            out = root / "out"
            handoff.mkdir(parents=True)

            package_manifest = {
                "schema": COLLECT.BUILD_MANIFEST_SCHEMA,
                "mode": "releasedbg",
                "platform": "windows",
                "arch": "x64",
                "avatarSync": False,
                "gameplay": True,
                "packageFlavor": "gameplay",
                "targets": list(COLLECT.GAMEPLAY_EXPECTED_MANIFEST_TARGETS),
                "copiedArtifacts": list(COLLECT.GAMEPLAY_EXPECTED_RUNTIME_ARTIFACTS),
                "stagedGameFiles": True,
                "companionPanel": True,
                "buildVersion": "fixture",
                "networkVersion": "fixture",
                "sourceRevision": "0" * 40,
                "sourceProvenance": {
                    "revision": "0" * 40,
                    "sourceTreeSha256": "1" * 64,
                    "dirty": False,
                    "dirtyApproved": False,
                },
            }
            (game / COLLECT.BUILD_MANIFEST_NAME).write_text(json.dumps(package_manifest), encoding="utf-8")

            readouts = {
                "status": (
                    "state=online\nonline=1\nplayerId=4\nsessionId=123\nconnectionGeneration=1\n"
                    "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\n"
                    "clientVersion=fixture\nserverVersion=fixture\ngameplayProtocolRevision=17\n"
                    "serverInstanceNonce=99\ngamePath={}\n".format(game)
                ),
                "lifecycle": (
                    "state=ready\nready=1\nepoch=3\nownerThreadId=1\nstableTickCount=4\n"
                    "playerFormId=20\nplayerBaseFormId=7\nplayerCellFormId=100\n"
                    "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\n"
                    "gamePath={}\n".format(game)
                ),
                "gameplay": COLLECT.vr_handoff.gameplay_snapshot_fixture(game),
                "pose": (
                    "localPoseAvailable=1\nlocal.hmd.valid=1\nlocal.leftHand.valid=1\n"
                    "local.rightHand.valid=1\nlocal.vrik.detected=1\nlocal.vrik.interfaceAvailable=1\n"
                ),
                "avatar": (
                    "schema=commonlib_bridge_v2\nready=1\nconnected=1\nbridgeReady=1\n"
                    "actorTargetsEnabled=1\nanimationGraphEnabled=1\nlocalAnimationGraphReady=1\n"
                    "localSnapshotReady=1\nlocalServerAssigned=1\nactorSkeletonWritesEnabled=0\n"
                    "cleanupRequired=0\nvisualPolicy=player_template_fallback\nlifecycleEpoch=3\nlocalServerId=7\n"
                    "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\n"
                    "gamePath={}\n".format(game)
                ),
                "playercell": (
                    "ready=1\nonline=1\nlocalPlayerId=4\nsessionId=123\nconnectionGeneration=1\n"
                    "playerFormId=20\ncurrentLevel=1\ncachedLevel=1\nlastLevelSent=1\n"
                    "gridCellRequestCount=1\nexteriorCellRequestCount=1\ninteriorCellRequestCount=0\n"
                    "levelRequestCount=1\nofflineSkippedRequestCount=0\nworldSpaceTranslationFailureCount=0\n"
                    "lastGrid.valid=1\nlastGrid.cellCount=25\nlastGrid.connectionGeneration=1\n"
                    "lastGrid.center=0,0\nlastGrid.worldSpace.serverModId=1\nlastGrid.worldSpace.serverBaseId=60\n"
                    "lastGrid.playerCell.serverModId=1\nlastGrid.playerCell.serverBaseId=100\n"
                    "lastCell.valid=1\nlastCell.exterior=1\nlastCell.connectionGeneration=1\n"
                    "lastCell.currentCoords=0,0\nlastCell.cell.serverModId=1\nlastCell.cell.serverBaseId=100\n"
                    "lastCell.worldSpace.serverModId=1\nlastCell.worldSpace.serverBaseId=60\n"
                    "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\n"
                    "gamePath={}\n".format(game)
                ),
                "higgs": "bridge.loaded=1\nbridge.sequence=4\nhiggs.detected=1\nhiggs.interfaceAvailable=1\n",
                # Present full-lane artifacts remain collectible but are not required to pass.
                "movement": "localMovementAvailable=1\nremoteMovementCount=0\n",
            }
            for name, content in readouts.items():
                (handoff / COLLECT.vr_handoff.READOUT_FILES[name]).write_text(content, encoding="utf-8")

            args = COLLECT.build_collection_args(
                game_path=game,
                handoff_dir=handoff,
                log=root / "missing-tp_client.log",
                gameplay_bridge_log=root / "missing-gameplay-bridge.log",
                out=out,
                require_connected=True,
                require_vrik=True,
                require_higgs=True,
                gameplay_bootstrap=True,
            )
            original_capture = COLLECT.vr_handoff.capture_readout_snapshots

            def capture_then_mutate(directory: pathlib.Path, names: tuple[str, ...]):
                snapshots = original_capture(directory, names)
                (handoff / COLLECT.vr_handoff.READOUT_FILES["pose"]).write_text("localPoseAvailable=0\n", encoding="utf-8")
                (handoff / COLLECT.vr_handoff.READOUT_FILES["higgs"]).write_text("bridge.loaded=0\n", encoding="utf-8")
                return snapshots

            with mock.patch.object(COLLECT.vr_handoff, "capture_readout_snapshots", side_effect=capture_then_mutate):
                archive = COLLECT.collect(args)

            with zipfile.ZipFile(archive) as zf:
                manifest = json.loads(zf.read("manifest.json"))
                checklist = json.loads(zf.read("runtime_checklist.json"))
                checks = {check["id"]: check for check in checklist["checks"]}
                files = {record["archiveName"]: record for record in manifest["files"]}
                self.assertEqual(manifest["missingRequired"], [])
                self.assertEqual(manifest["runtimeAuditExitCode"], 0)
                self.assertEqual(checklist["summary"][COLLECT.CHECK_FAIL], 0)
                for check_id in ("gameplay_bridge_log", "inventory_boundary", "planck_bridge"):
                    self.assertEqual(checks[check_id]["status"], COLLECT.CHECK_NOT_REQUIRED)
                self.assertIn(
                    f"handoff/{COLLECT.vr_handoff.READOUT_FILES['movement']}",
                    zf.namelist(),
                )
                movement_name = f"handoff/{COLLECT.vr_handoff.READOUT_FILES['movement']}"
                self.assertTrue(files[movement_name]["exists"])
                self.assertFalse(files[movement_name]["required"])
                for name in ("activation", "combat", "grab", "inventory", "magic", "projectile", "saveload", "planck"):
                    archive_name = f"handoff/{COLLECT.vr_handoff.READOUT_FILES[name]}"
                    self.assertFalse(files[archive_name]["exists"])
                    self.assertFalse(files[archive_name]["required"])
                self.assertNotIn(f"logs/{COLLECT.GAMEPLAY_BRIDGE_LOG_NAME}", zf.namelist())
                self.assertIn("localPoseAvailable=1", zf.read(f"handoff/{COLLECT.vr_handoff.READOUT_FILES['pose']}").decode("utf-8"))
                self.assertIn("bridge.loaded=1", zf.read(f"handoff/{COLLECT.vr_handoff.READOUT_FILES['higgs']}").decode("utf-8"))
                bridge_record = files[f"logs/{COLLECT.GAMEPLAY_BRIDGE_LOG_NAME}"]
                self.assertFalse(bridge_record["exists"])
                self.assertFalse(bridge_record["required"])

            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = ARCHIVE_AUDIT.audit_archive(
                    archive,
                    require_avatar_sync=False,
                    require_gameplay=False,
                    require_remote_player=False,
                    require_weapon_pose=False,
                    require_magic_pose=False,
                    require_projectile_pose=False,
                    require_movement_relay=False,
                    require_equipment_relay=False,
                    require_activation_relay=False,
                    require_magic_relay=False,
                    require_combat_relay=False,
                    require_projectile_relay=False,
                    require_grab_relay=False,
                    require_higgs_relay=False,
                    require_saveload_observer=False,
                    allow_failed_checks=False,
                    require_gameplay_bootstrap=True,
                )
            self.assertEqual(result, 0, output.getvalue())

    def test_gameplay_bootstrap_rejects_mismatched_package_flavor(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-runtime-evidence-scope-") as temp:
            archive = pathlib.Path(temp) / "mismatched-flavor.zip"
            package_manifest = {
                "schema": COLLECT.BUILD_MANIFEST_SCHEMA,
                "mode": "releasedbg",
                "platform": "windows",
                "arch": "x64",
                "avatarSync": False,
                "gameplay": True,
                "packageFlavor": "avatar-sync",
                "targets": list(COLLECT.GAMEPLAY_EXPECTED_MANIFEST_TARGETS),
                "copiedArtifacts": list(COLLECT.GAMEPLAY_EXPECTED_RUNTIME_ARTIFACTS),
                "stagedGameFiles": True,
                "companionPanel": True,
                "buildVersion": "fixture",
                "networkVersion": "fixture",
                "sourceRevision": "0" * 40,
                "sourceProvenance": {
                    "revision": "0" * 40,
                    "sourceTreeSha256": "1" * 64,
                    "dirty": False,
                    "dirtyApproved": False,
                },
            }
            check_ids = set(ARCHIVE_AUDIT.REQUIRED_CHECK_IDS)
            check_ids.update(check_id for check_id, _ in ARCHIVE_AUDIT.GAMEPLAY_BOOTSTRAP_CHECKS)
            checks = [{"id": check_id, "status": COLLECT.CHECK_PASS, "detail": "fixture"} for check_id in check_ids]
            checklist = {
                "schema": "skyrim_together_vr_runtime_checklist_v1",
                "summary": {COLLECT.CHECK_PASS: len(checks), COLLECT.CHECK_FAIL: 0, COLLECT.CHECK_NOT_REQUIRED: 0},
                "checks": checks,
            }
            manifest = {
                "schema": "skyrim_together_vr_runtime_evidence_v1",
                "runtimeAuditExitCode": 0,
                "missingRequired": [],
                "avatarSyncAudit": False,
                "gameplayAudit": False,
                "gameplayBootstrapAudit": True,
                "packageBuildManifest": package_manifest,
                "runtimeChecklist": checklist["summary"],
                "files": [],
            }
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("manifest.json", json.dumps(manifest))
                zf.writestr(f"package/{COLLECT.BUILD_MANIFEST_NAME}", json.dumps(package_manifest))
                zf.writestr("runtime_checklist.json", json.dumps(checklist))
                zf.writestr("runtime_checklist.txt", "fixture\n")
                zf.writestr("runtime_audit.txt", "fixture\n")
                zf.writestr("logs/tp_client.log", "fixture\n")

            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = ARCHIVE_AUDIT.audit_archive(
                    archive,
                    require_avatar_sync=False,
                    require_gameplay=False,
                    require_remote_player=False,
                    require_weapon_pose=False,
                    require_magic_pose=False,
                    require_projectile_pose=False,
                    require_movement_relay=False,
                    require_equipment_relay=False,
                    require_activation_relay=False,
                    require_magic_relay=False,
                    require_combat_relay=False,
                    require_projectile_relay=False,
                    require_grab_relay=False,
                    require_higgs_relay=False,
                    require_saveload_observer=False,
                    allow_failed_checks=False,
                    require_gameplay_bootstrap=True,
                )
            self.assertEqual(result, 1)
            self.assertIn("packageFlavor='avatar-sync' expected='gameplay'", output.getvalue())

    def test_archive_rejects_individually_valid_package_manifest_tamper(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-package-manifest-tamper-") as temp:
            archive = pathlib.Path(temp) / "tampered-package-manifest.zip"
            package_manifest = self.package_manifest(gameplay=True)
            embedded_package_manifest = json.loads(json.dumps(package_manifest))
            embedded_package_manifest["sourceRevision"] = "2" * 40
            embedded_package_manifest["sourceProvenance"]["revision"] = "2" * 40
            self.assertTrue(COLLECT.validate_build_manifest_data(package_manifest, avatar_sync=False, gameplay=True)[0])
            self.assertTrue(
                COLLECT.validate_build_manifest_data(
                    embedded_package_manifest,
                    avatar_sync=False,
                    gameplay=True,
                )[0]
            )

            checks = [
                {"id": check_id, "status": COLLECT.CHECK_PASS, "detail": "fixture"}
                for check_id in ARCHIVE_AUDIT.REQUIRED_CHECK_IDS
            ]
            checklist = {
                "schema": "skyrim_together_vr_runtime_checklist_v1",
                "summary": {
                    COLLECT.CHECK_PASS: len(checks),
                    COLLECT.CHECK_FAIL: 0,
                    COLLECT.CHECK_NOT_REQUIRED: 0,
                },
                "checks": checks,
            }
            manifest = {
                "schema": "skyrim_together_vr_runtime_evidence_v1",
                "runtimeAuditExitCode": 0,
                "missingRequired": [],
                "avatarSyncAudit": False,
                "gameplayAudit": True,
                "gameplayBootstrapAudit": False,
                "packageBuildManifest": embedded_package_manifest,
                "runtimeChecklist": checklist["summary"],
                "runtimeIdentity": {
                    "schema": "skyrim_together_vr_runtime_identity_v1",
                    "ok": False,
                    "reasons": ["not requested"],
                },
                "runtimeEvidenceTrust": "untrusted",
                "liveAdmissionRequested": False,
                "files": [],
            }
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("manifest.json", json.dumps(manifest))
                zf.writestr(f"package/{COLLECT.BUILD_MANIFEST_NAME}", json.dumps(package_manifest))
                zf.writestr("runtime_checklist.json", json.dumps(checklist))
                zf.writestr("runtime_checklist.txt", "fixture\n")
                zf.writestr("runtime_audit.txt", "fixture\n")
                zf.writestr("logs/tp_client.log", "fixture\n")

            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = ARCHIVE_AUDIT.audit_archive(
                    archive,
                    require_avatar_sync=False,
                    require_gameplay=False,
                    require_remote_player=False,
                    require_weapon_pose=False,
                    require_magic_pose=False,
                    require_projectile_pose=False,
                    require_movement_relay=False,
                    require_equipment_relay=False,
                    require_activation_relay=False,
                    require_magic_relay=False,
                    require_combat_relay=False,
                    require_projectile_relay=False,
                    require_grab_relay=False,
                    require_higgs_relay=False,
                    require_saveload_observer=False,
                    allow_failed_checks=True,
                )
            self.assertEqual(result, 1)
            self.assertIn(
                "archived package build manifest does not exactly match manifest.json packageBuildManifest",
                output.getvalue(),
            )


if __name__ == "__main__":
    unittest.main()
