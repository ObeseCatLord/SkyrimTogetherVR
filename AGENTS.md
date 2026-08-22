# Skyrim Together VR Agent Runbook

## Gameplay Parity Roadmap

Use `Docs/SkyrimVR/original-gameplay-parity-checklist.md` as the living
implementation and acceptance checklist for behavioral parity with the
`original-skyrim-together` branch. Update domain status only when its stated
source, build, or runtime evidence gate has actually passed. Architecture and
dependency-order rationale are in
`Docs/SkyrimVR/full-gameplay-parity-senior-disposition-20260715.md`.

## Two-Stage WinBoat Build

Do not commit or push source changes until their tracked Linux working-tree
delta has passed the disposable WinBoat candidate build. From the Linux
repository root, run:

```bash
Tools/SkyrimVR/build_winboat_candidate.sh
```

The candidate helper snapshots `HEAD`'s complete tracked delta with
`git diff --binary --full-index`, including binary changes and staged changes,
without altering the Linux index or creating a Linux commit. It refuses root
untracked files, unresolved submodules, and unsupported dirty submodules or
changed pointers; only the explicit CommonLib advancement and local-only PLANCK
snapshot flow below are accepted.
For CommonLib it verifies ancestry against the pinned base and trusted alandtse
upstream, bundles the exact target commit for WinBoat, and validates the
transferred bundle hash before checkout. A dirty `Libraries/activeragdoll`
(PLANCK) submodule is also supported only by materializing its complete state
into a local-only synthetic commit and self-contained bundle that retains the
pinned source history needed by an empty WinBoat repository, then materializing
an exact local-only root snapshot bundle. Neither synthetic object updates a
real branch or remote; both hashes and commits are retained in candidate
provenance and independently verified by WinBoat. This prevents an incomplete
overlay or untrusted dependency commit from being presented as build proof.
The current `HEAD` is the candidate base. When it is not yet reachable from the
trusted remote main branch, the helper creates and verifies a minimal Git bundle
from their common ancestor and transfers that base privately to WinBoat. The
guest independently fetches trusted `origin/main`, verifies the ancestry and
bundle identity, then applies the working-tree patch. A candidate build therefore
does not require publishing unverified source first.

On WinBoat it removes only stale `SkyrimTogetherVR-candidate-*` output after
confirming no candidate process is active, initializes the base pinned
submodules, applies the root patch before bootstrapping a newly added PLANCK
path from its verified local bundle, and checks out the exact synthetic
root/PLANCK snapshot so the audited Windows package wrapper
sees clean, immutable provenance. It runs:

```powershell
.\BuildCompleteSkyrimTogetherVR-Windows.ps1 `
  -HavokArchive "$env:LOCALAPPDATA\SkyrimTogetherVR\planck-build\archives\hk2010_2_0_r1.7z" `
  -DependencyRoot "$env:LOCALAPPDATA\SkyrimTogetherVR\planck-build\dependencies" `
  -Configuration Release
```

Candidate artifacts are imported only to the private Linux candidate-result
state directory. The helper does not create/modify a handoff, deploy files, or
launch Skyrim. Its trap and guest `finally` remove the Linux patch, guest patch,
temporary PowerShell payload, disposable worktree, and stale candidate output.
It prints `STVR_CANDIDATE_BASE`, `STVR_CANDIDATE_EPHEMERAL_REVISION`, and
`STVR_CANDIDATE_BUILD_SUCCESS`; only `true` is build proof. If a disconnected
guest still has an active candidate process, cleanup leaves that path in place
rather than deleting a live build; the next candidate run removes it once no
matching process remains.

Because the candidate helper deliberately refuses untracked files, bootstrap a
new copy of this helper and its documentation by staging them first; staged
files are included in the snapshot and the helper itself never changes the
index. After a successful candidate build, make the persistent commit, push it,
then run the normal clean build with handoff creation disabled when handoff
updates are not authorized:

```bash
Tools/SkyrimVR/build_winboat_gameplay.sh --skip-handoff <commit>
```

The normal helper requires a clean, pushed revision. From the Linux repository
root, run:

```bash
Tools/SkyrimVR/build_winboat_gameplay.sh
```

The helper refuses a dirty Linux worktree or a commit that is not reachable
from `github/main`. It uses the private `winboat-ssh` channel, fetches the
matching commit in WinBoat, creates a fresh detached Windows worktree, syncs
all pinned submodules recursively, and runs the command below. Before creating
the new worktree it removes prior generated WinBoat build worktrees so the VM
disk cannot grow by several gigabytes per iteration. Per-build cleanup does not
run `Optimize-Volume`; retrim is optional maintenance and must not block a build.

```powershell
.\BuildCompleteSkyrimTogetherVR-Windows.ps1 `
  -HavokArchive "$env:LOCALAPPDATA\SkyrimTogetherVR\planck-build\archives\hk2010_2_0_r1.7z" `
  -DependencyRoot "$env:LOCALAPPDATA\SkyrimTogetherVR\planck-build\dependencies" `
  -Configuration Release
```

Before either build, the host provisioner hash-verifies
`STVR_HAVOK_ARCHIVE` (defaulting to the private local Havok archive) and the
pinned staged SKSEVR source tree, then creates or reuses the durable private
guest paths shown above. Neither dependency is copied into source, package,
evidence, or handoff output. The complete Windows wrapper force-rebuilds the
exact worktree's PLANCK checkout, then configures xmake and regenerates all required
Papyrus PEX
files, builds and runs `TPTests`, builds the gameplay launcher and all bridge
DLLs, packages them, audits the package, collects build evidence, and audits
the evidence archive. It never installs files or launches Skyrim. Caprica is
resolved from `-PapyrusCompiler`, `CAPRICA`, `PATH`, `C:\Tools\Caprica`, or the
repository-adjacent `_refs` locations, in that order.

The WinBoat helper prints the retained Windows result paths and then uses the
private WinBoat SCP channel to import the exact result. It creates a deterministic
Linux gameplay ZIP, independently validates the gameplay manifest, evidence,
hashes, flavor, and source revision, then calls
`Tools/SkyrimVR/finalize_local_agent_handoff.sh`. The finalizer uses root-backed
state storage instead of `/tmp`, builds or reuses the exact pinned Bullseye
OpenComposite/XRizer binaries, validates their hashes and glibc ceilings,
audits the supplied live one-client gameplay-bootstrap evidence and binds it to
the exact gameplay build manifest,
regenerates and audits the complete local-agent handoff ZIP, checks ZIP
integrity, verifies the basename sidecar from the archive directory, uploads
both files resumably to `foundry:~/videos/`, and verifies the checksum on
Foundry. Override the destination with `STVR_HANDOFF_UPLOAD_TARGET=host:path/`.
The final paths are printed as
`STVR_LINUX_GAMEPLAY_PACKAGE`, `STVR_LINUX_BUILD_EVIDENCE`, and
`STVR_LOCAL_HANDOFF`. Do not manually select a different package/evidence pair
afterward. The handoff generator requires the same clean committed worktree and
includes the current checklist/documentation without raw Codex/session telemetry
or unredacted runtime logs.

For an already imported exact package/build-evidence pair and its accepted live
runtime evidence, run the same final stage without rebuilding the Windows DLLs:

```bash
Tools/SkyrimVR/finalize_local_agent_handoff.sh \
  --gameplay-package /absolute/path/to/gameplay.zip \
  --build-evidence /absolute/path/to/build-evidence.zip \
  --runtime-evidence /absolute/path/to/gameplay-bootstrap-runtime-evidence.zip \
  --upload-target foundry:videos/
```

The finalizer refuses a dirty repository, mismatched or unreviewed runtimes,
runtime evidence from another build, an existing output path, an invalid
archive, a bad local sidecar, or a failed remote checksum. The resulting
handoff embeds the exact runtime evidence under `evidence/`. The Foundry
archive is private; do not publish it to GitHub.

When handoff updates are not authorized, `--skip-handoff` is required. It still
performs the full Windows build/audit, SCP import, deterministic gameplay package,
and package/evidence validation, but does not create, overwrite, regenerate,
audit, or unzip a handoff archive:

```bash
Tools/SkyrimVR/build_winboat_gameplay.sh --skip-handoff <commit>
```

The option may also follow the optional commit:

```bash
Tools/SkyrimVR/build_winboat_gameplay.sh <commit> --skip-handoff
```

The helper prints `STVR_LINUX_GAMEPLAY_PACKAGE`, `STVR_LINUX_BUILD_EVIDENCE`, and
an explicit `STVR_LOCAL_HANDOFF=SKIPPED` status in this mode.

A new build cannot truthfully produce a final handoff until that exact binary
has passed the live bootstrap. Use `--skip-handoff` for the initial build,
install and run it, collect with `collect_runtime_evidence.py
--gameplay-bootstrap`, audit with `audit_runtime_evidence_zip.py
--require-gameplay-bootstrap`, commit the resulting tooling/docs, and then run
the finalizer command above. `build_winboat_gameplay.sh` accepts
`--runtime-evidence ZIP` or `STVR_RUNTIME_EVIDENCE` only for an evidence archive
whose nested build identity matches the revision being built; it never
auto-selects an older archive.

The helper stages its generated PowerShell driver as a temporary `.ps1` through
the private SCP channel instead of passing it through `-EncodedCommand`, which
avoids the Windows command-line length limit. Its exit trap removes that guest
driver, the Linux copy, and the temporary imported result even when the build
fails.

Do not build from the long-lived primary Windows checkout: generated PEX and
package files make rebuild provenance ambiguous. The helper exports the audited
package/evidence pair, removes its detached Windows worktree in a `finally`
path, removes its temporary Linux import, and runs bounded cleanup on exit. If
a candidate build exposes a source error, fix all related occurrences and rerun
the candidate build. Only after it reports `STVR_CANDIDATE_BUILD_SUCCESS=true`
may the persistent revision be committed and pushed, followed by the normal
clean build. After that successful clean build, deploy and run the applicable
acceptance test, update the build-result notes and parity checklist, then commit
and push those evidence notes before further source work.

Overrides are available when the WinBoat layout differs:

```bash
STVR_WINBOAT_REPO='C:\Users\name\Documents\Codex\SkyrimTogetherVR' \
WINBOAT_POWERSHELL=/path/to/winboat-powershell \
WINBOAT_SSH=/path/to/winboat-ssh \
Tools/SkyrimVR/build_winboat_gameplay.sh <commit>
```

## One-Command Server Build

Build the Linux server image from a clean checkout with initialized submodules:

```bash
Tools/SkyrimVR/server/build_server_image.sh skyrim-together-vr-server:<tag>
```

The helper uses BuildKit cache mounts when buildx is available and automatically
falls back to a temporary cacheless Dockerfile on older hosts. It does not
modify the tracked Dockerfile, start a container, or stop the running server.
Use `Docs/SkyrimVR/server-deployment.md` for the one-container deployment and
verification procedure.

Build and deploy the committed source to the Foundry ARM64 test server in one
command:

```bash
Tools/SkyrimVR/server/deploy_foundry_server.sh
```

The deployer requires a clean tree and initialized submodules, incrementally
syncs source over SSH, builds natively on Foundry, preserves the persistent
server mounts, replaces only `skyrim-together-vr`, verifies UDP 26099 and zero
restarts, rolls back on startup failure, and removes its temporary source tree.

For a fast clean MSVC check of only the CommonLib gameplay bridge after a
bridge-local compile fix, push the exact commit and run:

```bash
Tools/SkyrimVR/compile_winboat_gameplay_bridge.sh <commit>
```

The preflight creates a disposable detached WinBoat worktree, configures the
same CommonLib runtime matrix as the release build, runs the mandatory unit
tests, compiles `SkyrimTogetherVRGameplayBridge`, and removes the worktree and
temporary PowerShell payload on every exit. It does not create a distributable
package or handoff; follow a successful preflight with
`build_winboat_gameplay.sh` for the complete audited package and handoff.

## Build Storage Cleanup

The checked-in cleanup command only targets generated Skyrim Together build
worktrees, this repository's package artifacts, and explicitly requested
rebuildable caches:

```bash
Tools/SkyrimVR/cleanup_build_storage.sh --max-age-days 7 --trim
```

The scheduled cleanup also removes ignored `build/.objs` and `build/linux`
output and repository-local Python bytecode caches when `/` is at or above the
configured pressure threshold. Before a WinBoat build,
`build_winboat_gameplay.sh` removes those reproducible local outputs explicitly.
Do not extend this cleanup to source, the current prerelease/handoff bundle,
runtime evidence, game installs, or unrelated user data.

`Tools/SkyrimVR/install_build_cleanup_timer.sh` installs and enables a user
systemd timer that runs every three hours and removes generated outputs older
than two days. A transient service failure is retried after five minutes. Every
WinBoat build runs the same bounded cleanup before and after the build. If root
reaches 93% use or falls below 160 GiB free, scheduled cleanup reduces retention
to zero days for those generated paths and purges only the explicitly listed
rebuildable caches. The timer also removes only `/tmp/stvr-*` temporary
test/build paths older than two days; it does not scan unrelated temporary
content. The build helper holds a shared activity lock for its entire run, and
scheduled cleanup skips its pass while that lock is held. This prevents cleanup
from deleting an active build's reproducible output. The build helper removes
its detached WinBoat worktree immediately after exporting the package/evidence
pair. The timer always retains the newest exported result and expires older
result directories after two days. It does not request a synchronous volume
retrim. Cleanup uses a process lock and skips WinBoat cleanup without failing
when the VM is offline. Do not expand the cleanup
patterns to game installs, source checkouts, model caches, handoff archives,
Docker containers, or unrelated application data.

## Runtime Safety

- Treat `Docs/SkyrimVR/runtime-admission-contract.md` as the authoritative
  definition of a successful runtime test. A process, one fresh file, or
  `online=1` alone is never sufficient.
- Final-handoff discovery and audit accept only runtime archives with exact
  `liveAdmissionRequested=true` and `runtimeEvidenceTrust=trusted`. Generic or
  crash-only evidence remains useful for diagnosis but cannot prove release
  admission even when its other checklist fields pass.
- Use one exact game root per client. The canonical launcher holds an exclusive
  writer lock for that root, and all accepted readouts must agree on launch
  nonce, process, game root, client/server build, protocol, session, and
  connection generation.
- Never force-close `RaceSex Menu` with DevBench `menu close`/`kHide`. It does
  not run Skyrim's confirm/name transaction and has reproduced an access
  violation.
- Do not issue the connection command while `Main Menu`, `RaceSex Menu`, a
  loading menu, or a message box is open. Require fresh lifecycle readiness
  from the current process and epoch.
- Do not terminate a live test unless the user asks, the process crashes, or a
  relaunch is required for a deployed binary/runtime change.

## Linux Test Path

Skyrim VR root:

```bash
SKYRIMVR="/home/obesecatlord/FasterGames/SteamLibrary/steamapps/common/SkyrimVR"
```

Start or verify Monado before launch:

```bash
Tools/SkyrimVR/linux/manage-monado-runtime.sh start simulated-qwerty-fixed
Tools/SkyrimVR/linux/manage-monado-runtime.sh check
```

The helper leaves an already healthy Monado listener unchanged. If no listener
exists, it removes only an orphan `monado_comp_ipc` socket, then launches the
selected Envision profile in the
persistent `stvr-monado-runtime.service` user unit. Do not run `envision
--start &` from a short-lived shell: that shell can tear down Monado while
leaving its socket behind, causing XRizer `XR_ERROR_RUNTIME_UNAVAILABLE` and a
launcher status 5. Readiness requires `ss -xlp` to show `monado-service` as the
live listener and the OpenXR canary must enumerate a headset and view
configuration; the socket file existing by itself is not evidence. The socket
fixture deliberately writes additional listeners after the match because an
early parser exit becomes a false negative under `pipefail`. Use
`restart`, `stop`, or a different profile UUID through the same helper when the
runtime must change.

The current isolated handoff acceptance path is:

```bash
GAME=/home/obesecatlord/Games/SkyrimVR-STVR-handoff-c18ca1d8
PREFIX=/home/obesecatlord/Games/STVR-handoff-proton-compatdata-c18ca1d8
PROTON=/home/obesecatlord/.local/share/Steam/compatibilitytools.d/GE-Proton10-34
cd "$GAME"
STVR_FORCE_PROTON=1 STVR_COMPATDATA="$PREFIX" STVR_PROTONPATH="$PROTON" \
  STVR_OPENVR_RUNTIME=xrizer STVR_AUTOCONNECT=incidentalstoat.xyz:26099 \
  ./launch-skyrim-together-vr.sh
```

For interactive and two-client tests, start the player in Realm of Lorkhan by
default. Prefer a valid post-character Realm save when repeating a test, or
complete the normal Realm new-game and RaceSex flow for a fresh-character
gate. Use a different settled save only for a test that specifically requires
that location; the Sleeping Giant Inn save is the calibration-transition
fixture, not the normal multiplayer starting point.

The directory name is retained because the transactional install was created
during the earlier `c18ca1d8` bring-up. Its authoritative installed
`SkyrimTogetherVR_BuildManifest.json` now reports exact build `9eba3bb5`; do not
infer the installed revision from this historical directory name.

Skyrim VR's startup calibration is not a gameplay-ready state. While
`CalibrationOptionMenu` is open or the player remains in SkyrimVR.esm cell
`0x040008D4` (`VRPlayroom01`), require lifecycle `state=suspended`, `ready=0`,
and reason `calibration_option_menu` or `vr_playroom`; Foundry must not admit the
client. Do not close `Fader Menu` manually during calibration. After a normal
New Game completion or settled save load leaves the playroom, require lifecycle
readiness, exact-version authentication, and either a naturally absent Fader or
the logged `candidate` -> `hide issued` -> `verified closed` recovery sequence.

Require the status, lifecycle, player-cell, avatar, and gameplay readouts to
pass the fresh launch identity gate. `SkyrimTogetherVR.status` must also report
equal nonempty client/server versions, protocol revision 21, and nonzero
server, session, and connection-generation identities. The matching bridge
mapping must validate ABI 24 and capability revision 34.
`ready=1` is required only for gameplay and gameplay-bootstrap profiles; every
profile still requires a fresh structurally valid gameplay snapshot. Then
correlate that identity with Foundry's `STVR auth accepted` line. The current exact acceptance is recorded in
`Docs/SkyrimVR/runtime-connection-result-20260819-9eba3bb5.md`. The earlier
`c18ca1d8` run remains as historical diagnosis of character creation and the
stale fader.

The Linux helper must pass the game executable through Proton's standard
`Z:` mapping, for example
`Z:\\home\\...\\SkyrimVR\\SkyrimVR.exe`. Do not prefer a custom Steam-library
drive such as `S:` merely because its `dosdevices` symlink exists before
launch: Proton can recreate that directory during prefix initialization and
remove the mapping after the launcher has computed its command line. The
result is launcher error 161 (`The specified path is invalid`) before Skyrim
starts.

Before either direct Proton or `umu-run`, the online launcher structurally
validates `SkyrimTogetherVR_BuildManifest.json`: Windows x64 schema and flavor,
canonical network-version grammar, equal build/network versions, a 40-hex
source revision, a 64-hex source-tree hash, and clean non-approved provenance
are mandatory. A custom launcher path does not bypass this check. Non-dry
launches then take the canonical game-root lock before modifying plugin order
or restoring handoff-owned DLLs.

Launch the deterministic New Game and connection test:

```bash
cd /home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR
python3 Tools/SkyrimVR/devbench_new_game.py \
  --launch-game \
  --skyrim-vr "$SKYRIMVR" \
  --vm-update-mode active \
  --connect incidentalstoat.xyz:26099 \
  --timeout 180
```

For two simulated clients on the same Linux host, use only the named-instance
helpers. They isolate the game tree, Proton prefix, caches, logs, transient
user-service cgroup, and Monado IPC runtime:

```bash
export STVR_CLIENT_ROOT=/home/obesecatlord/FasterGames/SteamLibrary/steamapps/common/SkyrimTogetherVR-local-clients
export STVR_BASE_GAME_DIR="$SKYRIMVR"
export STVR_BASE_COMPATDATA=/home/obesecatlord/FasterGames/SteamLibrary/steamapps/compatdata/611670
Tools/SkyrimVR/linux/manage-local-clients.sh prepare player1
Tools/SkyrimVR/linux/manage-local-clients.sh prepare player2
Tools/SkyrimVR/linux/manage-local-clients.sh launch player1 incidentalstoat.xyz:26099
Tools/SkyrimVR/linux/manage-local-clients.sh launch player2 incidentalstoat.xyz:26099
```

Preparation is intentionally non-destructive and refuses existing names. On
the current NTFS game volume, budget approximately one complete game plus one
Proton prefix for every client. Do not place client copies on `/`. Stop clients
through `manage-local-clients.sh stop NAME`; it validates the marker, unit
environment, and systemd InvocationID before stopping the owned cgroup and
never escalates to `KILL`. The launcher uses `GAMEID=umu-611670` and the
matching Monado OpenXR manifest/state; use short client roots because the
private Unix socket is limited to 107 bytes. Exact operation, checks, and
limitations are in `Docs/SkyrimVR/linux-local-multi-client.md`.

## Build Execution Policy

GitHub Actions are manual reference workflows only. Pushes, pull requests,
tags, and schedules must not start project builds, and agents must not trigger
`workflow_dispatch`. The authoritative client build runs under MSVC on WinBoat
through the scripts in this runbook; the authoritative Linux server build runs
locally through `Tools/SkyrimVR/server/build_server_image.sh`.

For comparison, the retained manual Windows workflow must use the same complete
patched-PLANCK command:

```powershell
.\BuildCompleteSkyrimTogetherVR-Windows.ps1 `
  -HavokArchive "D:\archives\hk2010_2_0_r1.7z" `
  -DependencyRoot "D:\SkyrimTogetherVR-planck-dependencies" `
  -Configuration Release -Mode releasedbg
```

Do not use that workflow as build evidence. Run the documented WinBoat candidate
and clean-build paths instead, followed by target-machine and live VR checks.
Linux fixture checks remain available through the repository's local scripts;
run them locally when their behavior is affected.

The persistent synthetic keyboard socket is:

```bash
export YDOTOOL_SOCKET=/run/user/1000/stvr-ydotool.sock
```

## Verified Menu Inputs

Main Menu -> New Game:

1. Use `devbench_new_game.py`'s cached Win32 helper inside the exact Proton
   prefix. It sends repeated `Up` presses to clamp to the first row, then
   `Enter` when no `.ess` saves exist. When saves exist, it sends `Down`, then
   `Enter` so `Continue` cannot be loaded in place of `New Game`. Do not use
   host focus injection.
2. Publish XRizer `trigger` to accept Realm of Lorkhan's New Game confirmation.

Automated RaceSex completion with XRizer:

1. Publish XRizer `menu`. Despite the command name, this emits legacy OpenVR
   `Grip` (`0x02`), matching the installed `controlmapvr.txt` RaceSex
   `XButton`/Done binding. `ApplicationMenu` (`0x01`) is incorrect here.
2. Require DevBench `messageBoxOpen: true` and body text
   `Finish and name your character?`.
3. Publish XRizer `trigger`, translated to legacy Trigger/Menu Accept.
4. After the visible dialog closes, require RaceSex to remain the sole
   actionable target and publish one more `trigger` for Skyrim VR's hidden
   default-name stage.
5. Require `RaceSex Menu` to disappear. A closed dialog with RaceSex still
   open is not finalization.

For manual physical-controller completion, use the normal controller actions:
Grip activates RaceSex Done, Trigger accepts the affirmative dialog, and a
second Trigger accepts the preset name after the dialog disappears. The
Oculus legacy facade changes only the properties Skyrim uses to select its
control-map columns; XRizer still obtains poses, sticks, Grip, and Trigger from
the native Index OpenXR profile. This is source-verified but must be accepted on
physical Index hardware; the simulated Qwerty profile cannot prove it.

The normal XRizer launcher supplies `STVR_XRIZER_KEYBOARD_TEXT=Prisoner` only
when the caller did not set a name. The patched runtime implements both OpenVR
keyboard entry points, but a 2026-08-18 full OpenVR call trace proved Skyrim VR
does not call either one in this RaceSex transaction. The old Qwerty automation
worked because its 500 ms Trigger hold spanned the visible confirmation and the
hidden default-name stage. Direct commands intentionally emit one controller
press, so automation now performs a second state-gated press explicitly.

The predecessor controller/keyboard path was runtime-verified on 2026-07-14:
automation selected New Game without a manual click, finalized the player as
`Shezarrine`, closed RaceSex through the normal transaction, accepted the
allowlisted Realm intro, and reached `RealmLorkhan`. The current direct command
path must repeat that no-save gate before its handoff is published. FUS does
not supply a RaceSex/input mod fix: its
launcher explicitly disables OpenComposite because it breaks the keyboard and
SteamVR overlays. SteamVR normally provides the overlay keyboard transaction
that XRizer must emulate for this Monado test path.

## VR Update Owner

Skyrim VR 1.4.15 address ID `53926` is a project-local alias for
`BSScript::Internal::VirtualMachine::Update(float)`. Its verified RVA is
`0x12765B0` and its ABI is `void (this, float)`. The evidence is CommonLibSSE-NG's
VR vtable RVA `0x18E2148`, where virtual slot 4 points to `0x1412765B0` in
SkyrimVR.exe. The former override `0x9869D0` was not this virtual function and
must not be restored. `Tools/SkyrimVR/audit_bringup_hooks.py` enforces the
address, provenance, and void-return declaration.

The installed 1.4.15 executable used for this proof has SHA-256
`6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971`.
Its `.rdata` starts at RVA `0x157F000` / file offset `0x157DA00`, so the VM
vtable and slot-4 file offsets are `0x18E0B48` and `0x18E0B68`. Recheck a target
install without launching it:

```bash
objdump -h "$SKYRIMVR/SkyrimVR.exe"
od -An -v -tx8 -j $((0x18e0b68)) -N 8 "$SKYRIMVR/SkyrimVR.exe"
```

The expected slot value is `00000001412765b0`. This VM target is forwarding
telemetry only. Do not enable `active` mode for an executable whose section
layout, vtable entry, or version differs.

In `active` mode, the SKSE task bridge publishes only an atomic permit. The
exact `Main::Draw` target at address ID `35560`, RVA `0x5B9330`, consumes that
permit and calls the client once; task and VM callbacks must never call
`World::Update()` directly. The owner is the Windows thread recorded by
`TickBridge::Activate()` immediately before the endpoint is published `Ready`.
Only an outermost draw on that thread may dispatch, under the atomic reentrancy
guard, after the original draw returns. Worker VM calls are logged and forwarded
without consuming a permit. A viable observer run must log recurring
`SkyrimTogetherVR Main::Draw owner cadence` with owner-thread equality and no
reentrancy. A viable active connection run must additionally log
`SkyrimTogetherVR Main::Draw client update completed`, advance lifecycle from
`boot`, and retain the activation thread as owner.

Do not use Monado `O` for RaceSex Done under the simple-controller profile.
`O` changes the Qwerty WMR squeeze input, but XRizer's simple-controller legacy
Grip comes from `/input/menu/click`, which is Monado `N`.

Inspect live menu state without mutation:

```bash
curl -sS -X POST -H 'Content-Type: application/json' \
  --data '{"action":"list"}' http://127.0.0.1:8921/api/tool/menu
curl -sS -X POST -H 'Content-Type: application/json' \
  --data '{"action":"describe"}' http://127.0.0.1:8921/api/tool/menu
```

If the visible RaceSex dialog closes but RaceSex remains open, send one more
Trigger to accept the preset name. Do not hide RaceSex or report connection
success unless the menu closes through that transaction.

## Success Evidence

A successful connection test requires all of the following from the current
process:

- current `sksevr.log` reports `SkyrimTogetherVRGameplayBridge.dll ... loaded
  correctly`, and `SkyrimTogetherVRGameplayBridge.log` reports loader runtime
  `0x010400F1`, SKSE version at least `0x020000C0`, and release index at least
  `60`; do not confuse that SKSE interface version with SkyrimVR.exe and VR
  Address Library version `1.4.15.0`;
- no `Main Menu`, `RaceSex Menu`, loading menu, message box, or persistent
  `Fader Menu`; the automation may close only a lone Realm post-load fader
  after stable player/cell finalization has already been proven;
- Realm of Lorkhan scene and stable nonempty player name/race;
- `SkyrimTogether.esp` active;
- lifecycle `state=ready`, `ready=1`, nonzero epoch/owner/player/cell;
- online status with nonzero local player ID and a newer cell request;
- matching server-side admission while exactly one server container is running;
- after a normal exit, `SkyrimTogetherVR WinMain lifecycle shutdown hook reached`
  with no owner mismatch, starvation, reentrancy, or new crash dump.

Use `Tools/SkyrimVR/audit_runtime_handoff.py` for the local post-run audit.

## Patched PLANCK Complete Gameplay Package

To produce the authoritative full gameplay package with the patched PLANCK
`interface002` DLL, run this from a Windows checkout. `HavokArchive` is always
caller-supplied; the helper never downloads Havok, never deploys to a game
install, and keeps PLANCK dependencies outside the repository.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\BuildCompleteSkyrimTogetherVR-Windows.ps1 `
  -HavokArchive "D:\archives\havok2010_2_0_r1.7z" `
  -DependencyRoot "D:\SkyrimTogetherVR-planck-dependencies" `
  -SKSEVRArchive "D:\archives\sksevr_2_00_12.7z" `
  -SevenZipPath "C:\Program Files\7-Zip\7z.exe" `
  -Configuration Release
```

The wrapper builds `Libraries\activeragdoll\x64\Release\activeragdoll.dll`,
passes that exact artifact through the existing gameplay build/audit/collect
path, and then requires the resulting manifest's `interface002` marker, package
path, and SHA-256 to match `Data\SKSE\Plugins\activeragdoll.dll`. The package
copy happens after staged handoff game files, so it overrides any stock PLANCK
overlay for installation on each gameplay client. Before every forced rebuild it
freshly extracts and hashes every file in the complete private Havok build-input
tree and complete SKSEVR build tree. The forced provenance and package manifest
bind both archive SHA-256 values and both tree digests/counts; the package audit
rejects a missing, malformed, or mismatched binding. Ordinary gameplay builds
do not add or require this DLL unless `-PatchedPlanckArtifact` is explicitly
supplied.
