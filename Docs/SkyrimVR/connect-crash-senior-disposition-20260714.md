# Pre-authentication crash senior disposition

| Recommendation | Disposition | Implementation |
| --- | --- | --- |
| Build a VR-specific authentication request without player/calendar engine calls. | Adapted | VR uses an explicit username, zero initial world/cell, level 1, and the defined default `TimeModel`; desktop keeps its existing player-derived fields. |
| Preserve the loaded mod list. | Adapted | The direct TESDataHandler/`ModManager` file list remains the authoritative snapshot, now with a null-entry guard and a loaded-count log. |
| Remove connection-only `Actor::GetLevel` reads. | Adapted | Both periodic polling and status generation use fallback level 1. The unreachable desktop/gameplay level paths remain available outside connection-only mode. |
| Report an authentication send failure. | Adapted | A false `Send` result is described precisely as "not queued because transport disconnected" and dispatches one `authentication_not_queued` error; the underlying void network API cannot prove enqueue success. |
| Preserve accepted-event ordering. | Adopted | Mod mappings are dispatched before settings and `ConnectedEvent`, so forced discovery translates and sends cell/grid state using the accepted mapping. |
| Require runtime correction of initial location defaults. | Adopted | Success requires the same process session/generation to report nonzero grid and cell request counts after server admission. |

The senior review found no P0 issue in the proposed boundary. Its P1 correction was
that the player-cell status writer independently called `GetLevel`; removing only
the periodic updater would have moved the same unsafe read instead of eliminating it.

The runtime fault address maps into Skyrim VR's `BSExtraDataList` region and is
consistent with actor-level resolution, but the patch does not depend on proving
the exact nested instruction: the SKSE task callback no longer calls any player or
calendar engine function while constructing VR authentication.
