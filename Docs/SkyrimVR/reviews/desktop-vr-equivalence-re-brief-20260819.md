# Desktop-to-VR Equivalence Reverse-Engineering Review Brief

## Goal and boundary

Identify behavior inherited from desktop Skyrim Together that does not yet have a trustworthy Skyrim VR equivalent, and rank the cases where further static or runtime reverse engineering of `SkyrimVR.exe` would materially improve correctness. This is a read-only source/design review. It must not edit, build, launch, deploy, or package anything.

## Verified environment facts

| Fact | Evidence |
|---|---|
| Port source | Current worktree in this repository; desktop reference is branch `original-skyrim-together` |
| Runtime | Skyrim VR 1.4.15.0, SKSEVR, CommonLibSSE-NG based on alandtse CommonLibVR/NG |
| VR mapping data | Locked runtime CSV in `Dependencies/SkyrimVR.lock.json`; reference mapping in `../_refs/skyrim_vr_address_library/addrlib.csv`; exact-RVA overrides in `GameFiles/SkyrimVR/Data/SKSE/Plugins/SkyrimTogetherVR_AddressOverrides.csv` |
| Existing RE result | Root movement uses exact VR RVAs for generic/Actor SetPosition, MoveTo_Impl, and the root-motion/controller processor; the processor's thread affinity remains unknown |
| Existing replacements | Quest, dialogue, calendar, progression, movement, actions, inventory/equipment, combat/magic/projectiles, VRIK/HIGGS/FBT paths, and server relay code are source-present |
| Runtime proof | Only a prior one-client bootstrap is accepted; current source has not been built or tested |

## Questions to answer

1. Compare desktop hooks, engine calls, event sources, and lifecycle assumptions to their VR implementations. Which desktop behaviors have no equivalent, only a heuristic equivalent, or an ABI/threading assumption that is not proven in VR?
2. For each gap, state whether source-level correction is enough, runtime instrumentation is enough, or SkyrimVR.exe disassembly/decompilation is required.
3. For each RE candidate, identify the desktop symbol/ID/call path, current VR implementation, exact evidence missing, likely VR call graph or subsystem to inspect, and the user-visible failure if wrong.
4. Separate literal multiplayer parity blockers from optional VR enhancements and intentionally unavailable behavior.
5. Audit VR-only integrations (VRIK, HIGGS, PLANCK, FBT) for engine-side behavior that is represented only as metadata or pose state rather than equivalent physical/gameplay effects.

## Known high-risk candidates

- Root-motion/controller processor call graph and thread affinity.
- Physical damage source/target authority and PvP ordering.
- Character controller, Havok, ragdoll, grab, and PLANCK physical replay paths that may bypass SetPosition or typed CommonLib calls.
- Menu/pause, save/load, cell transition, quest/dialogue, and animation event timing differences in VR.
- Any desktop hook removed because its address was unavailable, replaced by polling, or translated through an alias/override.

## Required output

Return a prioritized table with severity, parity domain, desktop behavior, current VR equivalent, confidence, missing evidence, recommended RE target/method, and acceptance test. Cite repository file/line evidence. Include a short `do before build`, `defer until runtime trace`, and `out of scope` disposition. Do not broaden into general code style or repeat already accepted findings unless they change an RE recommendation.
