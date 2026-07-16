# Authentication Acceptance Crash Senior Disposition - 2026-07-16

## Decision

The exact-version server admitted the `3cf4aa0e` client, but Skyrim VR exited
before local online state was published. A second reproduction with Wine SEH
logging captured `c0000005` at Skyrim VR RVA `0x664759`. The package PDB resolved
the client caller chain through `PlayerService::OnServerSettingsReceived`,
`ToggleDeathSystem`, and `Actor::SetPlayerRespawnMode`.

A Sol max read-only review verified the trace, the compile flags, EnTT listener
order, and the absence of synchronous receive re-entry inside `Client::Send`.

## Disposition

| Recommendation | Disposition | Implementation |
| --- | --- | --- |
| Make VR PlayerService safety follow the player-cell service flag, not the connection-only package flavor. | Adopted | `kVrNetworkOnlyPlayerService` is true whenever VR player-cell service is enabled, including gameplay packages. |
| Reject the old predicate in the source audit. | Adopted | The audit requires the exact service-gated predicate and verifies the early return precedes desktop ConnectedEvent and ServerSettings subscriptions. |
| Preserve Discovery's service-gated strict-enforcement bypass. | Adopted | The same audit locks the existing discovery predicate. |
| Add durable checkpoints around authentication and each immediate ConnectedEvent listener. | Adopted | VR-only checkpoints flush synchronously at each stage and around avatar assignment send. |
| Lock pinned EnTT listener and nested event order. | Adopted | A synthetic dispatcher test records first listener, nested cell event, completion, then second listener. |
| Add a catch-all or SEH recovery boundary. | Rejected | The proven fault is a native access violation after entering an invalid game routine; continuing after partial mutation is unsafe. |
| Patch only `SetNoBleedoutRecovery`, suppress ServerSettings, or reorder listeners. | Rejected | Each option hides the first fault while leaving other desktop PlayerService mutations enabled or changing original protocol semantics. |

## Acceptance Gate

The next exact-revision client/server test must reach
`auth.connected_dispatch.done`, publish `online=1` with a nonzero player ID,
send the current-cell request for the same connection generation, remain
connected for at least 30 seconds, disconnect cleanly, and produce no new access
violation or dump. Two-client gameplay parity remains a later phase.
