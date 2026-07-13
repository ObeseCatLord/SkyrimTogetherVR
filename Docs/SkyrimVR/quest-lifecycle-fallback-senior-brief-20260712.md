# Quest Lifecycle Fallback Review Brief

## Goal

Make the default SkyrimTogetherVR connection-only package reach its existing
Papyrus timer bridge on Skyrim VR 1.4.15 with SKSEVR 2.0.12 under Linux/Proton.
This is a solo-operator port. The decision should prefer the smallest
crash-resistant lifecycle correction, not a new recurring native scheduler.

## Verified Environment Facts

| Claim | Status and evidence |
| --- | --- |
| The game is live in Monado through XRizer. | [verified: `FasterGames/.../SkyrimVR/logs/stvr-launch-20260713T015930Z.log`] The session reaches OpenXR `FOCUSED`; Bigscreen Beyond and both Valve Knuckles controllers use `/interaction_profiles/valve/index_controller`. |
| SKSEVR and all relevant bridge DLLs load. | [verified: current `Documents/My Games/Skyrim VR/SKSE/sksevr.log`] `SkyrimTogetherVRTickBridge.dll`, HIGGS, PLANCK, VRIK, and their bridges load successfully. |
| The remote dedicated server is healthy but has no client. | [verified: `ssh foundry docker logs --tail 60 skyrim-together-vr`] Server listens on UDP 10578 and reports zero players. |
| The live PEX files are the current Windows package PEX files. | [verified: SHA-256] Installed `SkyrimTogetherVRTickBridge.pex`, `SkyrimTogetherVerifyLaunchScript.pex`, and `SkyrimTogetherPlayerAliasScript.pex` exactly match `/tmp/SkyrimTogetherVR-releasedbg-fd9b7b86`. Installed verify PEX strings include `OnInit`, `OnUpdate`, `ArmOnInit`, `Tick`, and `RegisterForSingleUpdate`. |
| The ESP is configured in the actual Proton prefix. | [verified: `FasterGames/.../compatdata/611670/pfx/.../AppData/Local/Skyrim VR/{plugins,loadorder}.txt`] Both files contain `SkyrimTogether.esp`; it is present in the game's `Data` directory. |
| The quest record has the expected scripts and start flags. | [verified: raw record plus local `Code/client/Games/Skyrim/Forms/TESQuest.h`] `SkyrimTogether.esp` contains the QUST VMAD references to `SkyrimTogetherVerifyLaunchScript` and `SkyrimTogetherPlayerAliasScript`. Its DNAM flags are `0x0111`, which maps to `Enabled | StartsEnabled | RunOnce` in the local enum. |
| Native Papyrus registration succeeds. | [verified: `Data/SKSE/Plugins/SkyrimTogetherVRTickBridge.log`] Each launch writes `SKSEVR task and Papyrus bridge registered` followed about 68 seconds later by `Papyrus registration callback completed`. |
| Neither of the existing lifecycle paths executes. | [verified: same durable bridge log after two focused launches] No `papyrus_arm=OnInit`, `papyrus_arm=OnPlayerLoadGame`, `papyrus_tick`, task enqueue/run, or dispatch line exists. |
| The mapped client endpoint itself is ready. | [verified: current `logs/tp_client.log`] It logs `SkyrimTogetherVR tick bridge endpoint ready on thread 336`. |
| No connection work occurs. | [verified: current handoff directory and remote server] The `SkyrimTogetherVR.command` connect request remains unconsumed; no status/readout file is created; the server remains at zero players. |

## Existing Design and Non-Negotiable Guardrails

1. `Code/vr_tick_bridge/main.cpp` owns normal SKSEVR Papyrus registration only.
   `Tick()` queues one `SKSETaskInterface` task; the task validates an endpoint
   and calls the mapped client's connection-only update on its ready thread.
2. `GameFiles/SkyrimVR/scripts/source/SkyrimTogetherVerifyLaunchScript.psc`
   currently uses `OnInit` to arm `RegisterForSingleUpdate(0.05)`. The player
   alias calls the same re-arm helper from `OnPlayerLoadGame`.
3. An earlier Sol-max review rejected a native self-requeue task, VM destructor
   hook, main-loop/renderer/HIGGS callback scheduler, and worker-thread updates.
   Those remain rejected. Do not reopen full gameplay, avatar sync, overlay,
   or physics integration.
4. `Code/vr_tick_bridge` intentionally uses only the pinned legacy SKSEVR SDK.
   The main client's local `TESQuest` wrapper uses untranslated SE offsets and
   must not be reused from the standalone SKSEVR plugin.

## Failure Interpretation

- [verified] The correct script bytecode is present and its native functions
  are registered.
- [verified] The existing quest/alias does not invoke either arm native during
  two focused launches.
- [verified] The QUST has `DNAM\\Flags` Start Game Enabled set, but the staged
  package had no `Seq/SkyrimTogether.seq`. xEdit treats this asset as required
  for a Start Game Enabled quest and uses it to identify the quest before save
  state exists. This omission explains why the valid `OnInit` path never ran.
- [verified] `OnPlayerLoadGame` is a player-actor event. The current player
  `ReferenceAlias` is the correct later-load fallback; a quest script must not
  be changed to receive that event.

## Open Decisions

### 1. Smallest lifecycle fallback

**Adopted: generate and stage `Seq/SkyrimTogether.seq` from the ESP, retaining
the existing quest `OnInit` timer and player-alias `OnPlayerLoadGame` fallback.**
This restores the intended lifecycle without a native scheduler or a new ESP
record.

Alternatives:

- A. Direct quest `OnPlayerLoadGame` re-arm. Rejected: Papyrus exposes this
  event to the player actor, not a quest script.
- B. Add a SKSE messaging listener that reacts at one lifecycle phase and
  forces the quest to start. Rejected unless A is not valid: the standalone
  plugin lacks a safe VR-native quest API, and using the main client's SE
  `TESQuest` shim would violate the runtime boundary. A listener that only logs
  cannot start the cadence.
- C. Edit ESP flags or add a second startup quest. Rejected: the current QUST
  already carries `Enabled | StartsEnabled | RunOnce`, and a second record
  broadens the save/ESP surface without addressing delivery proof.
- D. Restore a recurring native hook/task. Rejected: prior review and runtime
  crash history make this a higher-risk scheduler, not a lifecycle correction.

### 2. Evidence before the next Windows rebuild

**Adopted: enable Papyrus engine logging, stage the generated SEQ asset, and
restart into a new game or a save from before this mod was enabled.** This
requires no DLL or Papyrus rebuild. A later save load uses the existing alias
fallback.

Rejected alternative: add only speculative native diagnostic code. The durable
bridge log already proves the native entry was never reached.

### 3. Controller-binding packaging

**Current state:** a local FUS `VRIK Default Controller Bindings` loose
`Data/Interface/Controls/PC/controlmapvr.txt` was installed for the active
test game. It is not part of the final Windows package yet. It is independent
of the bridge failure and must not determine the lifecycle design.

## Review Request

Verify the cited source/runtime facts first. Then provide a prioritized
decision, not an implementation patch:

1. Is direct `Quest.OnPlayerLoadGame` a valid and sufficient lifecycle fallback
   for Skyrim VR Papyrus, or is another existing Quest event safer?
2. Is enabling Papyrus logging before source edits the right next experiment?
3. If the direct quest event is not viable, specify the smallest safe fallback
   consistent with the guardrails. Do not propose a recurring native scheduler.
4. List exact acceptance breadcrumbs required before the one Windows rebuild
   and before declaring server connection verified.

Depth budget: under 1,200 words. Do not re-review networking, full gameplay,
avatar synchronization, HIGGS/PLANCK behavior, controller layout preference,
or the broader build system. No edits.
