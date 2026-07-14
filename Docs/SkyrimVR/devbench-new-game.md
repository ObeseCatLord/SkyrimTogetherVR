# DevBench New Game Automation

`Tools/SkyrimVR/devbench_new_game.py` drives the Linux/Proton smoke-test path
without headset input:

1. waits for DevBench on `127.0.0.1:8921`;
2. dismisses Skyrim VR calibration when present;
3. creates or reuses a persistent `ydotoold` keyboard before Proton starts;
4. focuses Skyrim with `kdotool`, selects the top New Game entry with `End`
   (Skyrim VR's Scaleform list is indexed bottom-to-top), and sends the VR
   trigger alias to Monado's Qwerty controller;
5. requests RaceSex completion with `R`, verifies the affirmative dialog, and
   activates it through a held Monado controller trigger;
5a. while finalizing and waiting to connect, scans for `MessageBoxMenu`; only
   the verified Realm of Lorkhan intro is accepted via `menu accept` when its
   body matches `someplace unknown` and `outside of time and space` and its sole
   non-cancel button is `Begin`; any other modal blocker is reported as a hard
   failure instead of stalling;
6. waits for Realm of Lorkhan and verifies the finalized player and active
   `SkyrimTogether.esp`, then requires two stable player/cell snapshots and
   client `playercell ready=1` before closing any stranded RaceSex UI;
7. before connecting, requires the dialog to remain closed across two polls and
   observes two newer successful SKSE task sequences; then writes one connection
   command and waits for fresh online status with a nonzero player ID plus the
   first current-cell request.

Requirements are DevBench 1.9.1, `kdotool`, `ydotool`/`ydotoold`, write access
to `/dev/uinput`, and the Monado Qwerty aliases from
`Tools/SkyrimVR/monado-qwerty-automation.patch`. The patch maps `P` to the
focused controller trigger and `O` to squeeze because synthetic pointer
positioning is not reliable on multi-monitor Wayland.

The default window expressions are anchored to the exact `Skyrim VR` and
`Monado!` titles. This prevents browser tabs or other windows containing the
word Skyrim from receiving automation input.

For an unattended run, let the automation launch Skyrim after its persistent
input device is ready:

```bash
Tools/SkyrimVR/devbench_new_game.py \
  --launch-game \
  --skyrim-vr "/path/to/SkyrimVR" \
  --connect incidentalstoat.xyz:26099
```

Connection verification intentionally requires `--launch-game`. The driver
removes inherited `STVR_AUTOCONNECT` and `STVR_PASSWORD` values and launches a
fresh offline process so an older in-memory connection cannot satisfy the test.

For a simulated headset that cannot deliver the RaceSex finish action, add
`--allow-finalized-racesex`. This connection-smoke fallback is accepted only
when DevBench independently sees a loaded, named, raced player already placed
in `RealmLorkhan`. It leaves the stranded RaceSex presentation open and proves
network admission only; it does not claim that the unavailable VR naming flow
completed. If the Papyrus cadence pauses at the post-character renderer
transition, the fallback pumps the registered native `Tick` through DevBench
until fresh current-process task sequences, online status, and cell sync are
observed.

The automation never treats hiding `RaceSex Menu` as successful character
creation. XRizer cannot display Skyrim VR's SteamVR naming keyboard, so the menu
can remain stranded after real controller activation. The script closes that
presentation only after independently proving Realm of Lorkhan placement, a
named/raced player, and the active Skyrim Together plugin.

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
