# PLANCK Compatibility

PLANCK is an active VR physics owner, not a passive animation or UI plugin.

## Inspected Version

The PLANCK source audit validates the pinned 0.8.0 API shape from `_refs/activeragdoll` every time. Validation of the downloaded PLANCK package zip is configurable so Windows builds do not depend on a maintainer-specific download folder:

```sh
python3 Tools/SkyrimVR/audit_planck_compat.py --planck-archive "C:\Downloads\PLANCK.zip" --require-planck-archive
STVR_PLANCK_ARCHIVE="/path/to/PLANCK.zip" python3 Tools/SkyrimVR/audit_planck_compat.py --require-planck-archive
```

When a PLANCK archive is supplied, the audit expects the pinned 0.8.0 package to contain:

- `SKSE/Plugins/activeragdoll.dll`
- `SKSE/Plugins/activeragdoll.ini`
- `Scripts/planck.pex`
- `Source/Scripts/planck.psc`

The upstream source reference is `_refs/activeragdoll`, tag `v0.8.0`.

## Public API Shape

PLANCK exposes `IPlanckInterface001` through SKSE messaging after `PostPostLoad`.
The request message is `0x92F38745`, dispatched to receiver `PLANCK`.

The pinned 0.8.0 virtual method order is:

```text
GetBuildNumber
Deprecated1
Deprecated2
AddIgnoredActor
RemoveIgnoredActor
AddAggressionIgnoredActor
RemoveAggressionIgnoredActor
SetAggressionLowTopic
SetAggressionHighTopic
AddRagdollCollisionIgnoredActor
RemoveRagdollCollisionIgnoredActor
GetLastHitData
GetCurrentHitEvent
GetSettingDouble
SetSettingDouble
```

`Tools/SkyrimVR/audit_planck_compat.py` compares this pinned order against `_refs/activeragdoll/include/planckinterface001.h`.

## HIGGS Coupling

PLANCK requires HIGGS. At `PostPostLoad`, PLANCK requests `IHiggsInterface001`, requires HIGGS build `1060000` or newer, registers HIGGS collision/physics/VRIK/grab callbacks, expands HIGGS collision layers, and forces weapon collision enabled for both hands.

That means SkyrimTogetherVR must not replay remote physics, mutate HIGGS collision settings, or call PLANCK actor ignore/settings APIs during the first VR port stage.

## Current Guard

SkyrimTogetherVR now treats PLANCK the same way it treats HIGGS for hook safety:

- detects loaded `activeragdoll.dll` in the DLL load shim
- detects installed `Data/SKSE/Plugins/activeragdoll.dll` at startup
- refuses to install the unvalidated flat-Skyrim gameplay hook batch when HIGGS or PLANCK is installed
- logs when PLANCK is loaded
- writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.compatibility` with `planck.installed`, `planck.loaded`, `vrPhysicsCompatibilityModInstalled`, `hookMode`, `unvalidatedGameplayHooksSuppressed`, and `planckPolicy=observation_only`
- exposes the same compatibility state in the desktop companion panel, `vr_handoff.py status`, runtime evidence checklist, and the in-game Papyrus telemetry message

The current PLANCK policy is observation-only. The main client does not request `IPlanckInterface001`, does not call `AddIgnoredActor`, `RemoveIgnoredActor`, `SetSettingDouble`, `GetLastHitData`, or `GetCurrentHitEvent`, and does not replay PLANCK physical hits.

`SkyrimTogetherVRPlanckBridge` is a separate SKSEVR plugin for the narrow API heartbeat. It requests `IPlanckInterface001` with message `0x92F38745` at SKSE `PostPostLoad`, writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.planck`, and exposes:

- `bridge.loaded`
- `bridge.sequence`
- `bridge.epoch`
- `planck.detected`
- `planck.interfaceRequestAttempted`
- `planck.interfaceAvailable`
- `planck.buildNumber`
- `planck.currentHitEventAddress=0`
- `planck.currentHitEventAvailable=0`
- `planck.currentHitEventObservationOnly=1`
- `planck.lastHitDataAvailable=0`
- `planck.lastHitDataProbeEnabled=0`
- `planck.lastHitDataReason=not_polled_nontrivial_return_boundary`
- `planck.lastHitDataBoundary=disabled_unvalidated_by_value_abi`
- `planck.policy=observation_only`

The bridge captures `GetBuildNumber()` when the interface is acquired on the SKSE messaging thread, then the writer thread serializes cached status only. It does not poll `GetCurrentHitEvent()` or `GetLastHitData()` from the writer thread. `GetLastHitData()` stays disabled because PLANCK 0.8.0 returns a by-value structure containing non-trivial SKSE/CommonLib types such as `NiPointer` and `BSFixedString`; `planck.lastHitDataBoundary=disabled_unvalidated_by_value_abi` makes that disabled ABI boundary visible in runtime evidence. It also does not call PLANCK mutators, actor ignore APIs, aggression APIs, ragdoll-collision ignore APIs, setting setters, HIGGS mutators, or hit replay logic.

The staged `VRCombatService` also records whether a game-emitted `TESHitEvent` matches PLANCK's documented hit-event magic word by reconstructing the 32-bit word at the CommonLib hit-event flags offset. That data is serialized as `VRCombatHitEvent::RawHitFlags` and `VRCombatHitEvent::PlanckHit`, written to `SkyrimTogetherVR.combat` as `rawHitFlags` and `planckHit`, and relayed to other clients as telemetry only. This classification does not request the PLANCK interface and does not read extended `PlanckHitData`.

## Future Work

A later validated PLANCK stage can observe `PlanckHitData` during a real hit-event dispatch and relay compact telemetry. That still needs the by-value return ABI validated, object ownership, local/remote actor authority, duplicate-hit suppression, and HIGGS collision conflict handling before remote physical hit replay is safe.

Before runtime testing a package with PLANCK installed, run:

```sh
python3 Tools/SkyrimVR/install_vr_prereqs.py --skyrim-vr "/path/to/SkyrimVR" --require --enable-plugins --planck-source "/path/to/PLANCK.zip"
python3 Tools/SkyrimVR/audit_planck_compat.py --planck-archive "/path/to/PLANCK.zip" --require-planck-archive
python3 Tools/SkyrimVR/audit_built_package.py --package artifacts/SkyrimTogetherVR/releasedbg --skyrim-vr "/path/to/SkyrimVR" --require-installed-prerequisites --require-higgs --require-planck
```

`install_vr_prereqs.py` also accepts `--vrik-source`, `--higgs-source`, `--planck-source`, `STVR_VRIK_SOURCE`, `STVR_HIGGS_SOURCE`, `STVR_PLANCK_SOURCE`, and `STVR_PLANCK_ARCHIVE` for portable prerequisite installation. `STVR_MO2_MODS`, `STVR_MO2_MOD_DIRS`, `STVR_BACKUP_DOWNLOADS`, `STVR_DOWNLOADS`, and `STVR_NORDIC_MODS` can prepend source-root searches without editing the script.
