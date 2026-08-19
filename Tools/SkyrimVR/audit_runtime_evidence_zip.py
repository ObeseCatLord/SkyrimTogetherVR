#!/usr/bin/env python3
"""Audit a SkyrimTogetherVR runtime evidence zip without needing the game folder."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import re
import tempfile
import zipfile

import collect_runtime_evidence


REQUIRED_ARCHIVE_ENTRIES = (
    "manifest.json",
    "package/SkyrimTogetherVR_BuildManifest.json",
    "runtime_audit.txt",
    "runtime_checklist.json",
    "runtime_checklist.txt",
    "logs/tp_client.log",
    "handoff/SkyrimTogetherVR.gameplay",
)

REQUIRED_CHECK_IDS = (
    "package_build_manifest",
    "startup_breadcrumbs",
    "lifecycle_schema",
    "connection_status",
    "gameplay_readiness",
    "local_pose",
    "local_vrik_api",
    "weapon_pose_context",
    "magic_pose_context",
    "projectile_pose_context",
    "remote_player_proxy",
    "discovery_schema",
    "player_cell_status",
    "remote_avatar_ready",
    "remote_higgs_avatar_ready",
    "movement_relay",
    "equipment_relay",
    "activation_relay",
    "magic_relay",
    "combat_relay",
    "projectile_relay",
    "grab_relay",
    "vr_physics_compatibility",
    "higgs_bridge",
    "planck_bridge",
    "higgs_relay",
    "saveload_observer",
    "avatar_sync_actor_targets",
    "runtime_identity",
)


MANIFEST_FLAG_REQUIRED_CHECKS = (
    ("requiredConnected", "connection_status", "connection status"),
    ("requiredVrik", "local_vrik_api", "local VRIK API"),
    ("requiredHiggs", "higgs_bridge", "HIGGS bridge"),
    ("requiredRemotePlayer", "remote_player_proxy", "remote-player proxy"),
    ("requiredRemotePlayer", "remote_avatar_ready", "remote VRIK avatar readiness"),
    ("requiredWeaponPose", "weapon_pose_context", "weapon pose context"),
    ("requiredMagicPose", "magic_pose_context", "magic pose context"),
    ("requiredProjectilePose", "projectile_pose_context", "projectile pose context"),
)

GAMEPLAY_BOOTSTRAP_CHECKS = (
    ("runtime_identity", "fresh same-process runtime identity"),
    ("connection_status", "connection status"),
    ("gameplay_readiness", "aggregate gameplay readiness"),
    ("live_lifecycle", "live lifecycle readiness"),
    ("live_player_cell", "live player-cell readiness"),
    ("local_pose", "local avatar pose"),
    ("local_avatar_bootstrap", "local-avatar bridge state"),
    ("local_vrik_api", "local VRIK API"),
    ("higgs_bridge", "local HIGGS bridge"),
)


BRIDGE_LOSS_COUNTERS = (
    ("bridge.eventRingDroppedPushes", "event-ring dropped pushes", ()),
    ("bridge.commandRingDroppedPushes", "command-ring dropped pushes", ()),
    ("bridge.rejectedCommands", "rejected commands", ()),
    (
        "bridge.discardedEvents",
        "discarded events",
        ("preReady", "lifecycleRetired"),
    ),
    (
        "bridge.rejectedSubmissions",
        "rejected submissions",
        ("preReady", "lifecycleRetired"),
    ),
)


SCENARIO_EVIDENCE_ARCHIVE_ENTRY = "scenario/explicit-scenario-evidence.json"
SCENARIO_EVIDENCE_SCHEMA = "skyrim_together_vr_explicit_scenario_evidence_v1"
SCENARIO_NONCE_HEX_LENGTH = 64
SCENARIO_MAX_WINDOW_SECONDS = 15 * 60
SCENARIO_MAX_COLLECTION_LAG_SECONDS = 5 * 60
SCENARIO_MAX_CLOCK_SKEW_SECONDS = 30
SCENARIO_MAX_ACTION_SECONDS = 2 * 60
SCENARIO_CLIENT_FIELDS = (
    "playerId",
    "sessionId",
    "connectionGeneration",
    "launchNonce",
    "processId",
)
SCENARIO_TARGET_REQUIRED_DOMAINS = frozenset(
    (
        "animation",
        "appearance",
        "equipment",
        "inventory",
        "actor_state",
        "object",
        "combat",
        "projectile",
        "magic",
        "quest",
        "dialogue",
        "npc_ownership",
    )
)
# The current bridge does not emit a wire-level action identifier or digest for
# these fields.  A human observation is retained as context, never promoted to
# automatic proof by this validator.
SCENARIO_MANUAL_OBSERVATION_DOMAINS = frozenset(("vr_body_pose", "audio"))
NATIVE_TRACE_CORROBORATION_MISSING = (
    "native-trace-corroboration-missing: scenario proof values are operator-supplied JSON; "
    "no archived native engine/server trace is available to cross-check action identifiers "
    "or payload digests, so strict gameplay certification is unavailable"
)
_HEX_256 = re.compile(rf"[0-9a-f]{{{SCENARIO_NONCE_HEX_LENGTH}}}\Z")


def _scenario_string(value: object) -> str:
    return value.strip() if isinstance(value, str) else ""


def _scenario_nonzero(value: object) -> bool:
    text = _scenario_string(value)
    if not text:
        return False
    try:
        return int(text, 10) > 0
    except ValueError:
        return False


def _scenario_token(value: object) -> bool:
    return bool(_HEX_256.fullmatch(_scenario_string(value)))


def _parse_utc(
    value: object,
    label: str,
    failures: list[str],
    *,
    require_z: bool = True,
) -> dt.datetime | None:
    text = _scenario_string(value)
    if require_z and not text.endswith("Z"):
        failures.append(f"scenario evidence {label} must be an RFC 3339 UTC timestamp ending in Z")
        return None
    try:
        parsed = dt.datetime.fromisoformat(text[:-1] + "+00:00" if text.endswith("Z") else text)
    except ValueError:
        failures.append(f"scenario evidence {label} is not a valid UTC timestamp")
        return None
    if parsed.tzinfo != dt.timezone.utc:
        failures.append(f"scenario evidence {label} must use UTC")
        return None
    return parsed


def _expected_scenario_client(
    snapshot: dict[str, str],
    status: dict[str, str],
) -> dict[str, str]:
    """Return the one client identity an explicit scenario record must bind."""
    return {
        "playerId": status.get("playerId", "").strip(),
        "sessionId": snapshot.get("session.id", "").strip(),
        "connectionGeneration": snapshot.get("session.connectionGeneration", "").strip(),
        "launchNonce": snapshot.get("launchNonce", "").strip(),
        "processId": snapshot.get("processId", "").strip(),
    }


def scenario_evidence_fixture(
    domains: tuple[str, ...],
    *,
    primary: dict[str, str],
    peer: dict[str, str],
    server_instance_nonce: str,
    now: dt.datetime | None = None,
) -> dict[str, object]:
    """Return a synthetic operator-supplied scenario fixture for shape tests only."""
    now = now or dt.datetime.now(dt.timezone.utc)
    start = now - dt.timedelta(seconds=len(domains) * 4 + 4)

    def utc(offset: int) -> str:
        return (start + dt.timedelta(seconds=offset)).isoformat(timespec="seconds").replace("+00:00", "Z")

    def token(seed: str) -> str:
        return hashlib.sha256(seed.encode("ascii")).hexdigest()

    domain_records: list[dict[str, object]] = []
    for index, domain in enumerate(domains):
        actions: list[dict[str, object]] = []
        for direction, (initiator, receiver) in enumerate((("primary", "peer"), ("peer", "primary"))):
            action: dict[str, object] = {
                "correlationToken": token(f"fixture:{domain}:{initiator}:{receiver}"),
                "initiator": initiator,
                "receiver": receiver,
                "initiatedUtc": utc(index * 4 + direction * 2),
                "receivedUtc": utc(index * 4 + direction * 2 + 1),
                "target": (
                    {"kind": "fixture_target", "id": f"fixture:{domain}"}
                    if domain in SCENARIO_TARGET_REQUIRED_DOMAINS
                    else None
                ),
                "proof": {
                    "kind": "operator_supplied",
                    "actionIdentifier": f"fixture.{domain}.{initiator}_to_{receiver}",
                    "payloadDigestSha256": token(f"payload:{domain}:{initiator}:{receiver}"),
                },
                "receiverObservation": {
                    "status": "applied",
                    "observedUtc": utc(index * 4 + direction * 2 + 1),
                    "postState": {"fixture": "applied"},
                },
            }
            if domain in SCENARIO_MANUAL_OBSERVATION_DOMAINS:
                action["manualHumanObservation"] = {
                    "kind": "manual_human_observation",
                    "observer": "fixture-observer",
                    "observedUtc": utc(index * 4 + direction * 2 + 1),
                    "statement": "Fixture manual observation; not automatic proof.",
                }
            actions.append(action)
        domain_records.append({"domain": domain, "actions": actions})

    return {
        "schema": SCENARIO_EVIDENCE_SCHEMA,
        "scenarioNonce": token("fixture:scenario"),
        "serverInstanceNonce": server_instance_nonce,
        "window": {"startedUtc": utc(0), "endedUtc": utc(max(2, len(domains) * 4 + 2))},
        "clients": {"primary": primary, "peer": peer},
        "domains": domain_records,
    }


def validate_scenario_evidence(
    evidence: object,
    *,
    expected_domains: tuple[str, ...],
    primary_snapshot: dict[str, str] | None = None,
    peer_snapshot: dict[str, str] | None = None,
    primary_status: dict[str, str] | None = None,
    peer_status: dict[str, str] | None = None,
    collection_created_utc: object | None = None,
    strict_gameplay_certification: bool = False,
) -> list[str]:
    """Validate operator-supplied scenario-record shape without treating counters as proof.

    The artifact is intentionally unsigned operator input. This checks shape,
    role/session bindings, and bounded timing only. It does not corroborate an
    action identifier or digest against a native engine/server trace, and a
    manual observation remains context rather than automatic proof.
    """
    failures: list[str] = []
    if not isinstance(evidence, dict):
        return ["scenario evidence root must be a JSON object"]
    if evidence.get("schema") != SCENARIO_EVIDENCE_SCHEMA:
        failures.append(f"scenario evidence schema is invalid: {evidence.get('schema')!r}")
    scenario_nonce = evidence.get("scenarioNonce")
    if not _scenario_token(scenario_nonce):
        failures.append("scenario evidence scenarioNonce must be a 256-bit lowercase hexadecimal token")
    server_nonce = evidence.get("serverInstanceNonce")
    if not _scenario_nonzero(server_nonce):
        failures.append("scenario evidence serverInstanceNonce must be a nonzero decimal string")

    window = evidence.get("window")
    start: dt.datetime | None = None
    end: dt.datetime | None = None
    if not isinstance(window, dict):
        failures.append("scenario evidence window must be an object")
    else:
        start = _parse_utc(window.get("startedUtc"), "window.startedUtc", failures)
        end = _parse_utc(window.get("endedUtc"), "window.endedUtc", failures)
        if start is not None and end is not None:
            if end < start:
                failures.append("scenario evidence window ends before it starts")
            elif (end - start).total_seconds() > SCENARIO_MAX_WINDOW_SECONDS:
                failures.append(
                    "scenario evidence window exceeds "
                    f"{SCENARIO_MAX_WINDOW_SECONDS} seconds"
                )

    reference: dt.datetime | None = None
    if collection_created_utc is not None:
        reference = _parse_utc(
            collection_created_utc,
            "collection createdUtc",
            failures,
            require_z=False,
        )
    if reference is not None and end is not None:
        if end < reference - dt.timedelta(seconds=SCENARIO_MAX_COLLECTION_LAG_SECONDS):
            failures.append("scenario evidence is stale relative to collection createdUtc")
        if end > reference + dt.timedelta(seconds=SCENARIO_MAX_CLOCK_SKEW_SECONDS):
            failures.append("scenario evidence window ends after collection createdUtc")

    clients = evidence.get("clients")
    scenario_clients: dict[str, dict[str, str]] = {}
    if not isinstance(clients, dict) or set(clients) != {"primary", "peer"}:
        failures.append("scenario evidence clients must contain exactly primary and peer")
    else:
        for role in ("primary", "peer"):
            client = clients.get(role)
            if not isinstance(client, dict):
                failures.append(f"scenario evidence client {role} must be an object")
                continue
            normalized: dict[str, str] = {}
            for field in SCENARIO_CLIENT_FIELDS:
                value = _scenario_string(client.get(field))
                valid = _nonzero_identifier(value) if field == "launchNonce" else _scenario_nonzero(value)
                if not valid:
                    failures.append(f"scenario evidence client {role} has invalid {field}")
                normalized[field] = value
            scenario_clients[role] = normalized
        if len(scenario_clients) == 2:
            for field in SCENARIO_CLIENT_FIELDS:
                if scenario_clients["primary"].get(field) == scenario_clients["peer"].get(field):
                    failures.append(f"scenario evidence clients must be distinct ({field})")

    expected_bindings = (
        ("primary", primary_snapshot, primary_status),
        ("peer", peer_snapshot, peer_status),
    )
    for role, snapshot, status in expected_bindings:
        if snapshot is None or status is None:
            continue
        expected = _expected_scenario_client(snapshot, status)
        actual = scenario_clients.get(role, {})
        for field, expected_value in expected.items():
            if actual.get(field) != expected_value:
                failures.append(f"scenario evidence client {role} {field} does not match archived session")
        expected_server_nonce = snapshot.get("session.serverInstanceNonce", "").strip()
        if _scenario_string(server_nonce) != expected_server_nonce:
            failures.append(f"scenario evidence serverInstanceNonce does not match {role} archived session")

    domains = evidence.get("domains")
    domain_records: dict[str, dict[str, object]] = {}
    if not isinstance(domains, list):
        failures.append("scenario evidence domains must be a list")
        domains = []
    for row in domains:
        if not isinstance(row, dict):
            failures.append("scenario evidence contains a non-object domain record")
            continue
        domain = _scenario_string(row.get("domain"))
        if not domain:
            failures.append("scenario evidence domain is missing its name")
            continue
        if domain in domain_records:
            failures.append(f"scenario evidence repeats domain {domain}")
            continue
        domain_records[domain] = row
    for domain in expected_domains:
        if domain not in domain_records:
            failures.append(f"scenario evidence is missing required domain {domain}")

    used_tokens: set[str] = set()
    for domain, row in domain_records.items():
        actions = row.get("actions")
        if not isinstance(actions, list) or not actions:
            failures.append(f"scenario evidence domain {domain} has no actions")
            continue
        directions: set[tuple[str, str]] = set()
        for index, action in enumerate(actions):
            label = f"scenario evidence domain {domain} action {index}"
            if not isinstance(action, dict):
                failures.append(label + " must be an object")
                continue
            token = _scenario_string(action.get("correlationToken"))
            if not _scenario_token(token):
                failures.append(label + " has an invalid correlationToken")
            elif token == scenario_nonce:
                failures.append(label + " reuses scenarioNonce as correlationToken")
            elif token in used_tokens:
                failures.append(label + " replays a correlationToken")
            else:
                used_tokens.add(token)
            initiator = _scenario_string(action.get("initiator"))
            receiver = _scenario_string(action.get("receiver"))
            if initiator not in {"primary", "peer"} or receiver not in {"primary", "peer"} or initiator == receiver:
                failures.append(label + " must name distinct primary/peer initiator and receiver roles")
            else:
                directions.add((initiator, receiver))

            initiated = _parse_utc(action.get("initiatedUtc"), label + ".initiatedUtc", failures)
            received = _parse_utc(action.get("receivedUtc"), label + ".receivedUtc", failures)
            receiver_observation = action.get("receiverObservation")
            observed: dt.datetime | None = None
            if not isinstance(receiver_observation, dict):
                failures.append(label + " receiverObservation must be an object")
            else:
                status = _scenario_string(receiver_observation.get("status"))
                if status not in {"applied", "completed", "rehydrated", "observed"}:
                    failures.append(label + " receiverObservation.status is not a successful result")
                post_state = receiver_observation.get("postState")
                if not isinstance(post_state, dict) or not post_state:
                    failures.append(label + " receiverObservation.postState must be a nonempty object")
                observed = _parse_utc(
                    receiver_observation.get("observedUtc"),
                    label + ".receiverObservation.observedUtc",
                    failures,
                )
            if initiated is not None and received is not None and observed is not None:
                if received < initiated or observed < received:
                    failures.append(label + " timestamps are out of action order")
                if observed - initiated > dt.timedelta(seconds=SCENARIO_MAX_ACTION_SECONDS):
                    failures.append(label + " exceeds the maximum action correlation interval")
                if start is not None and end is not None and (initiated < start or observed > end):
                    failures.append(label + " timestamps fall outside the scenario window")

            target = action.get("target")
            if domain in SCENARIO_TARGET_REQUIRED_DOMAINS:
                if (
                    not isinstance(target, dict)
                    or not _scenario_string(target.get("kind"))
                    or not _nonzero_identifier(target.get("id"))
                ):
                    failures.append(label + " requires a target identity")
            elif target is not None and (
                not isinstance(target, dict)
                or not _scenario_string(target.get("kind"))
                or not _nonzero_identifier(target.get("id"))
            ):
                failures.append(label + " target must be null or a kind/id identity object")

            proof = action.get("proof")
            proof_kind = ""
            if not isinstance(proof, dict):
                failures.append(label + " proof must be an object")
            else:
                proof_kind = _scenario_string(proof.get("kind"))
                if proof_kind == "operator_supplied":
                    if not _nonzero_identifier(proof.get("actionIdentifier")):
                        failures.append(label + " operator_supplied proof lacks actionIdentifier")
                    if not _scenario_token(proof.get("payloadDigestSha256")):
                        failures.append(label + " operator_supplied proof lacks payloadDigestSha256")
                elif proof_kind == "engine_correlated":
                    failures.append(
                        label
                        + " proof.kind=engine_correlated is invalid: supplied JSON cannot claim native engine correlation"
                    )
                elif proof_kind == "manual_unproven":
                    if not _scenario_string(proof.get("reason")):
                        failures.append(label + " manual_unproven proof lacks reason")
                    if strict_gameplay_certification:
                        failures.append(label + " is manual/unproven and cannot certify gameplay")
                else:
                    failures.append(label + " proof.kind must be operator_supplied or manual_unproven")

            manual = action.get("manualHumanObservation")
            if domain in SCENARIO_MANUAL_OBSERVATION_DOMAINS or manual is not None:
                if not isinstance(manual, dict):
                    failures.append(label + " requires manualHumanObservation")
                else:
                    if manual.get("kind") != "manual_human_observation":
                        failures.append(label + " manualHumanObservation must be labeled manual_human_observation")
                    if not _scenario_string(manual.get("observer")) or not _scenario_string(manual.get("statement")):
                        failures.append(label + " manualHumanObservation lacks observer or statement")
                    manual_time = _parse_utc(
                        manual.get("observedUtc"),
                        label + ".manualHumanObservation.observedUtc",
                        failures,
                    )
                    if manual_time is not None and start is not None and end is not None and not start <= manual_time <= end:
                        failures.append(label + " manualHumanObservation falls outside the scenario window")
        for direction in (("primary", "peer"), ("peer", "primary")):
            if direction not in directions:
                failures.append(
                    f"scenario evidence domain {domain} lacks {direction[0]}-to-{direction[1]} correlation"
                )
    return failures


def require_paired_scenario_evidence(
    primary_zf: zipfile.ZipFile,
    primary_names: set[str],
    peer_zf: zipfile.ZipFile,
    peer_names: set[str],
    *,
    primary_snapshot: dict[str, str],
    peer_snapshot: dict[str, str],
    primary_status: dict[str, str],
    peer_status: dict[str, str],
    primary_manifest: dict[str, object],
    expected_domains: tuple[str, ...],
    failures: list[str],
) -> None:
    """Check shared operator input, but fail strict certification without native traces."""
    failures.append(NATIVE_TRACE_CORROBORATION_MISSING)
    if SCENARIO_EVIDENCE_ARCHIVE_ENTRY not in primary_names:
        failures.append("primary evidence lacks explicit shared scenario evidence")
        return
    if SCENARIO_EVIDENCE_ARCHIVE_ENTRY not in peer_names:
        failures.append("peer evidence lacks explicit shared scenario evidence")
        return
    primary_payload = primary_zf.read(SCENARIO_EVIDENCE_ARCHIVE_ENTRY)
    peer_payload = peer_zf.read(SCENARIO_EVIDENCE_ARCHIVE_ENTRY)
    if primary_payload != peer_payload:
        failures.append("paired scenario evidence is not byte-identical across client archives")
        return
    try:
        evidence = json.loads(primary_payload.decode("utf-8"))
    except UnicodeDecodeError as exc:
        failures.append(f"scenario evidence is not UTF-8: {exc}")
        return
    except json.JSONDecodeError as exc:
        failures.append(f"scenario evidence is not valid JSON: {exc}")
        return
    failures.extend(
        validate_scenario_evidence(
            evidence,
            expected_domains=expected_domains,
            primary_snapshot=primary_snapshot,
            peer_snapshot=peer_snapshot,
            primary_status=primary_status,
            peer_status=peer_status,
            collection_created_utc=primary_manifest.get("createdUtc"),
            strict_gameplay_certification=True,
        )
    )


def load_json(zf: zipfile.ZipFile, name: str, failures: list[str]) -> dict[str, object]:
    try:
        return json.loads(zf.read(name).decode("utf-8"))
    except KeyError:
        failures.append(f"missing archive entry: {name}")
    except json.JSONDecodeError as exc:
        failures.append(f"{name} is not valid JSON: {exc}")
    except UnicodeDecodeError as exc:
        failures.append(f"{name} is not UTF-8: {exc}")
    return {}


def load_gameplay_snapshot(zf: zipfile.ZipFile, failures: list[str]) -> dict[str, str]:
    name = "handoff/SkyrimTogetherVR.gameplay"
    try:
        return collect_runtime_evidence.vr_handoff.parse_key_value_bytes(zf.read(name))
    except KeyError:
        failures.append(f"missing archive entry: {name}")
    return {}


def _nonzero_int(value: str | None) -> bool:
    try:
        return int(value or "", 0) > 0
    except ValueError:
        return False


def _nonzero_identifier(value: str | None) -> bool:
    """Return whether a textual identity is present and not an all-zero token."""
    normalized = (value or "").strip()
    return bool(normalized) and any(character != "0" for character in normalized)


def _nonnegative_counter(
    values: dict[str, str],
    key: str,
) -> tuple[bool, int]:
    """Parse one explicit bridge counter without silently accepting malformed data."""
    raw = values.get(key)
    if raw is None:
        return False, 0
    try:
        value = int(raw.strip(), 0)
    except ValueError:
        return False, 0
    return value >= 0, value


def audit_gameplay_loss_counters(
    values: dict[str, str],
    failures: list[str],
    warnings: list[str],
    *,
    label: str,
    strict: bool,
) -> None:
    """Validate bridge losses, exempting only explicitly classified lifecycle work.

    A ring drop or a rejected command always represents lost or invalid gameplay
    work.  The bridge can explicitly classify discarded events and rejected
    submissions that occurred before readiness or while an epoch was retiring;
    those exclusions must not exceed the reported total.  An unclassified
    remainder is harmful evidence in a strict gameplay proof and a visible
    warning in a non-strict collection.
    """
    for key, counter_label, exempt_reasons in BRIDGE_LOSS_COUNTERS:
        valid, total = _nonnegative_counter(values, key)
        if not valid:
            message = f"{label} gameplay snapshot {key} is missing or invalid"
            (failures if strict else warnings).append(message)
            continue

        exempt_total = 0
        for reason in exempt_reasons:
            exemption_key = f"{key}.{reason}"
            if exemption_key not in values:
                continue
            exemption_valid, exemption = _nonnegative_counter(values, exemption_key)
            if not exemption_valid:
                failures.append(f"{label} gameplay snapshot {exemption_key} is missing or invalid")
                continue
            exempt_total += exemption
        if exempt_total > total:
            failures.append(
                f"{label} gameplay snapshot {key} lifecycle exclusions exceed the total "
                f"({exempt_total}>{total})"
            )
            continue

        harmful_total = total - exempt_total
        if not harmful_total:
            continue
        message = (
            f"{label} gameplay snapshot reports harmful {counter_label}: "
            f"{harmful_total} unclassified of {total}"
        )
        (failures if strict else warnings).append(message)


def _require_snapshot_status_identity(
    values: dict[str, str],
    status: dict[str, str],
    snapshot_field: str,
    status_field: str,
    label: str,
    failures: list[str],
) -> None:
    snapshot_value = values.get(snapshot_field, "").strip()
    status_value = status.get(status_field, "").strip()
    if snapshot_value != status_value:
        failures.append(f"{label} gameplay {snapshot_field} does not match status {status_field}")
    if snapshot_field == "launchNonce":
        if not _nonzero_identifier(snapshot_value):
            failures.append(f"{label} gameplay {snapshot_field} is missing or zero")
    elif not _nonzero_int(snapshot_value):
        failures.append(f"{label} gameplay {snapshot_field} is missing or zero")


def require_paired_session_evidence(
    primary: dict[str, str],
    peer: dict[str, str],
    primary_status: dict[str, str],
    peer_status: dict[str, str],
    failures: list[str],
) -> None:
    """Require independent client and matching-session evidence before counters.

    The gameplay snapshots state that their counters are local-process
    observations.  A paired claim therefore needs a second admitted snapshot,
    distinct client identity, and each snapshot's matching transport status.
    Paired snapshots sharing a server nonce must have different connection
    generations because the server allocates them from one global sequence.
    """
    handoff = collect_runtime_evidence.vr_handoff
    for label, values, status in (
        ("primary", primary, primary_status),
        ("peer", peer, peer_status),
    ):
        if not handoff.gameplay_snapshot_ready(values):
            failures.append(f"{label} aggregate gameplay snapshot is not ready")
        for field, expected in handoff.GAMEPLAY_LOCAL_COUNTER_EVIDENCE.items():
            if values.get(field) != expected:
                failures.append(f"{label} gameplay snapshot lacks {field}={expected}")
        for snapshot_field, status_field in (
            ("launchNonce", "launchNonce"),
            ("processId", "processId"),
            ("session.id", "sessionId"),
            ("session.serverInstanceNonce", "serverInstanceNonce"),
            ("session.connectionGeneration", "connectionGeneration"),
        ):
            _require_snapshot_status_identity(
                values,
                status,
                snapshot_field,
                status_field,
                label,
                failures,
            )
        if status.get("online", "").strip().lower() not in {"1", "true", "yes"}:
            failures.append(f"{label} paired status is not online")
        if not _nonzero_int(status.get("playerId")):
            failures.append(f"{label} paired status lacks nonzero playerId")

    server_nonce = primary.get("session.serverInstanceNonce", "").strip()
    if not _nonzero_int(server_nonce):
        failures.append("primary paired evidence lacks nonzero session.serverInstanceNonce")
    elif server_nonce != peer.get("session.serverInstanceNonce", "").strip():
        failures.append("paired evidence session.serverInstanceNonce differs")

    for field in ("session.id", "launchNonce", "processId", "session.connectionGeneration"):
        if primary.get(field) == peer.get(field):
            failures.append(f"paired evidence must originate from distinct clients ({field})")

    if primary_status.get("playerId") == peer_status.get("playerId"):
        failures.append("paired evidence must originate from distinct players")


def paired_domain_requirements(
    *,
    require_gameplay: bool,
    require_movement_relay: bool,
    require_equipment_relay: bool,
    require_activation_relay: bool,
    require_magic_relay: bool,
    require_combat_relay: bool,
    require_projectile_relay: bool,
    require_grab_relay: bool,
    require_higgs_relay: bool,
    require_saveload_observer: bool,
) -> tuple[tuple[str, ...], tuple[str, ...]]:
    """Return canonical and optional extension domains required by this audit."""
    handoff = collect_runtime_evidence.vr_handoff
    canonical: list[str] = list(handoff.GAMEPLAY_MANDATORY_CANONICAL_DOMAINS) if require_gameplay else []
    for requested, domain in (
        (require_movement_relay, "movement"),
        (require_equipment_relay, "equipment"),
        (require_activation_relay, "object"),
        (require_magic_relay, "magic"),
        (require_combat_relay, "combat"),
        (require_projectile_relay, "projectile"),
        (require_saveload_observer, "save_load"),
    ):
        if requested:
            canonical.append(domain)
    optional: list[str] = []
    if require_grab_relay or require_higgs_relay:
        optional.append("higgs")
    return tuple(dict.fromkeys(canonical)), tuple(dict.fromkeys(optional))


def require_paired_peer_archive(
    peer_zf: zipfile.ZipFile,
    peer_names: set[str],
    primary_package_manifest: dict[str, object],
    failures: list[str],
    *,
    require_gameplay: bool,
) -> tuple[dict[str, str], dict[str, str]]:
    """Validate the peer archive's live identity and exact package provenance."""
    peer_failures: list[str] = []
    peer_manifest = load_json(peer_zf, "manifest.json", peer_failures)
    peer_package_manifest = load_json(
        peer_zf,
        "package/SkyrimTogetherVR_BuildManifest.json",
        peer_failures,
    )
    if peer_manifest.get("schema") != "skyrim_together_vr_runtime_evidence_v1":
        peer_failures.append("manifest schema is invalid")
    if require_gameplay and not peer_manifest.get("gameplayAudit"):
        peer_failures.append("archive was not collected with --gameplay")
    if not peer_manifest.get("liveAdmissionRequested") or peer_manifest.get("runtimeEvidenceTrust") != "trusted":
        peer_failures.append("archive is not trusted live gameplay evidence")
    for field in ("networkVersion", "sourceRevision", "sourceProvenance"):
        if peer_package_manifest.get(field) != primary_package_manifest.get(field):
            peer_failures.append(f"package {field} does not match primary evidence")
    runtime_identity = peer_manifest.get("runtimeIdentity")
    expected_network_version = peer_package_manifest.get("networkVersion")
    if not isinstance(runtime_identity, dict) or not isinstance(expected_network_version, str):
        peer_failures.append("runtime identity or package networkVersion is unavailable")
    else:
        audit_archived_runtime_identity(
            peer_zf,
            peer_names,
            peer_manifest,
            runtime_identity,
            expected_network_version,
            peer_failures,
        )
    peer_status_name = f"handoff/{collect_runtime_evidence.vr_handoff.READOUT_FILES['status']}"
    peer_status = (
        collect_runtime_evidence.vr_handoff.parse_key_value_bytes(peer_zf.read(peer_status_name))
        if peer_status_name in peer_names
        else {}
    )
    if not peer_status:
        peer_failures.append("status readout is missing")
    for failure in peer_failures:
        failures.append("peer evidence " + failure)
    return load_gameplay_snapshot(peer_zf, failures), peer_status


def require_check_pass(
    checks_by_id: dict[str, dict[str, object]],
    check_id: str,
    label: str,
    failures: list[str],
) -> None:
    check = checks_by_id.get(check_id, {})
    if check.get("status") != collect_runtime_evidence.CHECK_PASS:
        failures.append(f"{label} checklist did not pass: {check.get('detail', '<missing detail>')}")


def require_manifest_requested_checks(
    manifest: dict[str, object],
    checks_by_id: dict[str, dict[str, object]],
    failures: list[str],
) -> None:
    for manifest_flag, check_id, label in MANIFEST_FLAG_REQUIRED_CHECKS:
        if bool(manifest.get(manifest_flag)):
            require_check_pass(checks_by_id, check_id, f"manifest-required {label}", failures)
    if bool(manifest.get("avatarSyncAudit")):
        require_check_pass(
            checks_by_id,
            "connection_status",
            "manifest avatar-sync connection status",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "local_vrik_api",
            "manifest avatar-sync local VRIK API",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "higgs_bridge",
            "manifest avatar-sync HIGGS bridge",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "remote_player_proxy",
            "manifest avatar-sync remote-player proxy",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "remote_avatar_ready",
            "manifest avatar-sync remote VRIK avatar readiness",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "remote_higgs_avatar_ready",
            "manifest avatar-sync remote VRIK/HIGGS avatar readiness",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "avatar_sync_actor_targets",
            "manifest avatar-sync actor target application",
            failures,
        )
    if bool(manifest.get("gameplayBootstrapAudit")):
        for check_id, label in GAMEPLAY_BOOTSTRAP_CHECKS:
            require_check_pass(checks_by_id, check_id, f"manifest gameplay-bootstrap {label}", failures)


def audit_archived_runtime_identity(
    zf: zipfile.ZipFile,
    names: set[str],
    manifest: dict[str, object],
    runtime_identity: dict[str, object],
    expected_network_version: str | None,
    failures: list[str],
) -> None:
    """Revalidate archived identity fields and collection-time freshness metadata."""
    handoff_names = {
        name: f"handoff/{collect_runtime_evidence.vr_handoff.READOUT_FILES[name]}"
        for name in collect_runtime_evidence.vr_handoff.RUNTIME_IDENTITY_READOUTS
    }
    missing = [archive_name for archive_name in handoff_names.values() if archive_name not in names]
    if missing:
        failures.append("identity readout missing from archive")
        return
    game_path_value = manifest.get("gamePath")
    if not isinstance(game_path_value, str) or not game_path_value:
        failures.append("manifest gamePath is unavailable for archived identity validation")
        return

    readout_metadata = runtime_identity.get("readouts")
    evaluated_at_ns = runtime_identity.get("evaluatedAtNs")
    max_age_seconds = runtime_identity.get("maxReadoutAgeSeconds")
    marker_mtime_ns = runtime_identity.get("runStartMarkerMtimeNs")
    if not isinstance(readout_metadata, dict) or not isinstance(evaluated_at_ns, int) or not isinstance(max_age_seconds, (int, float)):
        failures.append("runtime identity freshness metadata is incomplete")
        return
    archived_readouts = {
        name: collect_runtime_evidence.vr_handoff.parse_key_value_bytes(zf.read(archive_name))
        for name, archive_name in handoff_names.items()
    }
    evaluated = collect_runtime_evidence.vr_handoff.evaluate_runtime_identity(
        archived_readouts,
        pathlib.Path("."),
        pathlib.Path(game_path_value),
        max_age_seconds=float(max_age_seconds),
        now_ns=evaluated_at_ns,
        readout_metadata=readout_metadata,
        expected_network_version=expected_network_version,
        require_gameplay_ready=bool(
            manifest.get("gameplayAudit") or manifest.get("gameplayBootstrapAudit")
        ),
    )
    if not evaluated["ok"]:
        failures.append("archived runtime identity fields are invalid: " + "; ".join(map(str, evaluated["reasons"])))

    manifest_files = manifest.get("files")
    if not isinstance(manifest_files, list):
        failures.append("manifest files field is not a list for identity validation")
        return
    files_by_name = {
        str(record.get("archiveName")): record
        for record in manifest_files
        if isinstance(record, dict)
    }
    oldest_allowed_ns = evaluated_at_ns - int(float(max_age_seconds) * 1_000_000_000)
    for name, archive_name in handoff_names.items():
        metadata = readout_metadata.get(name)
        record = files_by_name.get(archive_name)
        if not isinstance(metadata, dict) or not isinstance(record, dict):
            failures.append(f"runtime identity metadata is missing for {name}")
            continue
        mtime_ns = metadata.get("mtimeNs")
        if not isinstance(mtime_ns, int) or record.get("mtimeNs") != mtime_ns:
            failures.append(f"runtime identity mtime does not match collected {name} readout")
            continue
        if mtime_ns < oldest_allowed_ns or mtime_ns > evaluated_at_ns + 1_000_000_000:
            failures.append(f"collected {name} identity readout was not fresh")
        if isinstance(marker_mtime_ns, int) and mtime_ns < marker_mtime_ns:
            failures.append(f"collected {name} identity readout predates run-start marker")


def audit_archive(
    path: pathlib.Path,
    *,
    require_avatar_sync: bool,
    require_gameplay: bool,
    require_remote_player: bool,
    require_weapon_pose: bool,
    require_magic_pose: bool,
    require_projectile_pose: bool,
    require_movement_relay: bool,
    require_equipment_relay: bool,
    require_activation_relay: bool,
    require_magic_relay: bool,
    require_combat_relay: bool,
    require_projectile_relay: bool,
    require_grab_relay: bool,
    require_higgs_relay: bool,
    require_saveload_observer: bool,
    allow_failed_checks: bool,
    require_gameplay_bootstrap: bool = False,
    peer_archive: pathlib.Path | None = None,
) -> int:
    failures: list[str] = []
    warnings: list[str] = []
    if not path.exists():
        print(f"Evidence archive does not exist: {path}")
        return 1
    if not zipfile.is_zipfile(path):
        print(f"Evidence archive is not a zip file: {path}")
        return 1

    with zipfile.ZipFile(path) as zf:
        names = set(zf.namelist())
        for entry in REQUIRED_ARCHIVE_ENTRIES:
            if entry == "logs/tp_client.log":
                continue
            if entry not in names:
                failures.append(f"missing archive entry: {entry}")

        manifest = load_json(zf, "manifest.json", failures) if "manifest.json" in names else {}
        gameplay_snapshot = load_gameplay_snapshot(zf, failures)
        gameplay_snapshot_ok, gameplay_snapshot_detail = collect_runtime_evidence.vr_handoff.gameplay_snapshot_detail(
            gameplay_snapshot
        )
        if not gameplay_snapshot_ok:
            failures.append("invalid aggregate gameplay snapshot: " + gameplay_snapshot_detail)
        package_manifest = (
            load_json(zf, "package/SkyrimTogetherVR_BuildManifest.json", failures)
            if "package/SkyrimTogetherVR_BuildManifest.json" in names
            else {}
        )
        checklist = load_json(zf, "runtime_checklist.json", failures) if "runtime_checklist.json" in names else {}

        manifest_schema = manifest.get("schema")
        if manifest_schema != "skyrim_together_vr_runtime_evidence_v1":
            failures.append(f"unexpected manifest schema: {manifest_schema!r}")

        checklist_schema = checklist.get("schema")
        if checklist_schema != "skyrim_together_vr_runtime_checklist_v1":
            failures.append(f"unexpected runtime checklist schema: {checklist_schema!r}")

        missing_required = manifest.get("missingRequired", [])
        if missing_required:
            failures.append("collector reported missing required file(s): " + ", ".join(map(str, missing_required)))

        runtime_audit_exit = manifest.get("runtimeAuditExitCode")
        if runtime_audit_exit not in (0, None):
            failures.append(f"embedded runtime audit failed with exit code {runtime_audit_exit}")

        manifest_avatar_sync = bool(manifest.get("avatarSyncAudit"))
        manifest_gameplay = bool(manifest.get("gameplayAudit"))
        manifest_gameplay_bootstrap = bool(manifest.get("gameplayBootstrapAudit"))
        effective_require_gameplay = require_gameplay or manifest_gameplay
        effective_require_movement_relay = require_movement_relay or bool(manifest.get("requiredMovementRelay"))
        effective_require_equipment_relay = require_equipment_relay or bool(manifest.get("requiredEquipmentRelay"))
        effective_require_activation_relay = require_activation_relay or bool(manifest.get("requiredActivationRelay"))
        effective_require_magic_relay = require_magic_relay or bool(manifest.get("requiredMagicRelay"))
        effective_require_combat_relay = require_combat_relay or bool(manifest.get("requiredCombatRelay"))
        effective_require_projectile_relay = require_projectile_relay or bool(manifest.get("requiredProjectileRelay"))
        effective_require_grab_relay = require_grab_relay or bool(manifest.get("requiredGrabRelay"))
        effective_require_higgs_relay = require_higgs_relay or bool(manifest.get("requiredHiggsRelay"))
        effective_require_saveload_observer = require_saveload_observer or bool(manifest.get("requiredSaveloadObserver"))
        if not manifest_gameplay_bootstrap and "logs/tp_client.log" not in names:
            failures.append("missing archive entry: logs/tp_client.log")
        if require_avatar_sync and not require_gameplay and (not manifest_avatar_sync or manifest_gameplay):
            failures.append("archive was not collected with --avatar-sync")
        elif require_avatar_sync and not manifest_avatar_sync:
            failures.append("archive was not collected with --avatar-sync")
        if require_gameplay and not manifest_gameplay:
            failures.append("archive was not collected with --gameplay")
        if require_gameplay_bootstrap and not manifest_gameplay_bootstrap:
            failures.append("archive was not collected with --gameplay-bootstrap")

        package_manifest_ok = False
        expected_network_version: str | None = None
        if package_manifest:
            package_manifest_ok, package_manifest_detail = collect_runtime_evidence.validate_build_manifest_data(
                package_manifest,
                avatar_sync=manifest_avatar_sync and not (manifest_gameplay or manifest_gameplay_bootstrap),
                gameplay=manifest_gameplay or manifest_gameplay_bootstrap,
            )
            if not package_manifest_ok:
                failures.append("package build manifest validation failed: " + package_manifest_detail)
            else:
                network_version = package_manifest.get("networkVersion")
                if isinstance(network_version, str):
                    expected_network_version = network_version
            embedded_package_manifest = manifest.get("packageBuildManifest")
            if isinstance(embedded_package_manifest, dict):
                embedded_ok, embedded_detail = collect_runtime_evidence.validate_build_manifest_data(
                    embedded_package_manifest,
                    avatar_sync=manifest_avatar_sync and not (manifest_gameplay or manifest_gameplay_bootstrap),
                    gameplay=manifest_gameplay or manifest_gameplay_bootstrap,
                )
                if not embedded_ok:
                    failures.append("embedded package build manifest validation failed: " + embedded_detail)
                archived_manifest_canonical = json.dumps(
                    package_manifest,
                    ensure_ascii=False,
                    separators=(",", ":"),
                    sort_keys=True,
                )
                embedded_manifest_canonical = json.dumps(
                    embedded_package_manifest,
                    ensure_ascii=False,
                    separators=(",", ":"),
                    sort_keys=True,
                )
                if embedded_manifest_canonical != archived_manifest_canonical:
                    failures.append(
                        "archived package build manifest does not exactly match manifest.json packageBuildManifest"
                    )
            else:
                failures.append("manifest.json packageBuildManifest field is not an object")

        runtime_identity = manifest.get("runtimeIdentity")
        runtime_trust = manifest.get("runtimeEvidenceTrust")
        live_admission_requested = bool(manifest.get("liveAdmissionRequested"))
        if not isinstance(runtime_identity, dict):
            failures.append("manifest runtimeIdentity field is not an object")
        elif runtime_identity.get("schema") != "skyrim_together_vr_runtime_identity_v1":
            failures.append("unexpected runtime identity schema")
        elif live_admission_requested:
            if expected_network_version is None:
                failures.append("validated package networkVersion is unavailable for archived identity validation")
            audit_archived_runtime_identity(
                zf,
                names,
                manifest,
                runtime_identity,
                expected_network_version,
                failures,
            )
        if runtime_trust not in {"trusted", "untrusted"}:
            failures.append("manifest runtimeEvidenceTrust is missing or invalid")
        if live_admission_requested and runtime_trust != "trusted":
            failures.append("live admission archive is not trusted")
        elif not live_admission_requested and runtime_trust != "untrusted":
            failures.append("generic or crash evidence archive must be untrusted for live admission")
        elif runtime_trust == "untrusted":
            warnings.append("archive is untrusted for live admission; retained for generic/crash diagnostics only")

        checks = checklist.get("checks", [])
        if not isinstance(checks, list):
            failures.append("runtime_checklist.json has no checks list")
            checks = []

        checks_by_id: dict[str, dict[str, object]] = {}
        for check in checks:
            if not isinstance(check, dict):
                warnings.append(f"ignored non-object checklist row: {check!r}")
                continue
            check_id = str(check.get("id", ""))
            if check_id:
                checks_by_id[check_id] = check

        for check_id in REQUIRED_CHECK_IDS:
            if check_id not in checks_by_id:
                failures.append(f"runtime checklist missing check id: {check_id}")

        failed_checks = [
            check
            for check in checks_by_id.values()
            if check.get("status") == collect_runtime_evidence.CHECK_FAIL
            and (
                not manifest_gameplay_bootstrap
                or check.get("id") in collect_runtime_evidence.GAMEPLAY_BOOTSTRAP_REQUIRED_CHECK_IDS
            )
        ]
        if failed_checks and not allow_failed_checks:
            for check in failed_checks:
                failures.append(
                    "runtime checklist failed {id}: {detail}".format(
                        id=check.get("id", "<unknown>"),
                        detail=check.get("detail", ""),
                    )
                )

        manifest_checklist_summary = manifest.get("runtimeChecklist")
        checklist_summary = checklist.get("summary")
        if isinstance(manifest_checklist_summary, dict) and isinstance(checklist_summary, dict):
            if manifest_checklist_summary != checklist_summary:
                failures.append("manifest runtimeChecklist summary does not match runtime_checklist.json summary")
        else:
            failures.append("manifest/runtime checklist summary is not present in both JSON records")

        require_manifest_requested_checks(manifest, checks_by_id, failures)
        if live_admission_requested:
            require_check_pass(checks_by_id, "runtime_identity", "manifest live runtime identity", failures)
        if manifest_gameplay or manifest_gameplay_bootstrap:
            require_check_pass(checks_by_id, "gameplay_readiness", "aggregate gameplay readiness", failures)

        avatar_check = checks_by_id.get("avatar_sync_actor_targets", {})
        if require_avatar_sync and avatar_check.get("status") != collect_runtime_evidence.CHECK_PASS:
            failures.append(
                "avatar-sync actor target checklist did not pass: "
                + str(avatar_check.get("detail", "<missing detail>"))
            )
        if require_remote_player:
            require_check_pass(checks_by_id, "remote_player_proxy", "remote-player proxy", failures)
            require_check_pass(checks_by_id, "remote_avatar_ready", "remote VRIK avatar readiness", failures)
            if require_avatar_sync:
                require_check_pass(
                    checks_by_id,
                    "remote_higgs_avatar_ready",
                    "remote VRIK/HIGGS avatar readiness",
                    failures,
                )
        if require_weapon_pose:
            require_check_pass(checks_by_id, "weapon_pose_context", "weapon pose context", failures)
        if require_magic_pose:
            require_check_pass(checks_by_id, "magic_pose_context", "magic pose context", failures)
        if require_projectile_pose:
            require_check_pass(checks_by_id, "projectile_pose_context", "projectile pose context", failures)
        # Legacy per-service readouts remain useful diagnostics, but their
        # absence cannot label a canonical gameplay domain disabled. Requested
        # relay certification is checked below from explicit action scenarios,
        # never from aggregate captured/sent/applied counters.

        canonical_domains, optional_extension_domains = paired_domain_requirements(
            require_gameplay=effective_require_gameplay,
            require_movement_relay=effective_require_movement_relay,
            require_equipment_relay=effective_require_equipment_relay,
            require_activation_relay=effective_require_activation_relay,
            require_magic_relay=effective_require_magic_relay,
            require_combat_relay=effective_require_combat_relay,
            require_projectile_relay=effective_require_projectile_relay,
            require_grab_relay=effective_require_grab_relay,
            require_higgs_relay=effective_require_higgs_relay,
            require_saveload_observer=effective_require_saveload_observer,
        )
        strict_gameplay_loss_evidence = bool(canonical_domains or optional_extension_domains)
        audit_gameplay_loss_counters(
            gameplay_snapshot,
            failures,
            warnings,
            label="primary",
            strict=strict_gameplay_loss_evidence,
        )
        if canonical_domains or optional_extension_domains:
            if not live_admission_requested or runtime_trust != "trusted":
                failures.append("paired gameplay proof requires trusted live primary evidence")
            if peer_archive is None:
                failures.append("two-client relay proof requires --peer-archive")
            elif not peer_archive.exists() or not zipfile.is_zipfile(peer_archive):
                failures.append("peer evidence archive does not exist or is not a zip file")
            else:
                with zipfile.ZipFile(peer_archive) as peer_zf:
                    peer_names = set(peer_zf.namelist())
                    peer_snapshot, peer_status = require_paired_peer_archive(
                        peer_zf,
                        peer_names,
                        package_manifest,
                        failures,
                        require_gameplay=effective_require_gameplay,
                    )
                    primary_status_name = f"handoff/{collect_runtime_evidence.vr_handoff.READOUT_FILES['status']}"
                    primary_status = (
                        collect_runtime_evidence.vr_handoff.parse_key_value_bytes(zf.read(primary_status_name))
                        if primary_status_name in names
                        else {}
                    )
                    require_paired_session_evidence(
                        gameplay_snapshot,
                        peer_snapshot,
                        primary_status,
                        peer_status,
                        failures,
                    )
                    audit_gameplay_loss_counters(
                        peer_snapshot,
                        failures,
                        warnings,
                        label="peer",
                        strict=True,
                    )
                    require_paired_scenario_evidence(
                        zf,
                        names,
                        peer_zf,
                        peer_names,
                        primary_snapshot=gameplay_snapshot,
                        peer_snapshot=peer_snapshot,
                        primary_status=primary_status,
                        peer_status=peer_status,
                        primary_manifest=manifest,
                        expected_domains=canonical_domains + optional_extension_domains,
                        failures=failures,
                    )

        manifest_files = manifest.get("files", [])
        if isinstance(manifest_files, list):
            for file_record in manifest_files:
                if not isinstance(file_record, dict):
                    continue
                archive_name = str(file_record.get("archiveName", ""))
                if file_record.get("exists") and archive_name and archive_name not in names:
                    failures.append(f"manifest says collected file is missing from zip: {archive_name}")
                    continue
                if not file_record.get("exists") or not archive_name or archive_name not in names:
                    continue
                payload = zf.read(archive_name)
                expected_size = file_record.get("size")
                if expected_size != len(payload):
                    failures.append(
                        f"collected file size mismatch for {archive_name}: manifest={expected_size} zip={len(payload)}"
                    )
                expected_sha256 = str(file_record.get("sha256", ""))
                actual_sha256 = hashlib.sha256(payload).hexdigest()
                if expected_sha256 != actual_sha256:
                    failures.append(f"collected file SHA-256 mismatch for {archive_name}")
        else:
            failures.append("manifest files field is not a list")

        if bool(manifest.get("crashEvidence")):
            inventory_name = "system/runtime_inventory.json"
            if inventory_name not in names:
                failures.append(f"crash-evidence archive is missing {inventory_name}")
            else:
                inventory = load_json(zf, inventory_name, failures)
                if inventory.get("schema") != "skyrim_together_vr_runtime_inventory_v1":
                    failures.append(f"unexpected runtime inventory schema: {inventory.get('schema')!r}")
                if not isinstance(inventory.get("files"), list):
                    failures.append("runtime inventory has no files list")

        print(f"Evidence archive: {path}")
        print(f"Archive entries: {len(names)}")
        print(f"Manifest schema: {manifest_schema}")
        print(f"Runtime audit exit code: {runtime_audit_exit}")
        print(f"Avatar-sync audit: {manifest_avatar_sync}")
        print(f"Gameplay audit: {manifest_gameplay}")
        print(f"Gameplay bootstrap audit: {manifest_gameplay_bootstrap}")
        if manifest_gameplay_bootstrap:
            print("Gameplay bootstrap scope: one-client local bootstrap only; not two-client parity proof")
        if canonical_domains or optional_extension_domains:
            print("Paired scenario scope: operator-supplied action-record shape only; strict certification requires native trace corroboration")
        if package_manifest:
            print(f"Package build manifest schema: {package_manifest.get('schema')}")
            print(f"Package build manifest avatarSync: {package_manifest.get('avatarSync')}")
            print(f"Package build manifest gameplay: {package_manifest.get('gameplay')}")
        summary = checklist.get("summary", {})
        if isinstance(summary, dict):
            print(
                "Checklist summary: pass={pass_count} fail={fail_count} not_required={not_required_count}".format(
                    pass_count=summary.get(collect_runtime_evidence.CHECK_PASS, 0),
                    fail_count=summary.get(collect_runtime_evidence.CHECK_FAIL, 0),
                    not_required_count=summary.get(collect_runtime_evidence.CHECK_NOT_REQUIRED, 0),
                )
            )
        print(f"Checklist failed checks: {len(failed_checks)}")
        for check in failed_checks:
            print(f"- {check.get('id', '<unknown>')}: {check.get('detail', '')}")
        print(f"Warnings: {len(warnings)}")
        for warning in warnings:
            print(f"- {warning}")
        print(f"Failures: {len(failures)}")
        for failure in failures:
            print(f"- {failure}")

    return 1 if failures else 0


def command_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="stvr-evidence-zip-audit-") as temp:
        root = pathlib.Path(temp)
        game = root / "SkyrimVR"
        handoff = game / "Data" / "SkyrimTogetherReborn"
        log = game / collect_runtime_evidence.DEFAULT_LOG_RELATIVE
        gameplay_bridge_log = root / "gameplay-bridge" / collect_runtime_evidence.GAMEPLAY_BRIDGE_LOG_NAME
        out_dir = root / "out"
        handoff.mkdir(parents=True)
        log.parent.mkdir(parents=True)
        log.write_text(
            "\n".join(collect_runtime_evidence.audit_runtime_handoff.LOG_BREADCRUMBS)
            + "\nSkyrimTogetherVR VR update-owner runtime mode: active"
            + "\nSkyrimTogetherVR Main::Draw client update completed: count=1 sequence=1 thread=42\n",
            encoding="utf-8",
        )
        gameplay_bridge_log.parent.mkdir(parents=True)
        gameplay_bridge_log.write_text("[info] validated loader runtime=skyrimvr\n", encoding="utf-8")

        def write(name: str, contents: str) -> None:
            (handoff / collect_runtime_evidence.vr_handoff.READOUT_FILES[name]).write_text(contents, encoding="utf-8")

        write(
            "status",
            "state=online\nonline=1\nplayerId=4\nsessionId=123\nconnectionGeneration=1\n"
            "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\n"
            "clientVersion=fixture\nserverVersion=fixture\ngameplayProtocolRevision=17\n"
            "serverInstanceNonce=99\ngamePath={}\n".format(game),
        )
        write(
            "lifecycle",
            "state=ready\nready=1\nepoch=3\nownerThreadId=42\nstableTickCount=5\n"
            "playerFormId=20\nplayerBaseFormId=7\nplayerCellFormId=100\n"
            "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\ngamePath={}\n".format(game),
        )
        write("gameplay", collect_runtime_evidence.vr_handoff.gameplay_snapshot_fixture(game))
        write(
            "pose",
            "online=1\nlocalPoseAvailable=1\nlocal.hmd.valid=1\nlocal.leftHand.valid=1\nlocal.rightHand.valid=1\n"
            "local.spellOrigin.valid=1\nlocal.spellDestination.valid=1\n"
            "local.arrowOrigin.valid=1\nlocal.arrowDestination.valid=1\nlocal.bowAim.valid=1\nlocal.bowRotation.valid=1\n"
            "local.leftWeaponOffset.valid=1\nlocal.rightWeaponOffset.valid=1\n"
            "local.primaryMagicOffset.valid=1\nlocal.primaryMagicAim.valid=1\n"
            "local.secondaryMagicOffset.valid=1\nlocal.secondaryMagicAim.valid=1\n"
            "local.vrik.detected=1\nlocal.vrik.interfaceAvailable=1\nremotePoseCount=1\n"
            "remote.7.vrik.detected=1\nremote.7.vrik.interfaceAvailable=1\n",
        )
        write("movement", "ready=1\nlocalMovementAvailable=1\nremoteMovementCount=1\nlocalMovement.sequence=1\nremoteMovement.7.sequence=2\n")
        write(
            "inventory",
            "ready=1\n"
            "policy=equipment_snapshot_only\n"
            "fullInventoryTraversal=0\n"
            "inventoryMutation=0\n"
            "remoteEquipmentMutation=0\n"
            "normalInventoryPackets=0\n"
            "localEquipmentAvailable=1\n"
            "remoteEquipmentCount=1\n"
            "localEquipment.sequence=1\n"
            "remoteEquipment.7.sequence=2\n",
        )
        write(
            "discovery",
            "ready=1\n"
            "actorCount=2\n"
            "actorLimit=32\n"
            "currentGrid=4,-3\n"
            "centerGrid=4,-2\n"
            "cachedWorldSpaceFormId=60\n"
            "cachedInteriorCellFormId=0\n"
            "playerCellFormId=100\n"
            "playerWorldSpaceFormId=60\n"
            "locationFormId=200\n"
            "actor.0.formId=300\n"
            "actor.1.formId=301\n",
        )
        write(
            "playercell",
            "ready=1\n"
            "online=1\n"
            "localPlayerId=4\n"
            "sessionId=123\n"
            "connectionGeneration=1\n"
            "playerFormId=20\n"
            "currentLevel=12\n"
            "cachedLevel=12\n"
            "lastLevelSent=12\n"
            "gridCellRequestCount=1\n"
            "exteriorCellRequestCount=1\n"
            "interiorCellRequestCount=0\n"
            "levelRequestCount=1\n"
            "offlineSkippedRequestCount=0\n"
            "worldSpaceTranslationFailureCount=0\n"
            "lastGrid.valid=1\n"
            "lastGrid.worldSpace.serverModId=1\n"
            "lastGrid.worldSpace.serverBaseId=60\n"
            "lastGrid.playerCell.serverModId=1\n"
            "lastGrid.playerCell.serverBaseId=100\n"
            "lastGrid.center=4,-3\n"
            "lastGrid.cellCount=25\n"
            "lastGrid.connectionGeneration=1\n"
            "lastCell.valid=1\n"
            "lastCell.exterior=1\n"
            "lastCell.connectionGeneration=1\n"
            "lastCell.cell.serverModId=1\n"
            "lastCell.cell.serverBaseId=100\n"
            "lastCell.worldSpace.serverModId=1\n"
            "lastCell.worldSpace.serverBaseId=60\n"
            "lastCell.currentCoords=4,-3\n"
            "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\ngamePath={}\n".format(game),
        )
        write(
            "activation",
            "localActivationAvailable=1\nremoteActivationCount=1\n"
            "localActivation.sequence=1\nlocalActivation.object.serverModId=0\nlocalActivation.object.serverBaseId=12345\n"
            "remoteActivation.7.sequence=2\nremoteActivation.7.object.serverModId=0\nremoteActivation.7.object.serverBaseId=12345\n",
        )
        write(
            "magic",
            "localMagicEffectAvailable=1\nremoteMagicEffectCount=1\n"
            "localMagicEffect.sequence=1\nlocalMagicEffect.effect.serverModId=0\nlocalMagicEffect.effect.serverBaseId=111\n"
            "localMagicEffect.caster.serverModId=0\nlocalMagicEffect.caster.serverBaseId=20\n"
            "localMagicEffect.casterIsPlayer=1\nlocalMagicEffect.targetIsPlayer=0\n"
            "remoteMagicEffect.7.sequence=2\nremoteMagicEffect.7.effect.serverModId=0\nremoteMagicEffect.7.effect.serverBaseId=111\n"
            "remoteMagicEffect.7.caster.serverModId=0\nremoteMagicEffect.7.caster.serverBaseId=20\n"
            "remoteMagicEffect.7.casterIsPlayer=1\nremoteMagicEffect.7.targetIsPlayer=0\n",
        )
        write(
            "combat",
            "localCombatHitAvailable=1\nremoteCombatHitCount=1\n"
            "localCombatHit.sequence=1\nlocalCombatHit.hitter.serverModId=0\nlocalCombatHit.hitter.serverBaseId=20\n"
            "localCombatHit.hittee.serverModId=0\nlocalCombatHit.hittee.serverBaseId=300\n"
            "localCombatHit.source.serverModId=0\nlocalCombatHit.source.serverBaseId=30\n"
            "localCombatHit.projectile.serverModId=0\nlocalCombatHit.projectile.serverBaseId=40\n"
            "localCombatHit.rawHitFlags=1502691329\nlocalCombatHit.planckHit=1\n"
            "localCombatHit.hitterIsPlayer=1\nlocalCombatHit.hitteeIsPlayer=0\n"
            "remoteCombatHit.7.sequence=2\nremoteCombatHit.7.hitter.serverModId=0\nremoteCombatHit.7.hitter.serverBaseId=20\n"
            "remoteCombatHit.7.hittee.serverModId=0\nremoteCombatHit.7.hittee.serverBaseId=300\n"
            "remoteCombatHit.7.source.serverModId=0\nremoteCombatHit.7.source.serverBaseId=30\n"
            "remoteCombatHit.7.projectile.serverModId=0\nremoteCombatHit.7.projectile.serverBaseId=40\n"
            "remoteCombatHit.7.rawHitFlags=1\nremoteCombatHit.7.planckHit=0\n"
            "remoteCombatHit.7.hitterIsPlayer=1\nremoteCombatHit.7.hitteeIsPlayer=0\n",
        )
        write(
            "projectile",
            "localProjectileEventAvailable=1\nremoteProjectileEventCount=1\n"
            "localProjectileEvent.sequence=1\nlocalProjectileEvent.source.serverModId=0\nlocalProjectileEvent.source.serverBaseId=20\n"
            "localProjectileEvent.weapon.serverModId=0\nlocalProjectileEvent.weapon.serverBaseId=30\n"
            "localProjectileEvent.originValid=1\nlocalProjectileEvent.destinationValid=1\n"
            "remoteProjectileEvent.7.sequence=2\nremoteProjectileEvent.7.source.serverModId=0\nremoteProjectileEvent.7.source.serverBaseId=20\n"
            "remoteProjectileEvent.7.weapon.serverModId=0\nremoteProjectileEvent.7.weapon.serverBaseId=30\n"
            "remoteProjectileEvent.7.originValid=1\nremoteProjectileEvent.7.destinationValid=1\n",
        )
        write(
            "grab",
            "localGrabAvailable=1\nremoteGrabCount=1\n"
            "localGrab.sequence=1\nlocalGrab.object.serverModId=0\nlocalGrab.object.serverBaseId=12345\n"
            "remoteGrab.7.sequence=2\nremoteGrab.7.object.serverModId=0\nremoteGrab.7.object.serverBaseId=12345\n",
        )
        write(
            "compat",
            "schemaVersion=2\nready=0\nreadinessSource=SkyrimTogetherVR.gameplay\n"
            "higgs.installed=1\nhiggs.loaded=1\n"
            "planck.installed=1\nplanck.loaded=1\n"
            "vrPhysicsCompatibilityModInstalled=1\n"
            "bringupHooksCompiled=1\nunvalidatedHooksCompiled=0\n"
            "connectionOnly=1\nflatOverlay=0\nvalidatedInlinePatches=0\n"
            "unvalidatedGameplayHooksSuppressed=0\n"
            "hookMode=bringup_hooks\n"
            "gameplayMode=connection_only\n"
            "remoteAvatarPolicy=disabled\n"
            "remotePlayerProxyPolicy=readout_only\n"
            "movementPolicy=observation_only\n"
            "equipmentPolicy=observation_only\n"
            "activationPolicy=observation_only\n"
            "inventoryPolicy=equipment_snapshot_only\n"
            "magicPolicy=observation_only\n"
            "combatPolicy=observation_only\n"
            "projectilePolicy=observation_only\n"
            "grabPolicy=observation_only\n"
            "higgsRelayPolicy=observation_only\n"
            "saveLoadPolicy=observation_only\n"
            "discoveryPolicy=observation_only\n"
            "playerCellPolicy=network_only\n"
            "posePolicy=observation_only\n"
            "higgsPolicy=direct_optional_external_bridge\nplanckPolicy=unsupported_no_remote_physical_replay\n",
        )
        write(
            "higgs",
            "bridge.loaded=1\n"
            "bridge.sequence=4\n"
            "higgs.detected=1\n"
            "higgs.interfaceAvailable=1\n"
            "higgs.callbacksRegistered=1\n"
            "higgs.snapshotAvailable=1\n"
            "higgs.snapshotSequence=3\n"
            "higgs.twoHanding=0\n"
            "left.valid=0\n"
            "right.valid=0\n"
            "recentEventCount=0\n",
        )
        write(
            "planck",
            "bridge.loaded=1\n"
            "bridge.sequence=4\n"
            "planck.detected=1\n"
            "planck.interfaceRequestAttempted=1\n"
            "planck.interfaceAvailable=1\n"
            "planck.buildNumber=8\n"
            "planck.currentHitEventAvailable=1\n"
            "planck.currentHitEventObservationOnly=1\n"
            "planck.lastHitDataAvailable=0\n"
            "planck.lastHitDataProbeEnabled=0\n"
            "planck.lastHitDataReason=not_polled_nontrivial_return_boundary\n"
            "planck.lastHitDataBoundary=disabled_unvalidated_by_value_abi\n"
            "planck.policy=observation_only\n",
        )
        write(
            "higgsnet",
            "ready=1\nlocalHiggsAvailable=1\nremoteHiggsCount=1\n"
            "localHiggs.sequence=1\nlocalHiggs.detected=1\nlocalHiggs.interfaceAvailable=1\n"
            "remoteHiggs.7.sequence=2\nremoteHiggs.7.detected=1\nremoteHiggs.7.interfaceAvailable=1\n",
        )
        write(
            "saveload",
            "ready=1\nonline=1\nlocalPlayerId=4\nconnectionState=online\n"
            "loadGameObserved=1\nloadGameCount=1\nreadyAfterLastLoad=1\n"
            "waitingForReadyAfterLoad=0\nsecondsSinceLastLoad=0.5\n"
            "playerFormId=20\nplayerCellFormId=100\nplayerWorldSpaceFormId=60\n"
            "player.serverModId=0\nplayer.serverBaseId=20\n"
            "playerCell.serverModId=1\nplayerCell.serverBaseId=100\n"
            "playerWorldSpace.serverModId=1\nplayerWorldSpace.serverBaseId=60\n",
        )
        write(
            "remoteplayers",
            "ready=1\ntrackedPlayerCount=1\navatarValidationReadyCount=1\nhiggsAvatarValidationReadyCount=1\n"
            "remotePlayer.7.poseAvailable=1\nremotePlayer.7.vrikDetected=1\n"
            "remotePlayer.7.vrikInterfaceAvailable=1\nremotePlayer.7.vrikAvailable=1\n"
            "remotePlayer.7.hmdAvailable=1\nremotePlayer.7.leftHandAvailable=1\nremotePlayer.7.rightHandAvailable=1\n"
            "remotePlayer.7.movementAvailable=1\nremotePlayer.7.sameSpace=1\n"
            "remotePlayer.7.higgsAvailable=1\n"
            "remotePlayer.7.avatarValidationReady=1\nremotePlayer.7.avatarValidationBlocker=ready\n"
            "remotePlayer.7.higgsAvatarValidationReady=1\nremotePlayer.7.higgsAvatarValidationBlocker=ready\n",
        )
        write("avatar", "ready=1\nactorTargetsEnabled=1\nactorSkeletonWritesEnabled=0\nsameSpaceCount=1\nhmdCopiedCount=0\nleftHandCopiedCount=0\nrightHandCopiedCount=0\nvrikDetectedCount=1\nvrikInterfaceAvailableCount=1\ninvalidVrikCount=0\ninvalidTransformCount=0\ninvalidMovementCount=0\nlaunchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\ngamePath={}\n".format(game))
        build_manifest = {
            "schema": collect_runtime_evidence.BUILD_MANIFEST_SCHEMA,
            "mode": "releasedbg",
            "platform": "windows",
            "arch": "x64",
            "avatarSync": False,
            "gameplay": True,
            "packageFlavor": "gameplay",
            "targets": list(collect_runtime_evidence.GAMEPLAY_EXPECTED_MANIFEST_TARGETS),
            "copiedArtifacts": list(collect_runtime_evidence.GAMEPLAY_EXPECTED_RUNTIME_ARTIFACTS),
            "expectedArtifacts": list(collect_runtime_evidence.GAMEPLAY_EXPECTED_RUNTIME_ARTIFACTS),
            "packageRoot": str(game),
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
            "generatedAtUtc": "2026-01-01T00:00:00.0000000Z",
        }
        (game / collect_runtime_evidence.BUILD_MANIFEST_NAME).write_text(
            json.dumps(build_manifest, indent=2),
            encoding="utf-8",
        )
        scenario_path = root / "explicit-scenario-evidence.json"
        primary_client = {
            "playerId": "4",
            "sessionId": "123",
            "connectionGeneration": "1",
            "launchNonce": "0123456789abcdef0123456789abcdef",
            "processId": "42",
        }
        peer_client = {
            "playerId": "5",
            "sessionId": "456",
            "connectionGeneration": "2",
            "launchNonce": "fedcba9876543210fedcba9876543210",
            "processId": "43",
        }
        scenario_path.write_text(
            json.dumps(
                scenario_evidence_fixture(
                    collect_runtime_evidence.vr_handoff.GAMEPLAY_MANDATORY_CANONICAL_DOMAINS,
                    primary=primary_client,
                    peer=peer_client,
                    server_instance_nonce="99",
                ),
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )

        args = collect_runtime_evidence.build_collection_args(
            game_path=game,
            handoff_dir=None,
            log=None,
            gameplay_bridge_log=gameplay_bridge_log,
            out=out_dir,
            skse_log_root=None,
            extra_file=[],
            scenario_evidence=scenario_path,
            crash_evidence=True,
            include_crash_dumps=False,
            skip_log=False,
            no_audit=False,
            require_connected=True,
            require_vrik=True,
            require_higgs=True,
            require_remote_player=True,
            require_weapon_pose=True,
            require_magic_pose=True,
            require_projectile_pose=True,
            require_movement_relay=True,
            require_equipment_relay=True,
            require_activation_relay=True,
            require_magic_relay=True,
            require_combat_relay=True,
            require_projectile_relay=True,
            require_grab_relay=False,
            require_higgs_relay=False,
            require_saveload_observer=True,
            require_gameplay_relays=True,
            avatar_sync=False,
            gameplay=True,
        )
        archive = collect_runtime_evidence.collect(args)
        peer_archive = out_dir / "peer-runtime-evidence.zip"
        with zipfile.ZipFile(archive) as source_zip, zipfile.ZipFile(peer_archive, "w", compression=zipfile.ZIP_DEFLATED) as peer_zip:
            peer_manifest = json.loads(source_zip.read("manifest.json").decode("utf-8"))
            peer_identity_readouts = {
                f"handoff/{collect_runtime_evidence.vr_handoff.READOUT_FILES[name]}"
                for name in collect_runtime_evidence.vr_handoff.RUNTIME_IDENTITY_READOUTS
            }
            peer_payloads: dict[str, bytes] = {}
            for name in source_zip.namelist():
                if name == "manifest.json":
                    continue
                if name == "handoff/SkyrimTogetherVR.gameplay":
                    payload = collect_runtime_evidence.vr_handoff.gameplay_snapshot_fixture(
                        game,
                        launch_nonce="fedcba9876543210fedcba9876543210",
                        process_id=43,
                        session_id=456,
                        connection_generation=2,
                    )
                    peer_payloads[name] = payload.encode("utf-8")
                    peer_zip.writestr(name, payload)
                elif name in peer_identity_readouts:
                    payload = source_zip.read(name).decode("utf-8").replace(
                        "0123456789abcdef0123456789abcdef",
                        "fedcba9876543210fedcba9876543210",
                    ).replace("processId=42", "processId=43")
                    if name == f"handoff/{collect_runtime_evidence.vr_handoff.READOUT_FILES['status']}":
                        payload = payload.replace("playerId=4", "playerId=5")
                    payload = payload.replace("sessionId=123", "sessionId=456").replace(
                        "connectionGeneration=1",
                        "connectionGeneration=2",
                    )
                    peer_payloads[name] = payload.encode("utf-8")
                    peer_zip.writestr(name, payload)
                else:
                    peer_zip.writestr(name, source_zip.read(name))
            for record in peer_manifest["files"]:
                if not isinstance(record, dict):
                    continue
                archive_name = str(record.get("archiveName", ""))
                payload = peer_payloads.get(archive_name)
                if payload is None:
                    continue
                record["size"] = len(payload)
                record["sha256"] = hashlib.sha256(payload).hexdigest()
            peer_zip.writestr("manifest.json", json.dumps(peer_manifest, indent=2, sort_keys=True) + "\n")
        result = audit_archive(
            archive,
            peer_archive=peer_archive,
            require_avatar_sync=True,
            require_gameplay=True,
            require_remote_player=True,
            require_weapon_pose=True,
            require_magic_pose=True,
            require_projectile_pose=True,
            require_movement_relay=True,
            require_equipment_relay=True,
            require_activation_relay=True,
            require_magic_relay=True,
            require_combat_relay=True,
            require_projectile_relay=True,
            require_grab_relay=False,
            require_higgs_relay=False,
            require_saveload_observer=True,
            allow_failed_checks=False,
        )
        if result == 0:
            print("Evidence zip audit self-test did not reject strict gameplay certification without native trace corroboration.")
            return 1

        relaxed_result = audit_archive(
            archive,
            peer_archive=peer_archive,
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
        )
        if relaxed_result == 0:
            print("Evidence zip audit self-test allowed manifest-driven strict certification without native trace corroboration.")
            return 1

        missing_inventory_archive = out_dir / "missing-inventory-runtime-evidence.zip"
        with zipfile.ZipFile(archive) as source_zip, zipfile.ZipFile(
            missing_inventory_archive,
            "w",
            compression=zipfile.ZIP_DEFLATED,
        ) as weakened_zip:
            for name in source_zip.namelist():
                if name != "system/runtime_inventory.json":
                    weakened_zip.writestr(name, source_zip.read(name))
        missing_inventory_result = audit_archive(
            missing_inventory_archive,
            peer_archive=peer_archive,
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
        )
        if missing_inventory_result == 0:
            print("Evidence zip audit self-test did not reject crash evidence without a runtime inventory.")
            return 1

        weakened_archive = out_dir / "weakened-runtime-evidence.zip"
        with zipfile.ZipFile(archive) as source_zip, zipfile.ZipFile(
            weakened_archive,
            "w",
            compression=zipfile.ZIP_DEFLATED,
        ) as weakened_zip:
            manifest = json.loads(source_zip.read("manifest.json").decode("utf-8"))
            gameplay_name = "handoff/SkyrimTogetherVR.gameplay"
            weakened_gameplay = source_zip.read(gameplay_name).replace(
                b"domain.movement.sent=1\n",
                b"domain.movement.sent=0\n",
                1,
            )
            for record in manifest["files"]:
                if isinstance(record, dict) and record.get("archiveName") == gameplay_name:
                    record["size"] = len(weakened_gameplay)
                    record["sha256"] = hashlib.sha256(weakened_gameplay).hexdigest()
                    break
            for name in source_zip.namelist():
                if name == gameplay_name:
                    weakened_zip.writestr(name, weakened_gameplay)
                elif name == "manifest.json":
                    weakened_zip.writestr(name, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
                else:
                    weakened_zip.writestr(name, source_zip.read(name))

        weakened_result = audit_archive(
            weakened_archive,
            peer_archive=peer_archive,
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
        )
        if weakened_result == 0:
            print("Evidence zip audit self-test certified weakened evidence without native trace corroboration.")
            return 1

        no_scenario_archive = out_dir / "no-scenario-runtime-evidence.zip"
        with zipfile.ZipFile(archive) as source_zip, zipfile.ZipFile(
            no_scenario_archive,
            "w",
            compression=zipfile.ZIP_DEFLATED,
        ) as no_scenario_zip:
            for name in source_zip.namelist():
                if name != SCENARIO_EVIDENCE_ARCHIVE_ENTRY:
                    no_scenario_zip.writestr(name, source_zip.read(name))
        no_scenario_result = audit_archive(
            no_scenario_archive,
            peer_archive=peer_archive,
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
        )
        if no_scenario_result == 0:
            print("Evidence zip audit self-test accepted aggregate counters without scenario correlation.")
            return 1

        harmful_loss_archive = out_dir / "harmful-loss-runtime-evidence.zip"
        with zipfile.ZipFile(archive) as source_zip, zipfile.ZipFile(
            harmful_loss_archive,
            "w",
            compression=zipfile.ZIP_DEFLATED,
        ) as harmful_zip:
            manifest = json.loads(source_zip.read("manifest.json").decode("utf-8"))
            gameplay_name = "handoff/SkyrimTogetherVR.gameplay"
            harmful_gameplay = source_zip.read(gameplay_name).replace(
                b"bridge.eventRingDroppedPushes=0\n",
                b"bridge.eventRingDroppedPushes=1\n",
                1,
            )
            for record in manifest["files"]:
                if isinstance(record, dict) and record.get("archiveName") == gameplay_name:
                    record["size"] = len(harmful_gameplay)
                    record["sha256"] = hashlib.sha256(harmful_gameplay).hexdigest()
                    break
            for name in source_zip.namelist():
                if name == gameplay_name:
                    harmful_zip.writestr(name, harmful_gameplay)
                elif name == "manifest.json":
                    harmful_zip.writestr(name, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
                else:
                    harmful_zip.writestr(name, source_zip.read(name))

        harmful_loss_result = audit_archive(
            harmful_loss_archive,
            peer_archive=peer_archive,
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
        )
        if harmful_loss_result == 0:
            print("Evidence zip audit self-test did not reject harmful bridge loss.")
            return 1

        weakened_avatar_archive = out_dir / "weakened-avatar-runtime-evidence.zip"
        with zipfile.ZipFile(archive) as source_zip, zipfile.ZipFile(
            weakened_avatar_archive,
            "w",
            compression=zipfile.ZIP_DEFLATED,
        ) as weakened_zip:
            manifest = json.loads(source_zip.read("manifest.json").decode("utf-8"))
            manifest["requiredRemotePlayer"] = False
            checklist = json.loads(source_zip.read("runtime_checklist.json").decode("utf-8"))
            for check in checklist.get("checks", []):
                if isinstance(check, dict) and check.get("id") in {
                    "remote_player_proxy",
                    "remote_avatar_ready",
                    "remote_higgs_avatar_ready",
                }:
                    check["status"] = collect_runtime_evidence.CHECK_NOT_REQUIRED
                    check["detail"] = "self-test intentionally weakened avatar-sync remote-player lane"
            checklist["summary"] = {
                collect_runtime_evidence.CHECK_PASS: sum(
                    1
                    for check in checklist.get("checks", [])
                    if isinstance(check, dict) and check.get("status") == collect_runtime_evidence.CHECK_PASS
                ),
                collect_runtime_evidence.CHECK_FAIL: sum(
                    1
                    for check in checklist.get("checks", [])
                    if isinstance(check, dict) and check.get("status") == collect_runtime_evidence.CHECK_FAIL
                ),
                collect_runtime_evidence.CHECK_NOT_REQUIRED: sum(
                    1
                    for check in checklist.get("checks", [])
                    if isinstance(check, dict) and check.get("status") == collect_runtime_evidence.CHECK_NOT_REQUIRED
                ),
            }
            manifest["runtimeChecklist"] = checklist["summary"]
            for name in source_zip.namelist():
                if name == "runtime_checklist.json":
                    weakened_zip.writestr(name, json.dumps(checklist, indent=2, sort_keys=True) + "\n")
                elif name == "manifest.json":
                    weakened_zip.writestr(name, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
                else:
                    weakened_zip.writestr(name, source_zip.read(name))

        weakened_avatar_result = audit_archive(
            weakened_avatar_archive,
            peer_archive=peer_archive,
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
        )
        if weakened_avatar_result == 0:
            print("Evidence zip audit self-test did not reject an avatarSyncAudit archive with weakened VRIK/HIGGS remote-player lanes.")
            return 1

        print(f"Evidence zip audit self-test archive: {archive}")
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=pathlib.Path, nargs="?", help="runtime evidence zip to audit")
    parser.add_argument(
        "--peer-archive",
        type=pathlib.Path,
        help="second trusted --gameplay evidence zip required for paired scenario correlation",
    )
    parser.add_argument("--require-avatar-sync", action="store_true", help="require the archive to come from --avatar-sync collection")
    parser.add_argument("--require-gameplay", action="store_true", help="require trusted paired explicit action correlation for every mandatory canonical gameplay domain")
    parser.add_argument(
        "--require-gameplay-bootstrap",
        action="store_true",
        help="require the archive to come from one-client --gameplay-bootstrap collection",
    )
    parser.add_argument("--require-remote-player", action="store_true", help="require remote-player proxy and VRIK avatar readiness checklist lanes")
    parser.add_argument("--require-weapon-pose", action="store_true", help="require weapon offset pose nodes to pass")
    parser.add_argument("--require-magic-pose", action="store_true", help="require spell or magic aim/origin pose nodes to pass")
    parser.add_argument("--require-projectile-pose", action="store_true", help="require arrow/projectile pose nodes to pass")
    parser.add_argument("--require-vr-pose-context", action="store_true", help="require weapon, magic, and projectile pose context to pass")
    parser.add_argument("--require-movement-relay", action="store_true", help="require movement relay evidence to pass")
    parser.add_argument("--require-equipment-relay", action="store_true", help="require equipment relay evidence to pass")
    parser.add_argument("--require-activation-relay", action="store_true", help="require activation relay evidence to pass")
    parser.add_argument("--require-magic-relay", action="store_true", help="require magic relay evidence to pass")
    parser.add_argument("--require-combat-relay", action="store_true", help="require combat relay evidence to pass")
    parser.add_argument("--require-projectile-relay", action="store_true", help="require projectile relay evidence to pass")
    parser.add_argument("--require-grab-relay", action="store_true", help="require optional direct HIGGS grab/release evidence to pass")
    parser.add_argument("--require-higgs-relay", action="store_true", help="require optional direct HIGGS relay evidence to pass")
    parser.add_argument("--require-saveload-observer", action="store_true", help="require save/load observer evidence to pass")
    parser.add_argument("--require-gameplay-relays", action="store_true", help="require every mandatory canonical gameplay relay evidence lane")
    parser.add_argument("--allow-failed-checks", action="store_true", help="report checklist failures without returning a failing exit code")
    parser.add_argument(
        "--validate-scenario-evidence",
        type=pathlib.Path,
        help="validate a standalone explicit scenario JSON record without making a paired certification claim",
    )
    parser.add_argument("--self-test", action="store_true", help="run a temp-directory evidence zip audit fixture")
    args = parser.parse_args()

    if sum((args.require_avatar_sync, args.require_gameplay, args.require_gameplay_bootstrap)) > 1:
        parser.error("--require-avatar-sync, --require-gameplay, and --require-gameplay-bootstrap cannot be combined")
    if args.require_vr_pose_context:
        args.require_weapon_pose = True
        args.require_magic_pose = True
        args.require_projectile_pose = True
    if args.require_avatar_sync:
        args.require_remote_player = True
    if args.require_gameplay:
        args.require_remote_player = True
        args.require_weapon_pose = True
        args.require_magic_pose = True
        args.require_projectile_pose = True
        args.require_gameplay_relays = True
    if args.require_gameplay_relays:
        args.require_movement_relay = True
        args.require_equipment_relay = True
        args.require_activation_relay = True
        args.require_magic_relay = True
        args.require_combat_relay = True
        args.require_projectile_relay = True
        args.require_saveload_observer = True

    if args.self_test:
        return command_self_test()
    if args.validate_scenario_evidence:
        source = args.validate_scenario_evidence.expanduser().resolve()
        try:
            evidence = json.loads(source.read_text(encoding="utf-8"))
        except OSError as exc:
            print(f"Scenario evidence cannot be read: {exc}")
            return 1
        except json.JSONDecodeError as exc:
            print(f"Scenario evidence is not valid JSON: {exc}")
            return 1
        failures = validate_scenario_evidence(
            evidence,
            expected_domains=(),
            collection_created_utc=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
            strict_gameplay_certification=False,
        )
        print(f"Scenario evidence: {source}")
        print("Scenario evidence trust: untrusted/manual shape validation only; no native trace corroboration")
        print(f"Validation failures: {len(failures)}")
        for failure in failures:
            print(f"- {failure}")
        return 1 if failures else 0
    if not args.archive:
        parser.error("archive is required unless --self-test is used")
    return audit_archive(
        args.archive.expanduser().resolve(),
        peer_archive=args.peer_archive.expanduser().resolve() if args.peer_archive else None,
        require_avatar_sync=args.require_avatar_sync,
        require_gameplay=args.require_gameplay,
        require_remote_player=args.require_remote_player,
        require_weapon_pose=args.require_weapon_pose,
        require_magic_pose=args.require_magic_pose,
        require_projectile_pose=args.require_projectile_pose,
        require_movement_relay=args.require_movement_relay,
        require_equipment_relay=args.require_equipment_relay,
        require_activation_relay=args.require_activation_relay,
        require_magic_relay=args.require_magic_relay,
        require_combat_relay=args.require_combat_relay,
        require_projectile_relay=args.require_projectile_relay,
        require_grab_relay=args.require_grab_relay,
        require_higgs_relay=args.require_higgs_relay,
        require_saveload_observer=args.require_saveload_observer,
        allow_failed_checks=args.allow_failed_checks,
        require_gameplay_bootstrap=args.require_gameplay_bootstrap,
    )


if __name__ == "__main__":
    raise SystemExit(main())
