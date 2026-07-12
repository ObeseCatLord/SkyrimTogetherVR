# Senior Review Brief: VRIK Stall After Dependency Resolution

## Goal and scale

Continue the solo-operator Skyrim Together VR startup port by resolving the
SKSEVR/VRIK startup stall under the custom mapped launcher. The next decision is
the smallest safe observation or implementation after a fresh Windows build
captured the exact `LdrLoadDll` trace.

Do not broaden this review into gameplay synchronization, networking, UI, or
removal of VRIK/HIGGS/PLANCK support. VRIK compatibility is required.

## Environment facts

| Fact | Status and evidence |
| --- | --- |
| Source diagnostic commit is `66a579b5 diag(vr): trace stalled native plugin loads`. | [verified: `git log -1`, 2026-07-12] |
| A Windows rebuild of that source completed and the exact package EXE was installed. | [verified: package and installed EXE SHA-256 `586541e2a7e1d02b39737074db5c229bb79b6150b4d7e58e16261a4ba6566dd9`; package audit passed] |
| The package includes the new `LdrLoadDll trace` strings. | [verified: `strings` on package and installed EXE] |
| Full VRIK 0.8.5, HIGGS, PLANCK, SKSEVR 2.0.12, and VR Address Library are installed and passed strict package prerequisite audit. | [verified: `audit_built_package.py --require-installed-prerequisites --require-vrik --require-higgs --require-planck`] |
| All three STVR bridge DLLs were enabled in the trace run. | [verified: current target file list; SKSEVR log loaded each bridge before VRIK] |
| The only server is healthy and unrelated. | [verified: the client reaches the local SKSEVR native plugin scan before any connection path] |
| No client/game/debugger process remains after the bounded run. | [verified: process list after timeout] |

## Fresh runtime evidence

The new run at `2026-07-12 13:33:02` executed the TLS-prepared helper and
entered `LoadScriptExender`. At `13:34:02` it timed out by design and flushed
the diagnostic trace to `<SkyrimVR>/logs/tp_client.log`.

SKSEVR log order:

```text
activeragdoll.dll loaded correctly
higgs_vr.dll loaded correctly
SkyrimTogetherVRHiggsBridge.dll loaded correctly
SkyrimTogetherVRPlanckBridge.dll loaded correctly
SkyrimTogetherVRVrikBridge.dll loaded correctly
checking plugin ... VRIK.dll
```

Key trace records, all on helper Windows thread 400:

```text
sequence=50 depth=1 phase=pending elapsedMs=59990 module=...\\sksevr_1_4_15.dll
sequence=57 depth=2 phase=returned elapsedMs=3  module=...\\activeragdoll.dll
sequence=64 depth=2 phase=returned elapsedMs=14 module=...\\higgs_vr.dll
sequence=71 depth=2 phase=returned elapsedMs=12 module=...\\SkyrimTogetherVRHiggsBridge.dll
sequence=77 depth=2 phase=returned elapsedMs=2  module=...\\SkyrimTogetherVRPlanckBridge.dll
sequence=83 depth=2 phase=returned elapsedMs=2  module=...\\SkyrimTogetherVRVrikBridge.dll
sequence=89 depth=2 phase=pending elapsedMs=59262 module=...\\VRIK.dll
sequence=90-94 depth=3 phase=returned elapsedMs=0 module=api-ms.../kernel32
```

The trace callsite is the same loader trampoline address for every record. It
does not prove which VRIK instruction is blocked, but proves that VRIK's
dependency resolver returned and the outer `LoadLibrary(VRIK.dll)` remains
inside its loader-time execution.

## Prior evidence retained

* [verified previously] Excluding every `SkyrimTogetherVR*Bridge.dll` from the
  SKSE scan did not change the VRIK stop point. The files were restored.
* [verified previously] Moving SKSEVR startup to the TLS-prepared helper thread
  did not resolve the stall.
* [verified previously] VRIK's PDB/DLL show a small static TLS template, no TLS
  callback, a trivial user `DllMain`, and only KERNEL32/USER32 static imports.
  This does not exclude CRT/static-constructor work before the exported SKSE
  functions become callable.
* [verified previously] VRIK loaded through a conventional SKSEVR/Proton path
  in this same broad install, so generic VRIK/Proton incompatibility is weak.
* [verified source] `FileMapping.cpp` hooks `LdrLoadDll`, `LdrGetDllHandleEx`,
  `GetModuleHandle*`, and `GetModuleFileName*` to make a mapped host look like
  SkyrimVR. `LdrGetDllFullName` remains intentionally disabled and is not safe
  to enable unchanged.

## Decisions for review

### 1. Highest-information next step

**Current lean:** determine whether the stuck VRIK loader-time code observes an
inconsistent main-module identity (PEB/LDR vs API hook answers) before a broad
identity repair. The new trace rules out a nested unresolved DLL but cannot
observe VRIK's DllMain/CRT execution.

Options:

1. Add a narrow VRIK-specific runtime diagnostic that identifies whether the
   module reaches its entrypoint/static constructors, without changing loader
   policy.
2. Add a correctly contract-preserving main-image identity repair (PEB/LDR and
   all relevant module-name APIs) before rerunning.
3. Use a non-source debugger/loader observation capable of freezing the Windows
   thread while it is blocked.
4. Replace manual mapping with conventional process creation/injection.

Rejected now: guessing by enabling the existing `LdrGetDllFullName` hook is
unsafe because it does not honor the native output buffer contract; removing
VRIK violates the explicit compatibility requirement; replacing the mapper is
architecturally large without evidence that an identity inconsistency is causal.

### 2. Define the smallest valid identity repair if one is now warranted

**Current lean:** the code's API masking is known incomplete because the PEB
main-image metadata is not a conventional SkyrimVR loader entry. But evidence
does not yet show VRIK queried it. Any repair must be atomic in behavior:
`PEB.ImageBaseAddress`, `LDR_DATA_TABLE_ENTRY`, `GetModuleHandle*`,
`GetModuleFileName*`, `LdrGetDllHandleEx`, and `LdrGetDllFullName` must agree
on image base, canonical path, entrypoint, and image size.

### 3. Scope a VRIK-specific diagnostic if identity repair is premature

It must be loader-lock safe, narrow, reversible, and useful under Proton. It
may not depend on VRIK source changes, sideload a replacement VRIK, or call
VRIK's SKSE exports before the module has finished loading.

## Requested review output

Verify the code and logs before critique. Return a prioritized recommendation
of the single next action with exact file/function scope if it requires a
source change. State why it discriminates the leading failure families, what
would falsify it, and whether it requires a Windows rebuild. Explicitly audit
loader lock, CRT/static initialization, TLS, PEB/LDR identity, MinHook/Wine,
and compatibility with VRIK/HIGGS/PLANCK. Do not edit files, launch anything,
deploy anything, or change the server.
