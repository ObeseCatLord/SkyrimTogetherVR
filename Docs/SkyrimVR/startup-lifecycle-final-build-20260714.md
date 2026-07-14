# Startup Lifecycle Final Build - July 14, 2026

## Revision Contract

- Client source: `220bc4bca709bf1fa208984d1fd4b2b5b773210a`
- TiltedConnect submodule: `fe9b2033cfc82551aa1350eb9f298480a77ad7e7`
- Package mode/flavor: `releasedbg` / `default`
- Package generated: `2026-07-14T22:49:25.8012227Z`
- Source dirty: false
- Papyrus compiler: Caprica 0.3.0

The deployed server remains compatible with this client build. The commits
after the matched server revision `99d1e1d1` change client startup, lifecycle,
discovery, player readiness, connection scheduling, and TiltedConnect client
ownership. They do not change `Code/server`, `Code/encoding`, or the VR wire
schema.

## Corrected Startup Flow

The VR client now follows the same ownership model as upstream Skyrim
Together: the game's VM update callback owns `TiltedOnlineApp::Update()`, and
TiltedConnect callbacks run on that same thread. The SKSE task bridge only
publishes a coalesced permit; it never pumps the application or networking.

The VR lifecycle service blocks connection and gameplay traffic while the
main menu, RaceSex menu, loading menu, or fader menu is open. After those
menus close, player, base, and cell identities must remain stable for five
ticks and at least 250 ms before the lifecycle enters `ready`. Load events or
identity changes advance the lifecycle epoch, close stale transport state,
and require stabilization before reconnecting.

Character creation is intentionally not automated by the client. Like the
upstream implementation, Skyrim Together starts normal networking only after
Skyrim has produced a valid player and cell. DevBench automation should load a
known post-character save instead of forcing RaceSex menu closure.

The installable binary defaults to `STVR_VM_UPDATE_MODE=observe` for safe
diagnostics. The deployed `launch-skyrim-together-vr.sh` sets it to `active`
for Together runs; callers can explicitly override it to `observe` or `off`.

## Windows Build And Deployment

The exact clean revision was built in WinBoat with Visual Studio 2022, xmake,
the official SKSEVR 2.0.12 SDK, and Caprica 0.3.0. The client, launcher,
VRIK/HIGGS/PLANCK bridges, SKSE tick bridge, EarlyLoad, TPProcess, CEF payload,
and eight Papyrus scripts compiled successfully. The Windows package audit
reported zero failures.

The default package was copied to Linux and installed into the local Skyrim VR
directory. The strict post-install audit reported zero failures with SKSEVR,
VR Address Library, VRIK, HIGGS, and PLANCK present, and all installed Papyrus
hashes matching the package.

| Artifact | SHA-256 |
|---|---|
| `SkyrimTogetherVR.exe` | `79e8db41c1abc134774141142b202fc16c266b0244aab375e07aaa27319c541e` |
| `EarlyLoad.dll` | `108332d43e4b3e39ee01ec079c6bb96aeb207f51646c9f8d2de233c1d9106614` |
| `TPProcess.exe` | `29b152d06622fa1a170c086445ca4b86b27fff3b996f24d374a512a81dc61dbf` |
| `SkyrimTogetherVRHiggsBridge.dll` | `b86ae1c01d8b9d4ed4939dd7ea5e37e400f18bba8697d7386a9b4c41e8b705f4` |
| `SkyrimTogetherVRPlanckBridge.dll` | `dcd74c51bcde3638cb77cc684d67ff5d137754778a9527fbbcea824154403070` |
| `SkyrimTogetherVRTickBridge.dll` | `bda4bf7f4b82ca8c07c627a9a523789f41e58028c7467e75294f06bf8aaefeb0` |
| `SkyrimTogetherVRVrikBridge.dll` | `876f8187f49f37e03781733e8b162330eca003ead96a8e7e3bcf73c057063a4e` |

## Review And Verification

The architecture and implementation were reviewed against the untouched
`original-skyrim-together` branch. The review artifacts are:

- `startup-lifecycle-senior-brief-20260714.md`
- `startup-lifecycle-senior-disposition-20260714.md`

All no-launch source audits passed before the Windows build. The final package
and installed-prerequisite audits passed after compilation and deployment.
No Skyrim process was launched during this build/deployment cycle.

## Runtime Acceptance

The next test should use a valid post-character save and the normal Together
launcher. Acceptance requires:

1. `SkyrimTogetherVR.lifecycle` reaches `ready` on one stable VM owner thread.
2. The retained autoconnect command is consumed for the current lifecycle
   epoch.
3. Transport connects and authenticates without a first-tick or resolver
   teardown crash.
4. The remote server reports one connected player.
5. A collected runtime evidence archive passes the strict connection checks.

Runtime success must come from the user's VR test and collected evidence; the
successful compile and package audits do not substitute for it.
