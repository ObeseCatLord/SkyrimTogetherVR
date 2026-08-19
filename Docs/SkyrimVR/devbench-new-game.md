# DevBench New Game Automation

`Tools/SkyrimVR/devbench_new_game.py` drives the Linux/Proton smoke-test path
without headset input:

1. waits for DevBench on `127.0.0.1:8921`;
2. dismisses Skyrim VR calibration when present;
3. compiles a small cached Win32 scan-code helper inside the active Proton
   prefix; it sends enough `Up` presses to clamp to the first row, then `Enter`
   when no `.ess` saves exist, or `Down`, then `Enter` when `Continue` is
   present above `New Game`;
4. publishes a bounded XRizer `trigger` command to accept Realm of Lorkhan's
   New Game confirmation;
5. publishes XRizer `menu`, which is deliberately exposed as legacy OpenVR
   `Grip` (`0x02`) because Skyrim VR maps RaceSex `XButton`/Done to that button,
   verifies the affirmative dialog, and activates it with XRizer `trigger`;
5a. while finalizing and waiting to connect, scans for `MessageBoxMenu`; only
   the verified Realm of Lorkhan intro is accepted via `menu accept` when its
   body matches `someplace unknown` and `outside of time and space` and its sole
   non-cancel button is `Begin`; any other modal blocker is reported as a hard
   failure instead of stalling;
6. requires `RaceSex Menu` itself to close through Skyrim's normal name and
   finalization transaction, then waits for Realm of Lorkhan and verifies the
   finalized player, active `SkyrimTogether.esp`, two stable player/cell
   snapshots; in `active` mode it additionally requires matching lifecycle and
   `playercell` readiness from the current process and epoch;
7. before connecting, requires the dialog to remain closed across two polls and
   observes two newer successful SKSE task sequences; then writes one connection
   command and waits for fresh online status with a nonzero player ID plus the
   first current-cell request.

Requirements are DevBench 1.9.1, `x86_64-w64-mingw32-g++`, the same GE-Proton
and prefix used to launch Skyrim VR, and the bundled patched XRizer runtime.
Host focus tools, `ydotool`, and `/dev/uinput` are not part of the current path.

After the visible RaceSex confirmation closes, Skyrim VR has a second hidden
default-name stage. Automation sends exactly one additional Trigger only while
RaceSex is still the sole actionable target. Manual controller use follows the
same sequence: Grip for Done, Trigger for OK, then Trigger again for the preset
name. The old Monado-Qwerty flow appeared to need one press because its 500 ms
hold spanned both stages. A full XRizer call trace confirmed Skyrim VR does not
call OpenVR's keyboard API during this transaction, so the second explicit
controller stage is required even though XRizer implements those APIs.

The command file accepts exactly `menu` or `trigger`, must be an absolute,
bounded regular file, and is consumed without following symlinks. A synthetic
button remains pressed for a complete OpenXR action-sync interval. Its level and
edges are composed with physical state, so a held real trigger is not released
or double-pressed by automation.

For an unattended run, let the automation launch Skyrim after its persistent
input device is ready:

```bash
Tools/SkyrimVR/devbench_new_game.py \
  --launch-game \
  --skyrim-vr "/path/to/SkyrimVR" \
  --vm-update-mode active \
  --connect incidentalstoat.xyz:26099
```

Connection verification intentionally requires `--launch-game`. The driver
removes inherited `STVR_AUTOCONNECT` and `STVR_PASSWORD` values and launches a
fresh offline process so an older in-memory connection cannot satisfy the test.
It also requires `--vm-update-mode active`; the default `observe` mode installs
the opaque VR VM-update detour for cadence and owner-thread evidence but never
advances the Skyrim Together client.

If XRizer cannot present Skyrim VR's naming keyboard and the opt-in automation
callback is unavailable, the run fails closed after the confirmation dialog. It
never hides `RaceSex Menu`, accepts a still-open RaceSex presentation as
finalized, or manually invokes the native tick to bypass the paused VM. Generic
`kHide` reproduced an access violation and does not perform the engine's
character-finalization transaction.

For unattended connection testing, author a deterministic post-character save
once through the valid vanilla confirmation and naming path using the exact
packaged load order, then load it by save stem:

```bash
Tools/SkyrimVR/devbench_new_game.py \
  --launch-game \
  --skyrim-vr "/path/to/SkyrimVR" \
  --load-save "PostCharacterFixture" \
  --vm-update-mode active \
  --connect incidentalstoat.xyz:26099
```

The deterministic save is an automated connection fixture, not a substitute
for the New Game release gate. Run the same build first with the default
`--vm-update-mode observe` and no `--connect`; promote to `active` only after
the observer log proves stable cadence, one owner thread, correct forwarding,
and clean launch/load/exit behavior.

The Win32 helper emits deterministic scan-code press/release pairs. Controller
commands are level-held for one complete XRizer action-sync interval rather
than depending on host key timing.

Task-sequence evidence is scoped to bytes appended after the current launch;
sequence values restart at 1 in each process even though the bridge log itself
is append-only.

When `--connect` is supplied, stale command, online-status, and player-cell
handoff files are removed before launch. A successful run requires newly written
proof from that process: a ready player with a nonzero form and session ID, a
new accepted-connection generation with a nonzero local player ID, and a newer
interior or exterior cell request from that same generation. The final record
must contain a nonzero cell server base ID and report no worldspace translation
failure. Add `--require-exterior-grid` after leaving Realm of Lorkhan to require
a newer exterior-cell request, nonempty grid, and translated worldspace IDs.
Remote server admission must still be correlated from the server log by the test
operator.
