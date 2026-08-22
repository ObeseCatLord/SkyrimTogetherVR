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

## API Shape

Stock PLANCK exposes `IPlanckInterface001` through SKSE messaging after `PostPostLoad`.
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

`Tools/SkyrimVR/audit_planck_compat.py` compares this pinned order against
`Libraries/activeragdoll/include/planckinterface001.h`. The project fork keeps
that ABI intact and adds `IPlanckInterface002`. Revision 2 is a bounded,
fixed-size, trivially-copyable contract for:

- capability discovery
- local hit, ragdoll-enter/exit, and actor-grip event dequeue
- remote hit impulse and ragdoll-enter/exit submission
- bounded impulse-driven remote actor-grip begin/update/end
- remote-session cleanup

Revision 2 never exposes PLANCK, CommonLib, Havok, or HIGGS object pointers
across the plugin boundary. Every request carries sizes, session/event identity,
finite bounded values, and fixed-capacity node text.

## HIGGS Coupling

PLANCK requires HIGGS. At `PostPostLoad`, PLANCK requests `IHiggsInterface001`, requires HIGGS build `1060000` or newer, registers HIGGS collision/physics/VRIK/grab callbacks, expands HIGGS collision layers, and forces weapon collision enabled for both hands.

Ordinary object grabs remain owned by HIGGS. The revision-2 actor-grip path
drives PLANCK bodies with bounded impulses and does not create a receiver-local
HIGGS hand or proxy constraint.

## Current Integration

SkyrimTogetherVR now treats PLANCK the same way it treats HIGGS for hook safety:

- detects loaded `activeragdoll.dll` in the DLL load shim
- detects installed `Data/SKSE/Plugins/activeragdoll.dll` at startup
- refuses to install the unvalidated flat-Skyrim gameplay hook batch when HIGGS or PLANCK is installed
- logs when PLANCK is loaded
- writes static installed/loaded compatibility state while operational
  readiness remains owned by `SkyrimTogetherVR.gameplay`
- exposes the same compatibility state in the desktop companion panel, `vr_handoff.py status`, runtime evidence checklist, and the in-game Papyrus telemetry message

`SkyrimTogetherVRPlanckBridge` is the SKSEVR adapter. At SKSE `PostPostLoad`
it requests interface revision 2, requires the complete feature mask, and
publishes the interface only after all four bounded bridge exports are present.
The client advertises protocol capability `PlanckPhysicsInterface002` only when
that local contract is operational. It writes:

- `bridge.loaded`
- `bridge.sequence`
- `bridge.epoch`
- `planck.detected`
- `planck.interfaceRequestAttempted`
- `planck.interfaceAvailable`
- `planck.interfaceRequestCount`
- `planck.interfaceRevision`
- `planck.features`
- `planck.interface002RequiredFeatures`
- `planck.damageAuthority=none_remote_physics_only`
- `planck.remotePhysicsReplay=data_only_interface002`

The bridge obtains the API on the required SKSE messaging phase and uses the
existing game-thread pump for rate-limited handoff telemetry. There is no
background writer or unload-time join. Stock revision-1 transient hit pointers
and non-trivial by-value `PlanckHitData` are not used.

Protocol revision 20 introduced PLANCK events between peers that negotiated
interface 002. Current protocol revision 21 retains that lane and adds
owner/revision-authenticated ordinary quest updates plus owner-scoped canonical
quest recovery. The server validates sender identity, target range, PVP policy,
event order, and grip leases. Receivers use bounded lifecycle-scoped retry and
session cleanup. Canonical health/effect messages remain the sole damage
authority, so physical hit replay cannot apply a second damage mutation.

## Build Dependency

PLANCK requires the Havok 2010.2 SDK and official SKSEVR 2.0.12 source layout.
The SDK is caller-supplied and must remain outside the repository and handoff.
Use `BuildCompleteSkyrimTogetherVR-Windows.ps1` to build the patched PLANCK DLL,
then the matching protocol-21 gameplay client/server package. The strict package
audit binds `Data/SKSE/Plugins/activeragdoll.dll` to the interface-002 manifest
marker and SHA-256. A stock PLANCK DLL does not provide this interface and must
not be used for PLANCK networking.

## Remaining Proof

The current source is not yet built or runtime-proven. Two clients must prove
hit impulse, ragdoll entry and natural recovery, actor grip begin/update/end,
disconnect cleanup, non-PLANCK fallback, canonical damage deduplication, and
coexistence with HIGGS/VRIK under latency before PLANCK parity is checked off.

Build and audit the complete package on Windows with:

```powershell
.\BuildCompleteSkyrimTogetherVR-Windows.ps1 `
  -HavokArchive "D:\archives\hk2010_2_0_r1.7z" `
  -DependencyRoot "D:\stvr-planck-deps" `
  -Configuration Release
```

Then require the patched artifact explicitly:

```sh
python3 Tools/SkyrimVR/audit_built_package.py \
  --package artifacts/SkyrimTogetherVR/releasedbg \
  --gameplay --require-patched-planck-interface002
```
