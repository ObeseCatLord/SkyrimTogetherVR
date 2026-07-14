# Skyrim Together VR post-character first-tick crash senior brief

## Goal and scope

Review the complete connection-only runtime path that becomes reachable after a
Skyrim VR player exists. The immediate goal is to eliminate relocation,
indirection, and runtime-layout failures from character finalization through
server admission in one bounded pass. This is a solo port bring-up; avoid broad
gameplay-sync redesign and enterprise process.

## Verified evidence

| Claim | Evidence |
| --- | --- |
| The rebuilt package passed its Windows package/build-evidence audits. | `[verified: Windows build and local audit]` `SkyrimTogetherVR-build-evidence-default-20260714-160651Z.zip` reports 492 package files, zero warnings, and zero failures. |
| The installed launcher is the rebuilt artifact. | `[verified: sha256]` package and installed `SkyrimTogetherVR.exe` both hash to `2162ebeb09dfc67143f0602bdb6779dba9b8f8fec45553407fec746e66541ce5`. |
| The new client reaches the first accepted SKSE task callback. | `[verified: runtime log]` `tp_client.log` records callback sequence 1 on thread 612. |
| That callback crashes at client VA `0x1806852ec`. | `[verified: runtime log and minidump timestamp]` exception `c0000005`; `crash_UTC_2026-07-14_16-11-53.dmp`. |
| The faulting instruction is `mov 0xbc(%rsi), %r8d` in `DiscoveryService::VisitExteriorCell(bool)`. | `[verified: llvm-symbolizer and llvm-objdump]` client RVA `0x6852ec`. |
| `rsi` is the result of `TES::Get()`. | `[verified: disassembly]` call at `0x1806852c2` symbolizes to `TES::Get`; returned `rax` is moved to `rsi` and checked non-null before the fault. |
| `TES::Get()` currently uses `POINTER_SKYRIMSE(TES*, tes, 400441)` and one pointer load. | `[verified: source]` `Code/client/Games/TES.cpp`. |
| CommonLib NG uses `REL::Relocation<TES**>{RELOCATION_ID(516923, 403450)}` and one pointer load. | `[verified: local CommonLib NG source]` `devbench/lib/commonlibsse-ng/src/RE/T/TES.cpp`. |
| Installed VR Address Library maps `516923` to RVA `0x2feb6f8`. | `[verified: installed CSV]` `version-1-4-15-0.csv`. |
| `ModManager::Get()` currently uses ID `400269`; this type is the local TESDataHandler shim. | `[verified: source/layout]` `Code/client/Games/ModManager.cpp` and `Games/TES.h`. |
| CommonLib NG uses `TESDataHandler**` at `RELOCATION_ID(514141, 400269)`. | `[verified: local CommonLib NG source]` `src/RE/T/TESDataHandler.cpp`. Installed VR CSV maps `514141` to `0x1f82ad8`. |
| Player singleton now uses direct VR-supported ID `517014`; the previous old-ID mapping no longer blocks player readiness. | `[verified: source and runtime]` `PlayerCharacter.cpp`; the crash progressed through readable player/worldspace into exterior discovery. |
| TES runtime offsets read by discovery are `currentGridX/Y=0xb8/0xbc`, `centerGridX/Y=0xb0/0xb4`. | `[verified: source parity]` local `RuntimeLayout.h` matches CommonLib NG TES layout. |
| Connection-only first-tick services include discovery, player-cell status/network-only service, VRConnectionService, and readout-only pose/mod compatibility services. | `[verified: runtime log and constructors]` startup log enables these and accepts the task callback before crashing. |

## Environment facts

| Item | Value |
| --- | --- |
| Repo | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR` |
| CommonLib NG | sibling `devbench/lib/commonlibsse-ng` |
| Game | Skyrim VR 1.4.15 |
| VR Address Library | installed `Data/SKSE/Plugins/version-1-4-15-0.csv` |
| Fresh package | `/tmp/stvr-default-post-character-20260714` |
| Crash dump | Skyrim VR root `crash_UTC_2026-07-14_16-11-53.dmp` |
| Build platform | Windows MSVC via WinBoat; do not request a build until all source changes are settled |
| Runtime | Linux Proton + XRizer + Envision-managed patched Monado Qwerty controllers |
| Server | one Docker server at `incidentalstoat.xyz:10578`; admission must be confirmed in server logs |

## Open decisions and current lean

1. **TES singleton correction.** Lean: under `TP_SKYRIM_VR`, use direct ID
   `516923`; preserve the existing desktop ID. Keep one pointer load. Rejected:
   overriding legacy `400441`, because CommonLib supplies a direct VR-supported
   relocation and the legacy numeric slot is not the same singleton.
2. **ModManager/TESDataHandler singleton correction.** Lean: under VR use direct
   ID `514141`; preserve desktop `400269`. Rejected: waiting for the next crash,
   because this singleton is called immediately after TES in the same function.
3. **Complete reachable-path audit.** Identify every singleton/global,
   translated function, and direct runtime field reached from the first
   `UpdateEvent` through `DiscoveryService`, `PlayerService`,
   `VRConnectionService`, `TransportService::Connect`, connected event handling,
   and status writes. Classify what executes before versus after server
   admission. Lean: correct all direct CommonLib-supported VR IDs now, and gate
   any unverified nonessential observer rather than allowing a crash.
4. **Safety rail.** Lean: extend source audits/tests to require VR-specific
   singleton IDs and forbid the legacy IDs on this path. Add one concise startup
   diagnostic per corrected singleton if it materially aids runtime proof.
5. **Automation proof.** Lean: retain the unattended DevBench flow, then require
   Main Menu -> New Game -> RaceSex acceptance -> Realm placement -> fresh
   `playercell ready=1` -> local `status online=1` with nonzero ID -> remote
   server admission. Do not count a stale handoff file as success.

The singleton correction and reachable-path audit may overlap; merging them is
in scope. A reviewer should challenge the assumption that all CommonLib
`RELOCATION_ID` first operands are directly usable in this custom `VersionDb`
VR loader and verify pointer indirection independently.

## Requested review output

Verify before critiquing. Return a prioritized review with exact file/line or
symbol evidence, then a concrete disposition-ready table covering:

- every singleton/global on the post-character connection-only path;
- current ID and pointer semantics versus CommonLib NG/VR evidence;
- whether each is reached pre-connect, during connect, or immediately after the
  connected event;
- exact source corrections, guards, and audit/test changes;
- any remaining crash that the proposed TES and data-handler fixes would expose.

Choose the highest-leverage issue for a deeper implementation specification.
Do not edit files. Do not review gameplay relay lanes that are compile-disabled,
CEF overlay design, packaging history, or controller ergonomics except where
they directly affect the unattended proof gate. Depth budget: 1,800 words.
