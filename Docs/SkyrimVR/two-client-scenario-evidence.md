# Two-Client Scenario Evidence

`--require-gameplay` and every staged `--require-*-relay` paired audit are
fail-closed for certification. They do not accept aggregate `captured`, `sent`,
or `applied` counters as proof that one client performed the action the other
client observed. Those counters remain bridge health and activity diagnostics
only.

The current archive format has no native engine or server action trace to
cross-check any action ID or digest. Therefore strict paired certification
always reports `native-trace-corroboration-missing`, even when the prepared
scenario record is well-formed and binds to both archived sessions.

Each client archive must contain the exact same pre-created JSON file at
`scenario/explicit-scenario-evidence.json`. The collector copies it byte for
byte; it never creates, fills in, or rewrites observations:

```bash
python3 Tools/SkyrimVR/collect_runtime_evidence.py \
  --gameplay --scenario-evidence /path/to/explicit-scenario-evidence.json \
  --out /path/to/client-a.zip
python3 Tools/SkyrimVR/collect_runtime_evidence.py \
  --gameplay --scenario-evidence /path/to/explicit-scenario-evidence.json \
  --out /path/to/client-b.zip
python3 Tools/SkyrimVR/audit_runtime_evidence_zip.py \
  /path/to/client-a.zip --peer-archive /path/to/client-b.zip --require-gameplay
```

Use `python3 Tools/SkyrimVR/audit_runtime_evidence_zip.py
--validate-scenario-evidence FILE` to validate a prepared file before
collection. That standalone validation checks shape and its current time window
only. Its input is untrusted/manual operator evidence and it does not make a
paired certification claim.

## Schema

The artifact schema is `skyrim_together_vr_explicit_scenario_evidence_v1`.
All identity and nonce values are JSON strings. Generate `scenarioNonce` and
every `correlationToken` with `secrets.token_hex(32)`: 64 lowercase hexadecimal
characters. A format check cannot prove that the producer used a random source.

```json
{
  "schema": "skyrim_together_vr_explicit_scenario_evidence_v1",
  "scenarioNonce": "<64-lowercase-hex-from-secrets-token-hex-32>",
  "serverInstanceNonce": "<exact-shared-server-instance-nonce>",
  "window": {
    "startedUtc": "2026-08-19T12:00:00Z",
    "endedUtc": "2026-08-19T12:04:00Z"
  },
  "clients": {
    "primary": {
      "playerId": "<status-playerId>",
      "sessionId": "<status-sessionId>",
      "connectionGeneration": "<status-connectionGeneration>",
      "launchNonce": "<status-launchNonce>",
      "processId": "<status-processId>"
    },
    "peer": {
      "playerId": "<different-status-playerId>",
      "sessionId": "<different-status-sessionId>",
      "connectionGeneration": "<different-status-connectionGeneration>",
      "launchNonce": "<different-status-launchNonce>",
      "processId": "<different-status-processId>"
    }
  },
  "domains": [
    {
      "domain": "movement",
      "actions": [
        {
          "correlationToken": "<unique-64-lowercase-hex>",
          "initiator": "primary",
          "receiver": "peer",
          "initiatedUtc": "2026-08-19T12:01:00Z",
          "receivedUtc": "2026-08-19T12:01:01Z",
          "target": null,
          "proof": {
            "kind": "operator_supplied",
            "actionIdentifier": "<operator-claimed-action-id>",
            "payloadDigestSha256": "<operator-claimed-payload-sha256>"
          },
          "receiverObservation": {
            "status": "applied",
            "observedUtc": "2026-08-19T12:01:01Z",
            "postState": { "position": "<receiver-observed-state>" }
          }
        },
        {
          "correlationToken": "<another-unique-64-lowercase-hex>",
          "initiator": "peer",
          "receiver": "primary",
          "initiatedUtc": "2026-08-19T12:01:30Z",
          "receivedUtc": "2026-08-19T12:01:31Z",
          "target": null,
          "proof": {
            "kind": "operator_supplied",
            "actionIdentifier": "<operator-claimed-action-id>",
            "payloadDigestSha256": "<operator-claimed-payload-sha256>"
          },
          "receiverObservation": {
            "status": "applied",
            "observedUtc": "2026-08-19T12:01:31Z",
            "postState": { "position": "<receiver-observed-state>" }
          }
        }
      ]
    }
  ]
}
```

Create one domain record for every required domain. A full gameplay audit
requires both `primary` to `peer` and `peer` to `primary` actions for every
canonical gameplay domain. A staged `--require-*-relay` audit requires the
same two directions for that requested domain. Do not duplicate a domain or a
correlation token.

`target` must be `{ "kind": "...", "id": "..." }` for animation,
appearance, equipment, inventory, actor state, object, combat, projectile,
magic, quest, dialogue, and NPC ownership. It is `null` for domains without a
target identity, such as movement or world state.

The supported successful receiver statuses are `applied`, `completed`,
`rehydrated`, and `observed`. Every action needs a nonempty receiver-observed
`postState`. Action times must be ordered and fit a two-minute interval. The
scenario window is at most 15 minutes and must end no more than five minutes
before the primary collection time (or 30 seconds after it).

## Manual And Unproven Evidence

Use this form when even an operator claim of an action ID and digest is not
available:

```json
{
  "proof": {
    "kind": "manual_unproven",
    "reason": "The current bridge does not emit an engine action ID or payload digest."
  }
}
```

`operator_supplied` is a shape-checked claim, not engine proof. The legacy
`engine_correlated` label is rejected because supplied JSON cannot assert native
engine correlation. Strict paired audits reject every scenario record until a
native trace is archived and cross-checked; they do not fall back to aggregate
counters. `vr_body_pose` (embodiment) and any supplied `audio` domain must also
include this clearly labeled field on every action:

```json
{
  "manualHumanObservation": {
    "kind": "manual_human_observation",
    "observer": "<person-or-test-role>",
    "observedUtc": "2026-08-19T12:01:01Z",
    "statement": "Manual observation only; not automatic proof."
  }
}
```

The human observation is context, not an automatic proof. This tranche does
not add gameplay wire messages, signatures, a native action trace, or a trusted
event ledger. The validator can bind supplied records to both archived live
sessions and reject missing domains, non-identical copies, duplicate/replayed
tokens, identity or server-session mismatches, stale/out-of-window times, and
missing post-state. It cannot certify gameplay or detect replay in a separate
audit run without future native trace and server-side replay support.

## Scope Boundary

`--gameplay-bootstrap` remains a useful one-client live bootstrap audit. It
validates local connection, lifecycle, cell, local avatar, VRIK, and HIGGS
readiness; it is explicitly not two-client gameplay parity proof.
