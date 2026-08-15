# Local Linux Multi-Client Testing

Two simulated Skyrim Together VR clients can run under one Linux login when
their writable state is isolated. Each client needs its own Skyrim VR game
tree, Proton compatdata/prefix, caches and logs, transient user-service cgroup,
and Monado IPC runtime. Sharing any of those locations can corrupt the prefix,
overwrite the plugin's live control files, or connect both games to the same
compositor.

The checked-in helpers provide that isolation without deleting existing game
or client data:

- `manage-local-clients.sh` prepares, launches, reports, and gracefully stops
  named client copies.
- `manage-monado-instance.sh` starts one transient user service and one
  `monado_comp_ipc` socket per name.
- Both managers serialize `prepare`, `launch`, `stop`, `start`, and `restart`
  transitions with a per-name `flock`.
- Transient units are accepted only when the instance marker, unit environment,
  and systemd `InvocationID` all match. They use `KillMode=control-group` and
  `SendSIGKILL=no`; no operation infers ownership from a leader PID.
- Both launchers require the exact non-symlink
  `STVR_MONADO_RUNTIME_DIR/monado_comp_ipc` path, a matching validated
  `XR_RUNTIME_JSON`, and the persisted Monado prefix/library settings.

## Capacity and Limits

Preparation makes an independent copy with `cp --reflink=auto` while preserving
intra-tree hard links. On btrfs or reflink-capable XFS this is initially cheap.
The current Skyrim VR library is on NTFS, so budget approximately one full game
plus one Proton prefix per client. The helper checks the preserved-link
worst-case source size plus a 2 GiB reserve before copying. It prepares inside
a marker-owned staging directory and atomically renames it into place only
after all metadata is written; source/root ancestry is rejected to prevent a
recursive staging copy. Put `STVR_CLIENT_ROOT` on the game volume, not the
nearly full Linux root filesystem. Keep the resulting IPC path at 107 bytes or
fewer; long client-root paths are rejected before startup.

Two Monado Qwerty windows can coexist, but keyboard input goes to the focused
window. Rendering two Skyrim VR processes can saturate the GPU and system RAM.
This is useful for protocol and synchronization smoke tests; two physical
headsets/controllers generally require separate machines and sessions.

## Prepare Once

From the repository root:

```bash
export STVR_CLIENT_ROOT=/home/obesecatlord/FasterGames/SteamLibrary/steamapps/common/SkyrimTogetherVR-local-clients
export STVR_BASE_GAME_DIR=/home/obesecatlord/FasterGames/SteamLibrary/steamapps/common/SkyrimVR
export STVR_BASE_COMPATDATA=/home/obesecatlord/FasterGames/SteamLibrary/steamapps/compatdata/611670

Tools/SkyrimVR/linux/manage-local-clients.sh prepare player1
Tools/SkyrimVR/linux/manage-local-clients.sh prepare player2
```

The source game and prefix must already be configured and working. Preparation
does not replace an existing named client. It moves copied
`Data/SkyrimTogetherReborn/SkyrimTogetherVR.*` live files into that client's
`stale-runtime-seed` directory so old PIDs, epochs, or commands cannot leak
into a new run.

## Launch and Connect

```bash
Tools/SkyrimVR/linux/manage-local-clients.sh launch player1 incidentalstoat.xyz:26099
Tools/SkyrimVR/linux/manage-local-clients.sh launch player2 incidentalstoat.xyz:26099

Tools/SkyrimVR/linux/manage-local-clients.sh status player1
Tools/SkyrimVR/linux/manage-local-clients.sh status player2
```

Each launch starts its matching Monado instance unless `--no-manage-monado` is
supplied. The launch log and plugin runtime logs stay under the named client's
`logs` directory. The Monado manager persists the resolved prefix, OpenXR
manifest, library path, and absolute host desktop endpoints in the named
runtime state. Launch units pass that same `XR_RUNTIME_JSON`, library path, and
pressure-vessel mounts to UMU with `GAMEID=umu-611670`. When the private Monado
runtime replaces `XDG_RUNTIME_DIR`, absolute Wayland, DBus, PulseAudio, and
PipeWire endpoints are retained so the compositor and audio session remain
reachable. Focus the matching Monado window before sending simulated controller
input.

To use Monado instances that were started separately:

```bash
Tools/SkyrimVR/linux/manage-monado-instance.sh start player1
Tools/SkyrimVR/linux/manage-monado-instance.sh start player2
Tools/SkyrimVR/linux/manage-local-clients.sh --no-manage-monado launch player1 HOST:PORT
Tools/SkyrimVR/linux/manage-local-clients.sh --no-manage-monado launch player2 HOST:PORT
```

Use `STVR_MONADO_SERVICE` to select another `monado-service` binary. The manager
prefers the Envision `simulated_default` build on this host and otherwise uses
`monado-service` from `PATH`. Existing environment overrides for Qwerty and XRT
settings are preserved.
The manager enables Monado's `XRT_NO_STDIN=1` service mode because transient
systemd units do not provide epoll-compatible interactive stdin; shutdown is
still handled by `SIGTERM` from systemd.

## Stop

```bash
Tools/SkyrimVR/linux/manage-local-clients.sh stop player1
Tools/SkyrimVR/linux/manage-local-clients.sh stop player2
```

Stop addresses only the recorded, marker/environment/InvocationID-validated
transient client cgroup and waits up to the bounded configured timeout before
stopping that client's Monado service. If the cgroup remains active, the helper
returns failure and leaves Monado running; it does not escalate to `KILL`. The
scripts deliberately provide no delete or reset command.

## Tooling Checks

These checks do not launch Skyrim:

```bash
Tools/SkyrimVR/linux/manage-monado-instance.sh --self-test
Tools/SkyrimVR/linux/tests/test-monado-instance.sh
Tools/SkyrimVR/linux/tests/test-local-clients.sh
Tools/SkyrimVR/linux/tests/test-local-client-units.sh
Tools/SkyrimVR/linux/tests/test-launcher-runtime.sh
```

They run in Linux CI. The fake-systemd fixtures cover ownership, cgroup leader
loss, concurrent transitions, symlink/orphan sockets, timeout cleanup, and UMU
environment construction. A green result proves validation and isolation
behavior, not that two gameplay clients reached the server.
