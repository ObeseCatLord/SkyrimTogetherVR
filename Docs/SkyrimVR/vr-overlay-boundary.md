# VR Overlay Boundary

SkyrimTogetherVR does not use the normal Skyrim Together flat CEF/D3D11 overlay in the default VR build.

## Default Boundary

The default SkyrimTogetherVR target builds in Connection-only mode. That mode
keeps transport, session, discovery, player-cell synchronization, and the
file-based control surface active. Pose and gameplay observation relays are
reserved for explicit broader targets. It does not instantiate `OverlayService`,
`InputService`, `DebugService`, `RenderSystemD3D11`, or `BehaviorVar` patching.

The old flat CEF/D3D11 overlay path depends on desktop swap-chain and Win32 input assumptions. Those assumptions are not validated for Skyrim VR, SteamVR compositor presentation, VRIK/HIGGS/PLANCK input ownership, or VR menu behavior, so they stay outside the default build until a real VR overlay path is designed and checked against VR runtime behavior.

The explicit gameplay package still constructs the normal gameplay services, including `OverlayService`, because other gameplay systems expect that service to exist. Since `TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY=0`, the overlay app and render system are never created in that package; the overlay service is a no-op flat overlay service and guards every CEF call when `m_pOverlay` is absent. Debug update and draw paths also guard missing desktop render-window pointers. `InstallHooks2()` also skips the DirectInput overlay hook whenever the VR target is connection-only or flat overlay is disabled.

## Renderer Init

The renderer bring-up hook is not installed in the default VR target. Its
generic address row did not provide enough semantic evidence to justify an
otherwise unnecessary detour. The dormant implementation remains available
only behind the unvalidated-hook build gate for future renderer research.

Any future in-game overlay work should be a separate VR implementation. It should not reuse the desktop WndProc and flat swap-chain hook by default.

## Companion

The current external control surface is the local browser companion panel served by `Tools/SkyrimVR/vr_handoff.py serve` and launched by `LaunchSkyrimTogetherVRCompanion.bat` or `SkyrimTogetherVR.exe --companion`.

The Companion reads the staged file bus under `Data/SkyrimTogetherReborn`, shows connection, pose, movement, equipment, activation, magic, combat, projectile, grab, HIGGS, save/load, and remote-player readouts, and writes connect/disconnect commands through the same file contract as `VRConnectionService`.

It intentionally stays outside Skyrim's input and rendering stack. It is suitable for desktop browser use, SteamVR desktop view, and packaged launcher handoff while the in-game overlay remains gated.

## Papyrus

`SkyrimTogetherVRConnectionMenu` is retained as deferred UI source, but the
default VR package does not register the inherited flat-client native ABI and
does not grant its spell. The supported controller/input-neutral control
surface is the companion/file handoff until a separate native-ABI port is
reviewed.

## Audit

`Tools/SkyrimVR/audit_vr_overlay_boundary.py` locks down this boundary. It checks that the default VR build enables connection-only mode, keeps unvalidated hooks and validated inline patches off, avoids flat overlay services in the connection-only world construction path, does not install the renderer-init hook from the default main path, and keeps gameplay-package overlay calls guarded when the flat overlay app is absent. The explicit gameplay build turns connection-only mode off, but still sets `TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY=0`, so it also skips flat D3D overlay startup by default.

The audit also checks the companion, Papyrus, and documentation tokens that define the supported VR-safe overlay/input surfaces.
