# FBT Networking Senior Review Disposition

Date: 2026-07-14

The Sol Max review accepted a narrow first implementation: capture the local
post-VRIK body result, validate and relay it, and cache it for remote players.
It rejected direct remote skeleton writes until an actor-specific
post-animation phase is proven.

| Review point | Disposition | Implementation |
|---|---|---|
| Use the HIGGS post-VRIK/post-HIGGS callback for local capture only | Accepted | `SkyrimTogetherVRHiggsBridge` invokes the launcher callback after its HIGGS snapshot. `VRBodyPoseCapture` resolves and copies the local pelvis and leg nodes. |
| Do not mutate remote actors from the HIGGS callback | Accepted | The callback has no remote-player lookup, EnTT access, network send, logging, file I/O, allocation, or scene write. |
| Bridge callback and normal update with a nonblocking mailbox | Accepted | The producer and consumer use `TryAcquireSRWLockExclusive` and `TryAcquireSRWLockShared`; contention drops a sample instead of blocking the callback. |
| Carry pelvis local translation/rotation and six limb local rotations | Accepted | Body format 1 contains pelvis plus left/right thigh, calf, and foot. Limb translations are required to be near zero and scale near one. |
| Omit spine nodes in version 1 | Accepted | FBT defaults to world-only spine correction, so spine local transforms do not faithfully represent the rendered correction. |
| Give body data independent freshness | Accepted | Capture sequence, skeleton-root generation, and a 250 ms local mailbox age are tracked independently of the pose packet sequence. Stale capture is transmitted as an explicit invalid format-1 body. |
| Validate right-handed transforms strictly | Accepted | Shared client/server validation checks finite values, bounds, near-unit orthogonal axes, scale, and positive determinant. |
| Keep remote body rendering disabled | Accepted | `CharacterService` records validated body availability and freshness in telemetry only. No body-node writes are enabled. |
| Require matched client/server protocol builds | Accepted | `VRPoseUpdate` remains a fixed-order schema. Body `FormatVersion` validates the nested body lane; it is not mixed-build wire negotiation. |

The supplied SkyrimVR-FBT source has no redistribution license or build
project in its archive. It is used as an ABI and ordering reference only and
is not vendored into or redistributed with SkyrimTogetherVR.

