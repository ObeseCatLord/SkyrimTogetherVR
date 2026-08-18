# Runtime Admission Senior Review Disposition

Date: 2026-08-18

This records the disposition of the Sol max review of the per-launch runtime
admission design. The review covered identity publication, package and server
version binding, evidence collection, Linux launch ownership, XR readiness,
plugin admission, and the final automated menu-state gate. It did not review
gameplay parity or authorize deployment.

| Recommendation | Disposition | Result |
| --- | --- | --- |
| Repair the missing `re` import in the evidence collector. | Adopted | The collector imports `re`; its self-test covers the affected path. |
| Bind a live client to the installed package `networkVersion`. | Adopted | Authentication requires the exact build version and live evidence checks the client/server version against the validated package manifest. |
| Require a canonical game root on every identity-bearing readout. | Adopted | Status, lifecycle, player-cell, and avatar readouts require matching `gamePath`, process ID, and launch nonce. |
| Never call generic or crash-only evidence trusted. | Adopted | Only an explicitly requested, successful live-admission collection is trusted. A failed live-admission collection still writes its archive but exits nonzero. |
| Publish native readouts atomically. | Adopted | Shared same-directory temporary-file publication flushes and closes before an atomic replacement. Failed writes preserve the previous destination and dirty writers retry. |
| Audit one immutable set of readout bytes. | Adopted | Live collection snapshots every handoff readout once; checklist evaluation, archive content, and offline audit use the sealed snapshot. |
| Reject internal package-manifest disagreement. | Adopted | The archived package manifest must exactly equal `manifest.json.packageBuildManifest`; a semantically valid tamper fixture is rejected. |
| Acquire the Linux game-root lock before profile or game-root mutation. | Adopted | Non-dry launches take a nonblocking canonical-root lock before rewriting plugin order or restoring disabled runtime DLLs. Dry runs remain nonmutating. |
| Make the final usable-menu gate temporal, not a single sample. | Adopted | Automation requires a sustained post-load HUD interval and restarts the interval after any modal, loading, fader, main-menu, or character-creation transition. |
| Prove the DevBench `mods` ordering semantics before making it a release gate. | Resolved from source | The sibling DevBench implementation returns the engine loaded-mod order followed by light plugins. The isolated release lane requires the exact full ordered set and no light plugins. |
| Keep the exact isolated plugin-set policy for release admission. | Adopted | Extra, missing, reordered, or light plugins fail the automated release lane rather than being counted as release evidence. |
| Verify XR readiness without mutating runtime ownership. | Adopted | `manage-monado-runtime.sh check` validates the listener and OpenXR instance/system/view configuration without starting, stopping, or querying managed service ownership. |
| Make Linux package identity checks structural. | Adopted | The launcher rejects malformed, dirty, mismatched, unsupported-platform, unsupported-architecture, and unsupported-flavor manifests before launch. |
| Prevent generic evidence from being promoted by final-handoff tooling. | Adopted during verification | Final-handoff discovery and explicit audit now require exact live/trusted status; an otherwise valid generic archive is a negative regression fixture. |

The senior pass changed the implementation in three material ways: it made
native readout publication atomic, made evidence operate on a sealed readout
snapshot, and moved Linux launch ownership ahead of shared-state mutation. It
also converted the final menu proof from a point observation into a sustained
state gate.

Deployment, handoff regeneration, and server replacement remain explicitly out
of scope for this change set.
