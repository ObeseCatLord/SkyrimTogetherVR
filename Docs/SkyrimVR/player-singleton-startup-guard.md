# Player Singleton Startup Guard

## Failure Evidence

The default VR launcher reached the connection-only service constructors under
Linux/Proton and Monado, then crashed with `0xc0000005` at `0x18060cbcf`.
The matching Windows PDB locates that address inside
`VRSaveLoadService::VRSaveLoadService`. The faulting instruction dereferenced a
non-null player singleton before Skyrim VR had made its page readable.

The address is in the launcher image's `.zdata` section, not `libcef.dll`.
CEF delay loading is retained as unrelated packaging hardening.

## Guard

`Games/Skyrim/VR/VRPlayerReadiness.h` adds
`TryGetReadablePlayerForVR()`. It obtains the existing singleton, then uses
`VirtualQuery` to require a committed readable page and reject guard/no-access
pages. It returns `nullptr` while the singleton is unusable and logs once
without recording the address.

The default connection-only startup/first-update paths in discovery, connection,
movement, inventory, save/load, pose capture, and delayed player status use this
helper. Their existing null handling then defers player-derived telemetry and
automatic connection until a readable player exists. The default package builds
with `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=1`; full gameplay mode remains outside
this narrow startup fix.

This is a mapped-page filter, not proof that an object has the expected Skyrim
VR layout, lifetime, or nested pointers. It must not be used to justify
unvalidated gameplay hooks.

## Rebuild Gate

This source change requires a new Windows default package. Before installation,
the package must pass its existing built-package audit. The first Linux runtime
check must show:

1. The one-time `player singleton is not readable yet` warning, if startup is
   early enough to encounter the sentinel.
2. The save/load and connection handoff files written without a crash.
3. Connection telemetry after a readable player becomes available.

Do not enable avatar-sync or gameplay mode based on this guard alone.
