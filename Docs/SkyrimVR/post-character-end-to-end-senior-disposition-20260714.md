# Post-character end-to-end senior review disposition

| Recommendation | Disposition | Implementation |
| --- | --- | --- |
| Resolve `GetCurrentLocation` lazily with direct VR ID `19385` and a null guard. | Adopted | The VR accessor no longer depends on `Initializer::RunAll` or the AE alias file; desktop behavior remains unchanged. |
| Do not run all gameplay initializers in the default VR build. | Adopted | Bring-up hook isolation remains intact for HIGGS/PLANCK compatibility. |
| Narrow the default target to the connection/discovery/player-cell path. | Adopted | Observation, pose, and remote-proxy lanes are disabled in the default target and explicitly enabled for avatar-sync/gameplay targets. |
| Initialize and commit transport identity before dispatching events. | Adopted | Local player ID starts at zero, is assigned directly on accepted auth, and is cleared directly on disconnect. |
| Add session/connection generation to runtime proof. | Adopted | Transport exposes a per-process session ID and monotonically increasing accepted-connection generation; status and cell/grid records include both. |
| Count cell/grid requests only when transport send succeeds. | Adopted | Player-cell counters and last-request records now update only after a successful `Send`. |
| Suppress inherited autoconnect during unattended testing. | Adopted | The driver removes `STVR_AUTOCONNECT` and `STVR_PASSWORD` before launching, in addition to setting the launcher disable switch. |
| Reject stale or incomplete local proof. | Adopted | Automation requires a fresh session, a generation newer than baseline, matching player/generation IDs, positive grid cells, exterior state, nonzero base IDs, and zero translation failures. |
| Keep the forced post-auth discovery visit. | Adopted | Pre-auth discovery can populate caches while sends are suppressed; the forced visit is required to emit first online cell/grid state. |
| Add a second address-only initializer lifecycle. | Rejected | One self-resolving read-only accessor does not justify a parallel initializer framework. |
| Remove location telemetry from connection-only mode. | Rejected | The direct VR function is known and null-safe; both discovery and Discord use the accessor. |
| Treat local status as final server proof. | Rejected | Runtime acceptance still requires corroborating server admission and message processing evidence. |

The review also corrected the frame order to `PreUpdate -> runner drain -> Update`.
Discovery therefore executes before connection command polling on a frame, and a
queued connect command runs during the next frame's runner drain.
