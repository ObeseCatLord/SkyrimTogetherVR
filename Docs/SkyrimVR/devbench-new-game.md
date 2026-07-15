# DevBench New Game Automation

`Tools/SkyrimVR/devbench_new_game.py` drives the Linux/Proton smoke-test path
without headset input:

1. waits for DevBench on `127.0.0.1:8921`;
2. dismisses Skyrim VR calibration when present;
3. creates or reuses a persistent `ydotoold` keyboard before Proton starts;
4. focuses Skyrim with `kdotool`, selects the top New Game entry with `End`
   (Skyrim VR's Scaleform list is indexed bottom-to-top), and sends the VR
   trigger alias to Monado's Qwerty controller;
5. requests RaceSex completion with Monado `N` (the Qwerty
   `KHR/simple_controller` menu action, exposed by XRizer as legacy Grip /
   RaceSex `XButton`), verifies the affirmative dialog, and activates it through
   a held Monado `P` controller trigger;
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

Requirements are DevBench 1.9.1, `kdotool`, `ydotool`/`ydotoold`, write access
to `/dev/uinput`, and the Monado Qwerty aliases from
`Tools/SkyrimVR/monado-qwerty-automation.patch`. The patch maps `P` to the
focused controller trigger and `O` to squeeze because synthetic pointer
positioning is not reliable on multi-monitor Wayland.

The automation sets `STVR_XRIZER_KEYBOARD_TEXT` to `Shezarrine` (override with
`--character-name`) for the locally patched XRizer runtime. That opt-in path
returns the configured text and emits OpenVR `KeyboardDone` after Skyrim asks
for its virtual keyboard. XRizer preserves its normal unsupported-keyboard
behavior when the variable is absent.

The default window expressions are anchored to the exact `Skyrim VR` and
`Monado!` titles. This prevents browser tabs or other windows containing the
word Skyrim from receiving automation input. Do not substitute Monado `O` for
the RaceSex Done action: Qwerty devices negotiate
`/interaction_profiles/khr/simple_controller`, whose legacy Grip binding comes
from `/input/menu/click` (`N`), not the Qwerty WMR squeeze input (`O`). Confirm
the negotiated profile in `stvr-devbench-launch.log` when changing runtimes.

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

Synthetic keyboard taps are held for 500 ms. Short taps were intermittently
missed by Skyrim VR and XRizer even when the correct window had focus.

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
