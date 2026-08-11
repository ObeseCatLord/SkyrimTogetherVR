# HIGGS Compatibility

HIGGS must be treated as an owner of several VR-specific gameplay systems, not as a passive plugin.

## HIGGS-Owned Systems

The HIGGS public API exposes callbacks and controls for:

- object pull, grab, drop, stash, and consume events
- hand collision and grabbed-object collision
- two-handing state transitions
- weapon collision enable/disable state
- hand rigid bodies, weapon rigid bodies, and grabbed rigid bodies
- grabbed reference and grabbed node state
- finger curl values
- pre-physics callbacks
- pre/post VRIK and pre/post HIGGS callback phases
- grab transform state
- numeric HIGGS settings

SkyrimTogetherVR should not install hooks that override these systems until each hook is validated against Skyrim VR and tested with HIGGS installed.

## Current Guard

The VR launcher/client now has two HIGGS detection paths:

- load-time detection of `higgs_vr.dll` in the DLL load shim
- load-time detection of alternate `higgs.dll` builds in the DLL load shim
- startup detection of `Data/SKSE/Plugins/higgs_vr.dll` or `Data/SKSE/Plugins/higgs.dll`

If HIGGS is installed, the VR target refuses to install the unvalidated SkyrimTogether gameplay hook batch even when `TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=1`. It stays in the conservative bring-up hook set instead.

This is deliberate. The existing Skyrim Together hook batch was written for flat Skyrim SE/AE and includes input, UI, projectile, tasklet, render, and gameplay hooks that have not been proven against Skyrim VR or HIGGS.

## API Use

HIGGS exposes its C++ API through SKSE messaging. A normal SKSE plugin requests the interface after SKSE `PostPostLoad` by dispatching `0xF9279A57` to plugin name `HIGGS`; HIGGS returns revision `1` of `IHiggsInterface001`.

The mapped Skyrim Together client is not a normal SKSE plugin, so
`SkyrimTogetherVRHiggsBridge` owns HIGGS messaging and callbacks. It listens for
SKSE `PostLoad`/`PostPostLoad`, requests revision 1, captures only from HIGGS's
post-VRIK/post-HIGGS phase, and writes a compact handoff consumed by the mapped
client.

- keep the small companion SKSEVR plugin as the HIGGS interface owner and forward compact state/events to the SkyrimTogetherVR client
- restructure the client-side SKSE bridge so it can safely participate in SKSE messaging phases

Directly copying HIGGS headers into the current client without a proper SKSE message-phase owner is not enough.

Reference headers inspected:

- `_refs/higgs/include/higgsinterface001.h`
- `_refs/higgs/src/higgsinterface001.cpp`
- `_refs/higgs/include/pluginapi.h`
- `_refs/SkyrimVRTools/src/main.cpp`
- `_refs/VRCustomQuickslots/src/main.cpp`

`Tools/SkyrimVR/audit_higgs_bridge.py` compares the bridge's inlined `IHiggsInterface001` virtual method order against `_refs/higgs/include/higgsinterface001.h`. This is the main ABI guard for the standalone bridge because the bridge must not drift from HIGGS's vtable layout while still avoiding a direct dependency on HIGGS headers in the packaged plugin.

## Bridge Plan

The bridge boundary is callback-owned and non-mutating:

- request `IHiggsInterface001` from `SkyrimTogetherVRHiggsBridge` after SKSE `PostLoad` or `PostPostLoad`
- register pulled, grabbed, dropped, stashed, consumed, collision, and two-handing callbacks
- register a `PostVrikPostHiggs` callback and read `GetGrabbedObject`, `IsHoldingObject`, `IsTwoHanding`, `GetFingerValues`, and `GetGrabTransform` from that HIGGS-owned update phase
- do not query live HIGGS state while registering callbacks during SKSE `PostLoad`/`PostPostLoad`; the first snapshot must come from `PostVrikPostHiggs`
- cache the HIGGS snapshot under bridge mutexes and rate-limit handoff writes
  from the same game-thread callback; no background writer or unload-time join
  exists
- forward compact events/state through `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgs` for logging and remote visualization
- do not call mutating HIGGS functions

The HIGGS post-VRIK/post-HIGGS callback is not used as a generic Skyrim
Together update driver. It is invoked inside HIGGS's player-update work, where
draining `World::Update()` could stall or reenter HIGGS/PLANCK-sensitive game
state. Connection-only transport ticking uses the independent
`SkyrimTogetherVRTickBridge` SKSEVR task path, keeping the HIGGS bridge as an
observation-only owner of HIGGS API callbacks.

`SkyrimTogetherVR.higgs` includes `bridge.loaded`, `bridge.sequence`, `bridge.epoch`, `higgs.detected`, `higgs.interfaceAvailable`, `higgs.callbacksRegistered`, `higgs.snapshotAvailable`, `higgs.snapshotSequence`, `higgs.twoHanding`, left/right hand state, finger values, optional grab transforms, and a bounded `recentEvent.*` list. The companion helper reads it as the `higgs` readout and the browser panel shows a compact HIGGS status table. The main client only relays coherent fresh bridge snapshots; stale or partial files leave `SkyrimTogetherVR.higgsnet` not ready.

`GetGrabbedNodeName` is intentionally not polled in this first bridge pass because `BSFixedString` lifetime handling needs to be mirrored precisely before calling a by-value string-returning HIGGS API from the standalone bridge.

The network path replicates intent/state, not HIGGS-owned local physics:

- `VRHiggsService` reads `SkyrimTogetherVR.higgs`, sends `RequestVRHiggsState` snapshots when connected, receives `NotifyVRHiggsState` snapshots by server player id, and writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgsnet`
- `VRHiggsRelayService` stamps the authenticated server player id and relays a
  bounded ordered mutation batch independently of throttled observation state;
  its mutation ledger advances only after successful fanout
- transport is at least once; the receiver serializes mutations per player and
  advances its independent deduplication ledger only after bridge admission
- authority changes are staged in wire order and published only after fanout;
  conflicting edges receive bounded skipped terminals instead of blocking later
  events, and direct VRGrab edges are never discarded by a rate throttle
- offline/disconnect processing establishes a replay floor so retained handoff
  events from an earlier connection are not emitted into a new session
- the receiver translates pull/grab/drop to bounded engine motion-state updates;
  stash/consume durable mutation remains owned by canonical inventory traffic
- keep local HIGGS physics authoritative on the local machine
- do not call mutating HIGGS functions from the mapped client or remote bridge

Direct physical remote-hand attachment remains outside the public API:

- add object ownership, conflict resolution, and latency handling
- validate references through VR-safe `TESObjectREFR`/`Actor` layouts
- only then consider mutating calls such as `GrabObject`, `SetGrabTransform`, `DisableHand`, `EnableHand`, `DisableWeaponCollision`, `ForceWeaponCollisionEnabled`, or `SetHiggsLayerBitfield`

The current pose, movement, equipment, activation, magic, combat, projectile,
grab/release, and HIGGS paths do not modify HIGGS-owned hand state. HIGGS is an
explicit prerequisite for live full-body/VRIK/FBT/finger capture; base HMD/hand
pose and connection still work without it.

`VRGrabService` remains a narrow game-event bridge point. It observes Bethesda `TESGrabReleaseEvent` through the CommonLib `ScriptEventSourceHolder` offset and writes `SkyrimTogetherVR.grab`, but it does not request `IHiggsInterface001`, does not call HIGGS mutating APIs, and does not replay remote grabs or drops. `SkyrimTogetherVRHiggsBridge` covers the HIGGS API callback/state side without changing that no-mutation boundary.

PLANCK is layered on top of HIGGS and owns active-ragdoll collision, weapon-hit, aggression, and physical interaction behavior. The VR guard also detects `activeragdoll.dll`; if PLANCK is installed, SkyrimTogetherVR keeps the same conservative hook policy used for HIGGS and does not call PLANCK or HIGGS mutating APIs. `SkyrimTogetherVR.compatibility` records the installed/loaded HIGGS and PLANCK state, hook mode, unvalidated-hook suppression state, and observation-only policy markers for the companion panel, Papyrus telemetry, and runtime evidence checklist. `SkyrimTogetherVRPlanckBridge` separately writes `SkyrimTogetherVR.planck` with PLANCK API request/interface/build availability while leaving current-hit polling, `GetLastHitData()`, and all mutating PLANCK calls disabled. PLANCK-specific details are tracked in `Docs/SkyrimVR/planck-compatibility.md`.

## Networking Policy

Until HIGGS-specific networking is designed, SkyrimTogetherVR should:

- read and relay local HMD/hand node transforms without modifying HIGGS hand state
- relay activation telemetry without forcing object activation, pickup, grab, drop, collision, or physics state
- relay magic-effect telemetry without forcing spell casts, effect application, hand state, collision, or physics state
- relay combat-hit telemetry without forcing projectile launch, weapon collision, combat target, actor, or physics state
- relay projectile intent telemetry without forcing projectile creation, weapon collision, hand state, actor, or physics state
- relay grab/release telemetry without forcing HIGGS grabs, drops, hand state, collision, or physics state
- relay HIGGS bridge state through `VRHiggsService`/`SkyrimTogetherVR.higgsnet` without forcing HIGGS grabs, drops, hand state, collision, settings, or physics state
- avoid forcing grabs, drops, stashes, consumes, weapon collision, or hand disable state
- avoid modifying HIGGS collision layers or physics callbacks
- avoid installing projectile/activation hooks that conflict with HIGGS pick/grab behavior
- replicate high-level player pose and animation state first, then add HIGGS-specific object interactions behind a tested bridge

SkyrimVRTools may still be used later if it solves a specific Skyrim VR helper problem, but it is not required for this HIGGS guard.
