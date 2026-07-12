# Senior Review Brief: SKSEVR/VRIK Loader Stall After Bootstrap Fix

## Goal and scale

Make the Skyrim Together VR launcher load SKSEVR and the user's required VR
plugins, including VRIK, under Proton so the user can proceed to in-game VR
testing. This is a solo-operator port. The immediate decision is the smallest
correct next change or diagnostic after the latest Windows rebuild still stalls
while SKSEVR loads `VRIK.dll`.

This is not a request to redesign Skyrim Together gameplay synchronization or to
relax compatibility requirements by excluding VRIK, HIGGS, or PLANCK.

## Verified evidence

| Fact | Status and evidence |
| --- | --- |
| The current source commit is `f0ed216d fix(vr): bootstrap SKSEVR on mapped TLS thread`. | [verified: `git log -1 --oneline`, 2026-07-12] |
| The rebuilt Windows `SkyrimTogetherVR.exe` is deployed to the actual Skyrim VR directory. | [verified: SHA-256 is `6b86ee82533dec45be9c263b665621dcda77a37c5f5620c3bfac21e89fd98abf` for both `artifacts/SkyrimTogetherVR/packages/default/SkyrimTogetherVR.exe` and `<SkyrimVR>/SkyrimTogetherVR.exe`, 2026-07-12] |
| The deployed EXE contains the bootstrap-helper/TLS diagnostic strings introduced by `f0ed216d`. | [verified: `strings` scan of current package before install, 2026-07-12] |
| The latest launcher run executes the helper on a new thread, applies mapped TLS to that thread, and enters `LoadScriptExender`. | [verified: `<SkyrimVR>/logs/tp_client.log`, 2026-07-12 12:52:30, exact entries quoted below] |
| The helper never returns within 60 seconds and the launcher terminates the process by design. | [verified: same log, 2026-07-12 12:53:30; `Launcher.cpp:93-99`] |
| SKSEVR reaches its native-plugin scan, loads ActiveRagdoll/PLANCK, HIGGS, and all SkyrimTogether bridge DLLs, then stops at `checking plugin ... VRIK.dll`. | [verified: latest `sksevr.log` captured immediately after the prior run; no subsequent `loaded correctly` line for VRIK] |
| The target uses a full VRIK 0.8.5 installation, not only its DLL. | [verified: `<SkyrimVR>/Data/SKSE/Plugins/VRIK.dll` SHA-256 `9676988299747f3dc4c7830a55c125d2447f62931511f86c8c6b0a3aabf054d7`; VRIK ESP, scripts, meshes, textures, INI, and PDB are present; `plugins.txt` enables `vrik.esp`] |
| VRIK does not emit a fresh VRIK log during the stalled run. | [verified: target VRIK log records the former 0.8.2 run only; no newer entry after full 0.8.5 install] |
| The launcher is a manual mapper, not a conventional `SkyrimVR.exe` process: `ExeLoader` maps the game image at `0x140000000`; its stubs hook module/file-name/loader APIs to present the mapped image as the game. | [verified: `Code/immersive_launcher/loader/ExeLoader.cpp`; `Code/immersive_launcher/stubs/FileMapping.cpp`] |
| Stubs are installed before launcher startup code invokes SKSEVR. | [verified: `Code/immersive_launcher/Main.cpp:56-57` calls `CoreStubsInit()` before the later startup path] |
| The loader hooks `LdrLoadDll` and `LdrGetDllHandleEx`; an intended `LdrGetDllFullName` hook is implemented but deliberately not installed. | [verified: `FileMapping.cpp:138-156`, `285-289`] |
| Official SKSEVR's normal loader starts SkyrimVR suspended, injects SKSEVR using a remote `LoadLibrary`, waits, then resumes the real executable's main thread. | [verified: source from `sksevr_2_00_12.7z`, inspected as `sksevr_loader_main.cpp` and `inject.cpp`] |
| The earlier main-thread pre-entry bootstrap also stalled at VRIK. The latest helper-thread/TLS variant reproduces the same failure. | [verified: timestamped launcher and SKSEVR logs from both builds] |
| The server is healthy and unrelated to the failure: the hang occurs before the game starts and before any client connection attempt. | [verified: server container healthy; launch logs stop in SKSEVR load] |

Latest client log:

```text
[2026-07-12 12:52:30.495] ... SKSEVR bootstrap helper started (thread 400)
[2026-07-12 12:52:30.496] ... applied mapped Skyrim VR TLS (thread 400)
[2026-07-12 12:52:30.496] ... entering LoadScriptExender
[2026-07-12 12:53:30.496] ... helper timed out after 60 seconds
```

The runner is now stopped after the timeout; no test process remains. This is
intentional and prevents an orphaned process.

## Code topology relevant to the decision

* `Code/immersive_launcher/Main.cpp` preloads selected Windows DLLs, installs
  `CoreStubsInit()`, then starts the custom launcher.
* `Code/immersive_launcher/loader/ExeLoader.cpp` copies the target PE over the
  current executable image at the normal Skyrim base and resolves imports.
* `Code/immersive_launcher/Launcher.cpp` currently calls `LoadScriptExender()`
  on a joined helper thread after `ExeLoader::ApplyMappedTlsToCurrentThread()`;
  it does not enter the mapped game image until SKSEVR reports success.
* `Code/immersive_launcher/stubs/FileMapping.cpp` interposes
  `GetModuleHandle*`, `GetModuleFileName*`, `LdrGetDllHandle*`, and
  `LdrLoadDll`. It has a `TP_LdrGetDllFullName` implementation, but the MinHook
  registration is commented out.
* SKSEVR's PluginManager calls `LoadLibrary` while the SKSEVR DLL is initialized;
  VRIK is reached after the two physics plugins and the alphabetically earlier
  SkyrimTogether bridge DLLs.

## Claims not yet established

* [unknown] Whether VRIK's loader-lock-time initialization itself needs an
  unmodified process/PEB/LDR entry for the main executable, a correct
  `LdrGetDllFullName` answer, a per-thread TLS setup beyond the copied TLS
  block, or another loader invariant absent with manual mapping.
* [unknown] Whether a bridge DLL's early initialization contributes. The VRIK
  bridge is loaded immediately before VRIK and starts a writer in its
  `SKSEPlugin_Load`, but HIGGS/PLANCK bridges also load first and no evidence
  yet shows causality.
* [unknown] Whether VRIK waits on a dependency loaded by `LdrLoadDll` whose
  request is altered by the custom policy hook.
* [unverified] A VRIK PDB is available locally and could identify TLS callbacks
  or static constructors; this has not yet been traced to an exact wait site.

## Decisions for review

### 1. Identify the highest-leverage next step

**Current lean:** add instrumentation sufficient to identify the load-time wait
or recursively requested module before changing the module-identity contract.

Options:

1. Add narrowly scoped loader instrumentation around `TP_LdrLoadDll` and/or
   VRIK import/constructor path to find the precise stalled call.
2. Correct a known manual-mapper identity invariant now, most plausibly the
   `LdrGetDllFullName` behavior or a PEB/LDR module entry for SkyrimVR.
3. Do a reversible runtime isolation test by disabling only
   `SkyrimTogetherVRVrikBridge.dll`, then restore it. This distinguishes a
   bridge interaction but does not meet the final compatibility goal.
4. Replace the custom mapping strategy with a conventional SkyrimVR process and
   injection flow.

**Rejected for now:** removing VRIK or declaring VRIK unsupported contradicts
the user's explicit compatibility requirement; guessing a loader fix without
observability risks another Windows rebuild with no diagnostic value. Replacing
the mapping strategy is a broad architectural rewrite and should be justified
by strong evidence.

### 2. Assess whether the current bootstrap architecture is valid

**Current lean:** the helper's execution context is still suspect because
SKSEVR's ordinary path executes on the game process's initial thread before
entrypoint resume, whereas ours is a `std::thread` in the manual-mapped host.
The new TLS helper disproves only the copied-TLS-block hypothesis, not the
entire process/thread/loader contract.

Options:

1. Keep helper-thread bootstrap and repair only its missing invariants.
2. Invoke SKSEVR on the original process thread at the correct point while
avoiding the original TEB/TLS problem.
3. Make the manual mapper more closely emulate a real SkyrimVR image in loader
metadata and retain helper bootstrap.

**Rejected for now:** moving back to the old pre-entry main-thread bootstrap
without another contract change merely repeats a known stall.

### 3. Decide whether a bridge-isolation run is justified before a source change

**Current lean:** acceptable only as a reversible diagnostic after the review,
because it is fast, preserves VRIK itself, and can distinguish a direct STVR
bridge reentrancy defect from the general manual-mapper issue. It must not be
treated as a product workaround.

**Suspected overlap:** decisions 1 and 2 may be one root cause: VRIK may expose
an incomplete manual-mapping process identity rather than a separate TLS issue.
It is in scope to merge them.

## Requested review output

Verify the code and current evidence before critique. Return a prioritized,
evidence-backed recommendation that includes:

1. the likely failure family and why it explains the exact log boundary;
2. the next action, ranked between a no-source runtime diagnostic and a source
   change, with exact files/functions for any source change;
3. any relevant missing invariants in the manual mapper / Windows loader /
   SKSEVR startup contract;
4. failure modes the current plan missed, especially loader-lock, TLS, PEB/LDR,
   Proton/Wine, and VRIK/HIGGS/PLANCK interactions;
5. whether the latest build is useful enough to leave installed while we make
   the next diagnostic.

Do not edit files, launch the game, deploy anything, alter the server, or broaden
into gameplay-sync architecture. Cite paths and lines where practical. The
review should favor a single high-information next step over speculative broad
work.

## Review Disposition

| Senior recommendation | Disposition | Rationale |
| --- | --- | --- |
| Treat the failure as a nested loader-lock stall, not a generic VRIK, server, or gameplay failure. | Adopted | The latest SKSEVR log stops immediately before VRIK's `LoadLibrary`; `SKSEPlugin_Query`/`Load` cannot have run. |
| Exclude every SkyrimTogether bridge DLL for one reversible control run. | Adopted | Each of the three bridge `SKSEPlugin_Load` implementations calls `StartWriter()` (`Code/{higgs,planck,vrik}_bridge/main.cpp`), so isolating only the alphabetically adjacent VRIK bridge would not test the shared lifecycle defect. |
| Revert to bootstrap on the mapped primary thread. | Rejected | Official SKSEVR uses a remote helper thread while the game primary thread is suspended. The current helper topology is directionally correct, and the previous primary-thread attempt already stalled at the same boundary. |
| Enable `TP_LdrGetDllFullName` as a speculative identity repair. | Rejected | The current hook does not honor the native output-buffer contract and its paired handle hook has a length-unit defect. Enabling it now would add an uncontrolled loader behavior change with no evidence that it is the VRIK stall. |
| If bridges are exonerated, add a loader trace before changing mapper identity behavior. | Adopted conditionally | A failed control run will leave the bridge files restored and make an allocation-free `TP_LdrLoadDll` trace, flushed outside loader lock, the next source change. |
| Remove loader-lock lifecycle work if the control run implicates the bridges. | Adopted conditionally | A source change would defer interface acquisition and writer startup until a proven post-entry notification, and remove static shutdown joins. This needs a fresh Windows rebuild after review, not before the control result. |

## Control Result and Next Rebuild

The all-bridge control run was completed after the review. SKSEVR loaded PLANCK
and HIGGS, skipped all three `SkyrimTogetherVR*Bridge.dll` files, and stopped at
`VRIK.dll` at exactly the same point. The three bridge files were immediately
restored. This falsifies the bridge lifecycle as the direct cause of the current
stall.

A WineDbg process enumeration and early attach succeeded, but Wine did not
surface an interrupt stop before the launcher's intentional 60-second timeout.
It did not yield a usable Windows-side stack. The next source change is therefore
the adopted bounded diagnostic: record `LdrLoadDll` entry/return requests in a
fixed-capacity, allocation-free ring and emit the records from the existing
timeout thread. It changes neither module policy nor mapper identity behavior.

Fresh trace-patch review disposition: accepted the reviewer’s compiler-correct
mutable snapshot correction and added an explicit fixed-table saturation counter
to the timeout output. Retained fixed capacities and signed `LONG` sequencing:
the trace is scoped to one bounded 60-second startup run, where counter wrap is
not credible and dynamic growth would violate the loader-lock design constraint.
