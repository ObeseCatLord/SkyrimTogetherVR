# SKSEVR/VRIK startup review brief

## Goal

Choose a correct SKSEVR bootstrap lifecycle for the manually mapped Skyrim VR
executable used by SkyrimTogetherVR. The result must allow the official SKSEVR
native-plugin lifecycle to load VRIK, HIGGS, PLANCK, and the STVR bridges.
This is a solo-user port; favour a narrow, debuggable correction over a broad
launcher rewrite.

## Verified facts

| Fact | Evidence |
| --- | --- |
| The launcher maps `SkyrimVR.exe`, calls `RunTiltedInit`, calls `LoadScriptExender`, then invokes `LC->gameMain`. | `Code/immersive_launcher/Launcher.cpp:172-193` at commit `6e04eb9e`. |
| `LoadScriptExender` synchronously calls `LoadLibraryW(sksevr_1_4_15.dll)`. | `Code/client/ScriptExtender.cpp:119-165`. |
| The current Windows build contains commit `6e04eb9e`, was installed, and the launch began successfully under UMU/GE-Proton. | Package SHA-256 `58ec1ea16720f21c4dd66505357fb9f8a07497e948768fbed24d5ca57e3d9ddd`; `logs/stvr-launch-20260712T054741Z.log`. |
| SKSEVR 2.0.12 begins loading plugins and successfully loads PLANCK, HIGGS, and all three STVR bridges. | `compatdata/611670/.../Documents/My Games/Skyrim VR/SKSE/sksevr.log` from the current launch. |
| The same log stops exactly after `checking plugin ...\\VRIK.dll`; it has no `loaded correctly` line for VRIK. | Same `sksevr.log`. |
| Updating from a partial VRIK 0.8.2 deployment to a locally installed, complete VRIK 0.8.5 release did not change the stop point. | Target VRIK 0.8.5 DLL SHA-256 `9676988299747f3dc4c7830a55c125d2447f62931511f86c8c6b0a3aabf054d7`; its assets and settings were copied from `Games/Nordic Adventures - High Fantasy Beta 2.01/mods/VRIK Player Avatar`; current SKSEVR log remains stuck at the VRIK check. |
| The active VRIK ESP is enabled before SkyrimTogether ESP. | `compatdata/611670/.../AppData/Local/Skyrim VR/plugins.txt`. |
| The dedicated Tilted server is healthy and is unrelated to this client-side pre-game stall. | `foundry` Docker container `skyrim-together-vr` reported running. |
| The STVR client log reaches `RunTiltedInit` but does not reach client startup. | `Data/SKSE/Plugins/tp_client.log` entries at `2026-07-12 00:47:47`. |
| No fresh CrashLogger dump was emitted. Native GDB only exposes Wine wait frames, so it cannot attribute the Windows-side wait. | Current run observation on Linux. |

## Important observations

* VRIK 0.8.5 is the current local release. It declares `VRIK V0.8.5 loading`
  in the DLL. VRIK's official page requires SKSEVR and Skyrim VR.
* Its own fresh log is never written. The existing `vrik.log` is from VRIK
  0.8.2, so the block occurs during DLL loading or before VRIK's first log call.
* The STVR VRIK bridge itself successfully loads before VRIK. Its prior review
  found no eager VRIK interface use; it subscribes to a post-load messaging
  point. Treat this as strong but not conclusive.

## Open decision

### Where/how to bootstrap SKSEVR

**Review disposition:** adopted a synchronous helper-thread bootstrap with a
strict pre-`gameMain` barrier. The helper receives the mapped Skyrim VR TLS
template before calling `LoadLibraryW`; the launcher logs and aborts after a
finite timeout rather than entering the mapped game with an incomplete SKSEVR
bootstrap. A delayed hook was rejected as too late, and self-remote injection
was rejected as needless complexity for an already in-process mapper.

| Review recommendation | Disposition | Implementation |
| --- | --- | --- |
| Separate bootstrap thread with a strict pre-game barrier | Adopted | `BootstrapScriptExtenderOnLoaderThread()` waits for one helper result before `LC->gameMain()`. |
| Apply the mapped game TLS template to the helper | Adopted | `ExeLoader::ApplyMappedTlsToCurrentThread()` copies the immutable mapped template after a capacity check. |
| Protect against TLS-size mismatch | Adopted | The template is rejected when it exceeds `TlsMemory.cpp`'s reserved slot; the helper logs both sizes. |
| Avoid live work after a timeout | Adapted | The timeout flushes logging and terminates the failed process, since safely cancelling `LoadLibraryW` is not possible. |
| Defer injection or emulate a remote process | Rejected | Both weaken the native-hook ordering or add no useful behavior to the in-process mapper. |

Options:

1. **Deferred bootstrap from a safe hook after `LC->gameMain` begins**: install
   the STVR start hook first, then load SKSEVR only from a proven early runtime
   callback. Rejected unless the callback runs before SKSEVR's required engine
   patches; late injection can make plugin hooks unsafe.
2. **Bootstrap on a helper thread while allowing `LC->gameMain` to proceed**:
   close a possible main-thread wait cycle, but preserve a synchronization point
   that keeps STVR's own SKSE-dependent features from starting too early.
3. **Recreate official suspended-process/remote-thread injection behavior**:
   likely closest semantically but may be incompatible with the existing
   in-process manual mapper and substantially increase launcher complexity.
4. **Do nothing and omit VRIK**: rejected. VRIK compatibility is an explicit
   port requirement and the launch must retain it.

Potential overlap: options 1 and 2 may both be variants of "enter game main
first, then inject on an independent thread". The reviewer may merge them if
that better reflects the actual SKSEVR lifecycle.

## Questions for review

1. Given official SKSE loader architecture and the manual mapper, which option
   is technically sound enough to implement and why?
2. What minimal source changes and tests/logging should accompany the fix so a
   single Windows rebuild gives useful evidence?
3. Could the VRIK bridge ordering itself realistically be the cause? If yes,
   specify a bounded way to prove/fix it without disabling VRIK in the release.
4. Identify any safety problem in the current synchronous `LoadLibraryW` call
   that prior reviews missed.

## Scope and non-goals

Do not review broad SkyrimTogether networking, server behavior, game-play sync,
or mod deployment. Do not modify files. Focus only on the SKSEVR native-plugin
startup boundary and the VRIK/HIGGS/PLANCK compatibility requirement.
