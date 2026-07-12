# Next Windows Rebuild: VRIK Loader Trace

## Why this rebuild is required

The current deployed binary `6b86ee82533dec45be9c263b665621dcda77a37c5f5620c3bfac21e89fd98abf`
contains the helper-thread/TLS bootstrap fix. It is correctly installed, but it
still times out while SKSEVR is inside `LoadLibrary(VRIK.dll)`.

The source now adds a diagnostic only. It records every `LdrLoadDll` request in
fixed storage and writes the trace from the existing 60-second timeout path. It
does not change VRIK, HIGGS, PLANCK, bridge loading, DLL policy, or mapper
identity behavior. A new Windows package is therefore required before another
startup run can provide the missing observation.

## Build

From an x64 Visual Studio Developer Command Prompt in the repository root:

```bat
BuildAndAuditSkyrimTogetherVR-Windows.bat
```

Use the resulting `artifacts\SkyrimTogetherVR\packages\default` package only.
Do not install the avatar-sync, gameplay, or DLL-only packages for this loader
diagnostic.

## Install and run

Install the default package with the existing package installer, preserving the
complete VRIK 0.8.5, HIGGS, PLANCK, and all three
`SkyrimTogetherVR*Bridge.dll` files. Do not rename or remove any plugin for the
next run. The bridge-only control has already been run and did not change the
VRIK stall.

Launch through `launch-skyrim-together-vr.sh`. The expected run still ends at
the intentional 60-second timeout unless the underlying stall is resolved.

## Required evidence

Retain these logs from the same run:

* `Data\SKSE\Plugins\tp_client.log`, including every
  `SkyrimTogetherVR LdrLoadDll trace:` line after the timeout message.
* `Documents\My Games\Skyrim VR\SKSE\sksevr.log`, which establishes the last
  plugin SKSEVR reached.

The most important trace entry will normally have `phase=pending` and an
elapsed time near 60,000 ms. Its `module`, `thread`, `depth`, and `callsite`
will distinguish a direct VRIK initialization wait from a nested dependency or
loader-hook recursion. A `trace retained the most recent 256 requests` line is
informational; a `could not assign a depth slot` line means only the nesting
depth is unavailable for that request.

Do not interpret a clean package audit as proof that VRIK initialized: the
runtime evidence above remains required.
