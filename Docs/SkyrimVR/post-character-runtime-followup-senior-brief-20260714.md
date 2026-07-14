# Senior Review Brief: Post-Character Runtime Follow-up

## Goal and scope

Choose the minimum correct fixes that let the default connection-only Skyrim
Together VR client survive New Game, recognize the live Skyrim VR player, consume
one connection command, and join the server. This is a solo-operator port. Review
the native player-singleton relocation, Papyrus cadence lifecycle, and automation
acceptance gates only. Do not broaden into gameplay hook enablement.

## Verified evidence

| Claim | Evidence |
| --- | --- |
| The ABI-v2 SKSE task bridge is healthy before New Game. | `[verified: live logs]` `Data/SKSE/Plugins/SkyrimTogetherVRTickBridge.log` records successful monotonic task dispatch on SKSE executor threads. |
| Cadence stops during the New Game/character transition. | `[verified: live timestamps]` the last normal task completes at 10:37:22.541 immediately before renderer initialization; a command written at 10:39 remains unconsumed until a one-shot DevBench call to native `Tick`. |
| A one-shot post-transition tick reaches `VRConnectionService`. | `[verified: live log]` the DevBench global native `Tick` call logs `connection handoff queued until local player and parent cell are available` and writes `state=waiting_for_player`. |
| The game player is valid from CommonLib NG. | `[verified: DevBench/CommonLib NG]` `inspect player` returns named Nord player form `0x14`; `inspect scene` returns `RealmLorkhan` and a valid cell. |
| The mapped client's player read is invalid. | `[verified: client status]` discovery/player-cell files remain `ready=0`, and `TryGetReadablePlayerForVR()` reports no readable singleton. |
| The client uses address ID `401069` for `PlayerCharacter::Get`. | `[verified: source]` `Code/client/Games/Skyrim/PlayerCharacter.cpp:45`. |
| The generated VR override maps that ID to RVA `0x1c3cbd8`. | `[verified: staged CSV/audit]` `SkyrimTogetherVR_AddressOverrides.csv` and `address-audit.json`. |
| CommonLib NG uses SE ID `517014` for `PlayerCharacter::Singleton`. | `[verified: local CommonLib NG source]` `devbench/lib/commonlibsse-ng/src/RE/P/PlayerCharacter.cpp`. |
| The installed Skyrim VR Address Library resolves `517014` to RVA `0x2feb9f0`. | `[verified: installed CSV]` `Data/SKSE/Plugins/version-1-4-15-0.csv`. |
| The current quest cadence arms in `OnInit` and player `OnPlayerLoadGame` only. | `[verified: PSC]` `SkyrimTogetherVRMigrationXScript.psc` and both player alias scripts. |
| Skyrim VR/XRizer cannot present the SteamVR naming keyboard. | `[verified: live UI]` real controller activation dismisses the finish dialog and places the player in Realm of Lorkhan, but RaceSex remains open. |

## Environment facts

| Item | Value |
| --- | --- |
| Runtime | Skyrim VR 1.4.15, SKSEVR 2.0.12, Proton, Envision-managed Monado |
| Default safety mode | Connection-only; gameplay sync hooks disabled |
| Native build | Windows/WinBoat scripted build available |
| Address source | VR Address Library CSV plus generated STVR override/alias files |
| Runtime proof required | task dispatch, local online/nonzero player ID, remote server admission |

## Open decisions and current lean

1. **Player singleton relocation.** Lean: use `517014` under
   `TP_SKYRIM_VR` and retain `401069` for non-VR. Reject translating the old
   `401069` RVA because CommonLib NG and the installed VR database provide a
   direct authoritative singleton entry.
2. **Cadence re-arm.** Lean: add player-alias `OnLocationChange` to invoke the
   owning quest's `VerifyLaunch`, so New Game and later world transitions re-arm
   the single-update timer. `ClaimCadence` preserves one owner. Reject a native
   background producer because SKSE task execution remains the authority and the
   current design intentionally keeps Papyrus as cadence producer.
3. **RaceSex automation.** Lean: require the real controller finish action,
   then require a named/raced player in `RealmLorkhan`; only after those checks may
   DevBench close the stranded RaceSex presentation. Reject treating a raw menu
   hide as character-finalization proof.
4. **Address audit safety rail.** Lean: add a targeted test/audit assertion that
   VR `PlayerCharacter::Get` uses `517014`, preventing generated translation data
   from silently restoring the wrong RVA.

The first two decisions overlap at the readiness gate: cadence can be healthy
while every connection remains permanently queued if the singleton relocation is
wrong. Review them as one end-to-end post-character contract where useful.

## Requested review

Verify the cited files and data first. Return a prioritized critique, the minimum
patch architecture, and exact verification gates. Specifically challenge whether
`OnLocationChange` is reliable for a new game and whether the direct VR ID needs
special pointer semantics. Keep the response under 1,200 words. Do not edit files.
