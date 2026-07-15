# Canonical Avatar Foundation Senior Review Disposition

Date: 2026-07-15

The senior review verified the bridge architecture but found three P0 defects
before the first two-client mutation test: packed EnTT ID zero was treated as
an unassigned sentinel, command results were not correlated to pending
operations, and failed disconnect retirement allowed a later session to
continue with stale adapter actors.

| Finding | Disposition | Implementation |
|---|---|---|
| Split packed EnTT identity into stable slot and generation. | Adopted | `VRCanonicalEntityIdentity.h` performs checked slot/version plus-one encoding and reconstruction. Packed server ID zero remains valid. |
| Replace plain generation ordering. | Adopted with a documented bound | `VRCanonicalEntity.h` uses modulo-4095 serial comparison and accepts at most 2047 unseen forward incarnations. Same-generation recreation is allowed only after a completed visibility destroy with a newer action ID. A full-cycle replay remains unprovable without a wider server incarnation field. |
| Correlate bridge command results. | Adopted | Successful submission returns its assigned action or sequence ID. The avatar service accepts only exact pending IDs and treats latest-update rejection as lifecycle uncertainty. |
| Recover from lost result events. | Adopted | Creates are idempotently retried after a bounded timeout. Exhausted create acknowledgement recovery and any destroy acknowledgement timeout retire the lifecycle epoch. |
| Treat destroy enqueue backpressure as lifecycle corruption. | Rejected | Failed enqueue is retried with backoff and does not consume an accepted-operation attempt. Only an accepted destroy timeout or explicit rejection retires the lifecycle. |
| Fail closed after retirement failure. | Adopted | `TransportService` latches cleanup-required, disables avatar capability, and blocks later authentication until bridge retirement succeeds. |
| Replace the actor factory or apply appearance now. | Deferred | `PlaceObjectAtMe` remains the first-runtime factory. Telemetry explicitly reports `player_template_fallback`; appearance parity is not claimed. |
| Add temporary-reference flags without runtime evidence. | Rejected | The current handle plus `Disable`/`SetDelete` cleanup remains unchanged until Skyrim VR proves the factory and persistence behavior. |

The patch also stops root-transform submissions after convergence and uses
shortest-arc normalized quaternion interpolation. This removes permanent
command-ring pressure after the last movement packet.

A follow-up read-only review found that ordinary server visibility removal
does not destroy the EnTT entity. The final implementation therefore permits
same-generation recreation only after a strictly newer action follows the
completed destroy. A spawn received while destroy is pending is retained and
replayed after the old handle is confirmed gone. Failed destroy enqueue uses
backoff and does not itself trigger lifecycle retirement.

Required runtime proof remains unchanged: two clients in one cell must prove
create, movement convergence, despawn, disconnect cleanup, and cleanup-latch
recovery. Skyrim was not launched during implementation or source review.
