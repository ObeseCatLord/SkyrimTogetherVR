# Fadeout, resync, and connection-stability senior-review disposition (2026-08-22)

Review method: one `gpt-5.6-sol` ultra reviewer verified the evidence brief before critique. The main agent then spot-checked the activation history, PDB mapping, quest authorization path, event-sink semantics, and Fader policy against source and runtime evidence.

| Finding or proposal | Disposition | Reason and verified evidence |
|---|---|---|
| Restore the raw local-player pointer gate before all activation native reads | Adopt, P0 | The exact PDB maps the faulting return to `HookActivateRef` immediately after its original call. Commit `8e4342f6` removed the former local-player-first rejection and began inspecting arbitrary actor activations. Restoring that boundary is the narrowest evidenced regression repair while preserving local pre/post activation replication. |
| Keep owned-NPC outbound activation capture | Reject for this release | It requires native target/actor reads on arbitrary activation threads and is the behavior added at the regression boundary. Local-player activation remains supported. |
| Disable the activation detour immediately | Contingent fallback | If a fresh acceptance run reproduces the same access violation after local-player gating, disable only this hook and repeat the same test. Stability only with the hook disabled would isolate detour/trampoline sensitivity. |
| Replace object activation replication with `TESActivateEvent` | Reject | The existing event service is telemetry-only. It cannot provide the accepted return and exact pre/post open-state contract used by `ActivateRequest` and remote stale-state checking. |
| Authorize self-owner quest snapshots before party membership | Adopt, P1 | `OnConnected` requests this snapshot, but current authorization silently rejects it and exhausts the retry budget. Self-owner is safe without a party; remote owners still require the same nonzero party. |
| Encode solo quest response as `HasParty=false`, `PartyId=0` | Adopt, P1 | This is already a valid wire contract and avoids dereferencing an absent optional party. |
| Increase quest retries or reconnect on exhaustion | Reject | The failure is an authorization contract mismatch, not transient transport loss. |
| Change Fader recovery or treat Fader as a lifecycle boundary | Reject | Current Fader policy did not hide during the crashing runs, is gated by stable UI/loading/identity evidence, and does not retire transport. The observed disconnect follows the Windows access violation. |
| Treat XRizer panic as root cause | Reject | Valid XRizer frame presentation continued after authentication in a diagnostic run, while the symbolized Windows faults precede teardown panic in the SEH run. |

## Adopted implementation invariants

- `CapturePreActivation` rejects a non-player activator by raw pointer identity before `As`, form ID, target, base, cell, worldspace, position, or open-state access.
- Every intercepted native activation still invokes the original exactly once.
- Publication occurs only after an accepted original call and retains current pre/post state semantics.
- Quest authorization rejects zero IDs, permits `requester == owner` without a party, and otherwise requires one shared nonzero party.
- Fader, lifecycle, capabilities, and protocol version remain unchanged.

## Runtime decision gate

Accept the repair only after a fresh no-save client remains ready for at least two minutes on two connected runs, emits no quest retry exhaustion, access violation, XRizer abort, REST disappearance, or server `BadConnection`, and survives a deliberate fade/load transition. If the activation fault recurs, use the single-hook disable fallback before investigating any broader subsystem.
