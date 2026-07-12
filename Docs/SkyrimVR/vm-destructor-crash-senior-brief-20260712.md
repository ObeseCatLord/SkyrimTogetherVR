# Skyrim Together VR VM Hook Crash: Senior Review Brief

## Goal and scope

Decide the safest next implementation step after the current Skyrim VR
bring-up build crashes in `HookVMDestructor` before its main-loop cadence
diagnostic can run. The immediate objective is a stable, connection-only
client that consumes `SkyrimTogetherVR.command` and connects to the existing
server. This is a solo-operator port; no enterprise process is required.

Do not redesign gameplay synchronization, controller support, HIGGS/PLANCK,
or server deployment in this review.

## Verified runtime evidence

| Claim | Evidence |
|---|---|
| The new build starts the mapped Skyrim VR executable and reaches the original Papyrus binder. | `tp_client.log`, 2026-07-12 17:28:12: `VR Papyrus native registration bypassed` then `original binder completed`. |
| Rendering initializes before the crash. | Same log, 17:28:17: `renderer init hook reached` and `renderer init completed`. |
| The active crash enters `HookVMDestructor`. | Same log, 17:28:22: `VM destructor hook reached`, followed in the same millisecond by `0xc0000005` at `0x140709f88`. |
| The address-library-selected destructor entrypoint is `0x140707340`. | Same log at install: `vmDestructor=0x140707340`. |
| The address-library-selected VM update and main-loop targets are `0x1409869d0` and `0x1405f4770`. | Same install log. |
| No `main-loop cadence` record is emitted before the active crash. | `tp_client.log` for the 17:27 launch contains no cadence record. |
| The connection handoff command is present but unconsumed. | `Data/SkyrimTogetherReborn/SkyrimTogetherVR.command` still contains `action=connect`, `endpoint=incidentalstoat.xyz:10578`; no `.status` was written. |
| The dedicated server is healthy but has no player. | `foundry`: container `skyrim-together-vr` is Up; latest console title/log reports 0 players. |
| XRizer and Monado see both controllers. | Fresh launcher log identifies `Valve Knuckles Left`, `Valve Knuckles Right`; both interaction profiles resolve to `valve/index_controller`. This is separate from the game-side button mapping issue. |

The coredump is retained locally as
`/home/obesecatlord/FasterGames/SteamLibrary/steamapps/common/SkyrimVR/crash_UTC_2026-07-12_22-28-22.dmp`.

## Relevant source facts

`Code/client/SkyrimVM64.cpp` inherits three SKSE/flat-Skyrim address-library
IDs without independently validating their Skyrim VR meaning:

| Function family | Flat ID | Current VR result | Current behavior |
|---|---:|---:|---|
| `TMainLoop` | 36564 | `0x1405f4770` | Logs cadence, forwards only. |
| `TVMUpdate` | 53926 | `0x1409869d0` | Would inspect `VMContext::inactive`, tick `World::Update`, capture pose, then forward. It has never been observed in this failing launch. |
| `TVMDestructor` | 40412 | `0x140707340` | Logs once then forwards. It is observed and immediately faults. |

The implementation is in `Code/client/SkyrimVM64.cpp:25-157`. The function
types and `VMContext` layout originate in flat Skyrim Together history (the
last pre-port implementation); no VR reverse-engineering evidence establishes
their semantic or ABI equivalence. The VR port added logs but still installs
all three hooks unconditionally in `InstallMainLoopHooks()`.

The latest source commit is `b3a29b46848bcb82bc5167588ca70092d1d8c517`
(`diag(vr): measure main-loop hook cadence`). The deployed corresponding
gameplay wrapper is `SkyrimTogetherVRGameplay.exe`, SHA-256
`3f5f0a1022441a408463aed665ec53cb91b0d75c3e2c96b849c4644a367d1f38`.

## Prior history and constraints

- An earlier delayed crash at `0x140c42410` was traced to custom Papyrus
  `NativeFunction` registration. The VR build now calls the original binder
  and returns before custom registrations. This fixed that crash and must be
  retained.
- Earlier senior guidance advised against invoking `World::Update` from an
  unvalidated callback. It recommended first measuring main-loop cadence.
- User requests no further rebuilds for speculative offset changes. A Windows
  build is available through WinBoat SSH only after a reviewed source change.
- The user is the runtime tester. Do not launch or terminate an active VR game
  session as part of review. The current child has crashed; only its Proton
  wrapper remains.

## Open decisions

### 1. Remove unsafe VR VM hooks now or retain an observation-only subset?

**Current lean:** In VR bring-up mode, stop installing `TVMUpdate` and
`TVMDestructor` immediately. `HookVMDestructor` is a confirmed crash source;
`TVMUpdate` dereferences an unvalidated layout and can trigger equivalent
failure. Keep only `TMainLoop` observation if the review finds its address
and forwarding form plausibly safe; otherwise remove all three and rely on a
different verified tick source.

**Alternatives considered:** Retain the VM destructor hook for more logging is
rejected because the log establishes the crash and it cannot safely forward.
Retain `TVMUpdate` merely because it has not fired is rejected because it uses
an unverified object layout.

### 2. What is the right next tick source for connection-only VR?

**Current lean:** First install no unvalidated VM hooks and prove stable
startup. Then identify a Skyrim VR-specific, reverse-engineered tick callback
(prefer SKSE/CommonLib API, a documented SKSE task interface, or a verified
VR executable function) before invoking `World::Update`. Do not turn
`HookMainLoop` into a world tick until its recurrence and thread context are
measured in a stable build.

**Alternatives considered:** Call `World::Update` from `HookMainLoop` now is
rejected because the process crashes before cadence can be measured and the
main-loop target itself is inherited from a flat ID. Poll from a custom thread
is rejected because World and engine APIs have main-thread affinity.

### 3. Is a connection-only service model viable without a full world tick?

**Current lean:** It can validate launch, command parsing, and a narrow
connection path only if those operations can be safely scheduled through a
known main-thread mechanism. It must not be presented as gameplay-ready.

**Alternatives considered:** Directly invoke transport on the bootstrap thread
is rejected pending thread-affinity review.

### 4. What safety rails belong in the next change?

**Current lean:** Make VR bring-up defaults structurally avoid VM hook
installation, log exact enablement, prevent `World::Update` unless a
compile-time reviewed target flag enables it, and add a static audit that
fails if the VM destructor or update hook is installed in default VR builds.

**Possible overlap:** Decisions 1 and 2 are coupled: removing unsafe hooks
changes the evidence available for choosing the next tick source. Merge them
if that produces a clearer recommendation.

## Requested review output

Verify the cited source and logs first. Then provide:

1. A prioritized decision and concrete next patch shape.
2. Whether `TMainLoop` can remain observation-only, and the exact evidence
   required before using it to tick `World::Update`.
3. Any missed ABI/trampoline, teardown, or initialization failure mode.
4. A minimal validation sequence that needs only a user VR run after one
   Windows build.
5. Human decisions only if unavoidable.

Do not edit files. Keep the response to approximately 1,500 words and cite
paths/lines for load-bearing claims.

## Review Disposition

| Recommendation | Disposition | Rationale |
|---|---|---|
| Do not register VM update or destructor hooks in default VR. | Adopted | The destructor detour is a confirmed crash path and the VM update layout is unverified. |
| Keep only main-loop forwarding observation. | Adopted | It has no guessed object-layout access and had been observed on a prior non-crashing run. It does not call client or engine work. |
| Use the default connection-only wrapper for future smoke tests. | Adopted | The deployed gameplay wrapper had normal gameplay services enabled and cannot validate the staged connection-only boundary. |
| Claim command consumption or server connection after this patch. | Rejected | `VRConnectionService` depends on `World::Update`, and no safe VR game-thread tick is established. |
| Add a structural audit for zero VM hooks in the VR block. | Adopted | The previous audit encoded the unsafe registration state as expected behavior. |
