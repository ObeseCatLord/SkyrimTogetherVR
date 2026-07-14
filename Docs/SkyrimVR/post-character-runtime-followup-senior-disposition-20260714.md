# Senior Review Disposition: Post-Character Runtime Follow-up

> Superseded on 2026-07-14 by
> `startup-lifecycle-senior-disposition-20260714.md`. Live testing reproduced an
> access violation after generic RaceSex `kHide`; stable player/cell snapshots
> while RaceSex is open are not proof of a completed engine transaction.

| Recommendation | Disposition | Implementation |
| --- | --- | --- |
| Use direct VR singleton ID `517014` with one pointer load. | Adopted | `PlayerCharacter::Get()` selects `517014` only for `TP_SKYRIM_VR`; desktop retains `401069`. The VR services audit requires the exact conditional source shape. |
| Do not rely on `OnLocationChange` alone. | Adopted | Both player aliases use `OnCellAttach` as the primary New Game re-arm and `OnLocationChange` as fallback. `OnPlayerLoadGame` remains for saves. |
| Replace, rather than stack, the Papyrus timer. | Adopted | Each owning quest exposes `RearmCadence(reason)`, claims its owner, calls `UnregisterForUpdate()`, then registers one 50 ms update. Papyrus trace records owner and reason. |
| Require a non-cancel controller action. | Adopted | Automation rejects a confirmation whose first/default button is `cancelIndex`, then holds the Monado trigger across OpenXR sync frames. |
| Prove stable readiness before and after closing stranded RaceSex UI. | Adopted | The script requires Realm placement, stable name/race/cell snapshots, active plugin, and client `playercell ready=1` with nonzero player form before closing RaceSex, then verifies those values remain stable. |
| Claim full character finalization only after a name event or equivalent identity proof. | Adapted | The automated gate is explicitly scoped to connection-only readiness. It does not claim the unavailable SteamVR naming workflow completed; it proves a stable named/raced player, Realm cell, client readiness, and later server admission. |

## Corrected runtime chronology

The last normal task completed immediately before renderer initialization at
10:37:22.541. The manual diagnostic tick at 10:41:52 proved the connection
service was reachable but exposed the wrong player singleton relocation. The
two failures are independent and both are covered by this patch.
