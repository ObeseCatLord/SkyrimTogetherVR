# Skyrim Together VR Agent Runbook

## Gameplay Parity Roadmap

Use `Docs/SkyrimVR/original-gameplay-parity-checklist.md` as the living
implementation and acceptance checklist for behavioral parity with the
`original-skyrim-together` branch. Update domain status only when its stated
source, build, or runtime evidence gate has actually passed. Architecture and
dependency-order rationale are in
`Docs/SkyrimVR/full-gameplay-parity-senior-disposition-20260715.md`.

## One-Command WinBoat Build

Commit and push all source and submodule changes before a Windows build. From
the Linux repository root, run:

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

```bat
BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay
```

That Windows wrapper configures xmake, regenerates all required Papyrus PEX
files, builds and runs `TPTests`, builds the gameplay launcher and all bridge
DLLs, packages them, audits the package, collects build evidence, and audits
the evidence archive. It never installs files or launches Skyrim. Caprica is
resolved from `-PapyrusCompiler`, `CAPRICA`, `PATH`, `C:\Tools\Caprica`, or the
repository-adjacent `_refs` locations, in that order.

The WinBoat helper prints `STVR_BUILD_WORKTREE`, `STVR_GAMEPLAY_PACKAGE`, and
`STVR_BUILD_EVIDENCE` on success. Do not build from the long-lived primary
Windows checkout: generated PEX and package files make rebuild provenance
ambiguous. If a build exposes a source error, fix all related occurrences,
commit and push the next revision, then rerun the same helper. Do not delete old
worktrees without explicit cleanup intent.

Overrides are available when the WinBoat layout differs:

```bash
STVR_WINBOAT_REPO='C:\Users\name\Documents\Codex\SkyrimTogetherVR' \
WINBOAT_POWERSHELL=/path/to/winboat-powershell \
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

## Build Storage Cleanup

The checked-in cleanup command only targets generated Skyrim Together build
worktrees, this repository's package artifacts, and explicitly requested
rebuildable caches:

```bash
Tools/SkyrimVR/cleanup_build_storage.sh --max-age-days 7 --trim
```

`Tools/SkyrimVR/install_build_cleanup_timer.sh` installs and enables a user
systemd timer that runs daily and removes generated outputs older than two
days. The timer also removes only `/tmp/stvr-*` temporary test/build paths
older than two days; it does not scan unrelated temporary content. The build
helper also removes prior generated WinBoat worktrees before each build; it
does not request a synchronous volume retrim. Cleanup uses a process lock and
skips WinBoat cleanup without failing when the VM is offline. Do not expand the cleanup
patterns to game installs, source checkouts, model caches, handoff archives,
Docker containers, or unrelated application data.

## Runtime Safety

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

Verify Monado before launch:

```bash
pgrep -af 'monado-service|envision'
test -S /run/user/1000/monado_comp_ipc
```

The Linux helper must pass the game executable through Proton's standard
`Z:` mapping, for example
`Z:\\home\\...\\SkyrimVR\\SkyrimVR.exe`. Do not prefer a custom Steam-library
drive such as `S:` merely because its `dosdevices` symlink exists before
launch: Proton can recreate that directory during prefix initialization and
remove the mapping after the launcher has computed its command line. The
result is launcher error 161 (`The specified path is invalid`) before Skyrim
starts.

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

The persistent synthetic keyboard socket is:

```bash
export YDOTOOL_SOCKET=/run/user/1000/stvr-ydotool.sock
```

## Verified Menu Inputs

Use `kdotool` to focus the exact window before every input. Hold synthetic keys
for at least 500 ms.

Main Menu -> New Game:

1. Focus `^Skyrim VR$`; send Linux key `End` (`107`), then `Enter` (`28`).
2. Focus `^Monado!.*$`; send `P` (`25`) to activate the emulated trigger and
   accept Realm of Lorkhan's New Game confirmation.

RaceSex completion with the Monado Qwerty driver and XRizer:

1. Confirm the current XRizer log negotiated
   `/interaction_profiles/khr/simple_controller` for both hands.
2. Focus `^Monado!.*$`; send `N` (`49`). This is simple-controller Menu,
   translated by XRizer to legacy Grip and Skyrim's RaceSex `XButton`/Done.
3. Require DevBench `messageBoxOpen: true` and body text
   `Finish and name your character?`.
4. Focus `^Monado!.*$`; send `P` (`25`). This is simple-controller Select,
   translated to legacy Trigger/Menu Accept.
5. Require the confirmation dialog to close and then require `RaceSex Menu` to
   disappear. A closed dialog with RaceSex still open is not finalization.

The automation launcher sets `STVR_XRIZER_KEYBOARD_TEXT=Shezarrine` by default
(`--character-name` overrides it). The active Envision runtime is the checkout
named by `~/.config/openvr/openvrpaths.vrpath`, currently
`~/.local/share/envision/ovr_comp`; do not build an inactive duplicate checkout.
Its opt-in patch implements `ShowKeyboardForOverlay`, stores the name, and
queues `KeyboardDone` in both the global OpenVR event queue and the requesting
overlay's `PollNextOverlayEvent` queue. Skyrim VR requires the overlay event;
global delivery alone closes the confirmation but leaves RaceSex open. In
`IVROverlay_028`, `unCharMax` is the second `u32` after line mode; the first is
keyboard flags. Without the environment variable, XRizer retains its normal
`RequestFailed` behavior.

This exact path was runtime-verified on 2026-07-14: automation selected New
Game without a manual click, finalized the player as `Shezarrine`, closed
RaceSex through the normal transaction, accepted the allowlisted Realm intro,
and reached `RealmLorkhan`. FUS does not supply a RaceSex/input mod fix: its
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

If XRizer logs `ShowKeyboardForOverlay unimplemented`, the opt-in automation
runtime is not active. Accepting the RaceSex dialog will leave RaceSex waiting
for the naming callback. Stop there; do not hide the menu or report connection
success.

## Success Evidence

A successful connection test requires all of the following from the current
process:

- no `Main Menu`, `RaceSex Menu`, loading/fader menu, or message box;
- Realm of Lorkhan scene and stable nonempty player name/race;
- `SkyrimTogether.esp` active;
- lifecycle `state=ready`, `ready=1`, nonzero epoch/owner/player/cell;
- online status with nonzero local player ID and a newer cell request;
- matching server-side admission while exactly one server container is running;
- after a normal exit, `SkyrimTogetherVR WinMain lifecycle shutdown hook reached`
  with no owner mismatch, starvation, reentrancy, or new crash dump.

Use `Tools/SkyrimVR/audit_runtime_handoff.py` for the local post-run audit.
