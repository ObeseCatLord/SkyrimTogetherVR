# SkyrimVR-FBT Compatibility

SkyrimTogetherVR does not call SkyrimVR-FBT APIs and does not patch its code.
The integration reads the final local player skeleton after the HIGGS
post-VRIK/post-HIGGS phase, then sends a validated body-pose lane. This keeps
FBT, VRIK, HIGGS, and PLANCK ownership separate.

## Captured Body Data

Body format 3 contains pelvis local translation/rotation; spine0/1/2, neck,
clavicle, upper-arm, forearm, thigh, calf, and foot local rotations; sparse
rotations for 30 named finger joints; a capture sequence; and a skeleton-root
generation. Non-pelvis translation and scale are not network-authoritative.
Both client and server reject values outside strict bounds. Capture older than
250 ms becomes an explicit invalid body sample.

The supplied FBT source replaces VRIK's small `updateBody` wrapper, calls the
real VRIK body update, and then runs its FBT body pass. Some lower-spine output
is world-only, so capture derives effective parent-local spine rotations from
the final world transforms. HIGGS invokes the registered post-VRIK/post-HIGGS
callback after that wrapper returns, so capture observes the final FBT result
when all three plugins are loaded normally.
Runtime evidence must still prove `bodyCapture.successCount` increases and
`local.body.valid=1` before treating that ordering as validated on a given
installation.

## Remote Boundary

Remote body data is relayed, validated again, and applied only to CommonLib
managed remote actors. The gameplay bridge writes parent-local body rotations
parent-first, propagates the skeleton, then derives head/hand locals from their
world targets and applies sparse finger rotations. It preserves remote actor
limb translations/scales and suppresses all pose writes during PLANCK/engine
ragdoll state. Failed endpoint preparation rolls the body transaction back.

HIGGS is the explicit prerequisite for live body, FBT, named-finger, and VRIK
API snapshot capture because its public post-VRIK/post-HIGGS callback is the
only validated final-skeleton phase. Without HIGGS, base HMD/hand capture and
connection remain available, while the full-body lane fails closed as stale or
invalid instead of using an unverified animation hook.

## FBT Installation

SkyrimVR-FBT is an optional third-party prerequisite and is not redistributed
by this repository. Its supplied source archive contains no license, so only
the behavioral and ABI observations needed for interoperability are recorded.

FBT patches exact VRIK RVAs and must fail closed when VRIK changes. For the
currently installed VRIK build with PE timestamp `0x69277C5C`, the verified
local profile is:

```ini
[VrikHook]
VrikTimestamp = 0x69277C5C
RvaUpdateBodyWrapper = 0x48B0
RvaVrikBodyUpdate = 0xE3B0
RvaVrikBodyInstance = 0x10D298
RvaUpdateWalk = 0x180D0
AllowTimestampMismatch = 0
```

Do not set `AllowTimestampMismatch=1` to work around a VRIK update. Derive and
verify a new profile from that exact VRIK DLL/PDB, including the wrapper bytes,
before enabling the hook.

## Runtime Evidence

- `SkyrimTogetherVR.compatibility`: `fbt.installed`, `fbt.loaded`, and `fbtPolicy`
- `SkyrimTogetherVR.higgs`: endpoint fault state, capture attempts, successes, and last callback result
- `SkyrimTogetherVR.pose`: local body version, validity, sequence, generation,
  joint mask, and pelvis/leg/spine/neck/clavicle/arm nodes
- `SkyrimTogetherVR.avatar`: validated remote body availability and sequence

The desktop companion mirrors these fields in its compatibility, HIGGS, pose,
and per-player summaries. A healthy capture path has a non-faulted endpoint,
increasing attempt and success counters, `local.body.valid=yes`, and increasing
body sequence values. A successful callback can still publish an invalid body
sample while the player skeleton is unavailable, so success count alone is not
proof that FBT data is being captured.

An unavailable or stale FBT/body lane does not block connection or base
head/hand pose relay. It fails closed as invalid body data.
