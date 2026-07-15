# Senior Review Brief: Skyrim VR Lifecycle Menu Predicate

## Goal And Scope

Select the smallest safe fix for a reproducible Skyrim VR lifecycle false
positive that prevents the default connection-only client from becoming ready
after a valid New Game. This is a solo-maintained VR port. The fix must retain
the fail-closed main-menu/RaceSex/loading/fader gate without trusting an
unverified Skyrim VR function address.

Review only `UI::GetMenuOpen`, its VR ABI/layout dependencies,
`VRLifecycleService::GetBlockingMenuName`, and focused regression/audit
coverage. Do not re-review the launcher, update-owner architecture, controller
automation, server, gameplay replication, or avatar bridges.

## Verified Evidence

| Claim | Evidence |
| --- | --- |
| The deployed source and package are commit `67f20fd936ae6a20f24261579d59a8812afc439f`. | `[verified: git, package manifest, clean WinBoat build]` |
| Windows `TPTests` passed 253 assertions in 12 cases and the package/static audits passed. | `[verified: clean WinBoat build log and Linux audits]` |
| A fresh automated New Game completed the real RaceSex name/confirm transaction, accepted the allowlisted Realm intro, and reached a finalized loaded player with only `HUD Menu` open. | `[verified: DevBench REST and automation output, 2026-07-15]` |
| The client did not crash, used activation/owner thread 324, and at draw 6000 had produced and consumed 938 permits. | `[verified: fresh `tp_client.log`]` |
| Lifecycle remained `state=suspended`, `reason=main_menu`, with no connection command emitted until the deterministic driver timed out. | `[verified: fresh lifecycle file and driver result]` |
| `VRLifecycleService` obtains `main_menu` only from `UI::GetMenuOpen("Main Menu")`. | `[verified: `Code/client/Services/Generic/VRLifecycleService.cpp`]` |
| The local `UI::GetMenuOpen` always calls address ID `82074`; the generated VR map resolves it directly to RVA `0xF916C0`. | `[verified: `UI.cpp`, generated override CSV, address audit]` |
| CommonLibSSE-NG does not call a relocated function for `UI::IsMenuOpen`; it looks up `menuMap` and requires a non-null menu whose `kOnStack` flag is set. | `[verified: sibling CommonLib NG `src/RE/U/UI.cpp`]` |
| The port already routes VR `UI::menuMap` to CommonLib's verified offset `0x128` and VR `IMenu::menuFlags` to verified offset `0x1C`; `kOnStack` is bit `0x40`. | `[verified: local `UI.h`, `IMenu.h`, `RuntimeLayout`, CommonLib NG headers, existing layout audits]` |
| DevBench, built on CommonLib NG, reports `Main Menu` absent in the same process where the client predicate reports it present. | `[verified: same-process live REST and lifecycle file]` |

## Environment Facts

| Item | Value |
| --- | --- |
| Runtime | Skyrim VR 1.4.15.0 |
| Client mode | default connection-only, active owner |
| Native owner | exact `Main::Draw` ID 35560 / RVA `0x5B9330` |
| UI singleton | exact CommonLib VR ID 514178 / RVA `0x1F83200` |
| Suspect menu function | ID 82074 / generated direct VR RVA `0xF916C0` |
| CommonLib reference | sibling `devbench/lib/commonlibsse-ng` |
| Runtime outcome | loaded Realm player, no crash, lifecycle false-positive, no connect |

## Current Lean And Decisions

1. **VR menu-open implementation.** Lean: under `TP_SKYRIM_VR`, implement the
   CommonLib NG predicate locally: find the named `UIMenuEntry` in the verified
   menu map, require a non-null `spMenu`, and read `kOnStack` through
   `GetMenuFlagsData()`. Keep the relocated desktop call unchanged for non-VR.
   Rejected: assigning a new VR function address without disassembly evidence.
2. **Memory safety.** Lean: fail closed when the UI/menu-map storage, entry, or
   menu object cannot be safely read, rather than returning "menu absent".
   Decide the least invasive representation (`optional<bool>`, explicit probe
   enum, or caller-side status) that avoids treating unreadable state as ready.
3. **Concurrency.** Lean: the predicate runs only from the established
   `Main::Draw` owner after the original draw returns, matching the engine/UI
   thread. Confirm whether iterating the menu map is acceptable or whether the
   existing map `find` implementation is materially safer.
4. **Coverage.** Lean: static audits must reject ID 82074 on the VR path and
   require local `menuMap` plus `kOnStack` semantics; add focused unit coverage
   only where the data structure can be tested without fabricating an invalid
   game object.

Potential overlap: decisions 1 and 2 may be one API-design decision. Merging
them is in scope.

## Reviewer Request

Verify the cited source and runtime evidence first. Then provide a prioritized
review with blockers, the exact safe predicate/API shape, ABI/layout concerns,
and the minimum credible regression coverage. Call out any reason the proposed
local CommonLib-compatible predicate could still misclassify or crash. This is
read-only review; do not edit files.
