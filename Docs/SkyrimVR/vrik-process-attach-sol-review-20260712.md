# Senior Review Brief: VRIK CRT Process-Attach Stall

## Goal and scale

Identify the next safe, high-information action toward making the mapped Skyrim
Together VR launcher load full VRIK 0.8.5 under Proton. This is a solo-operator
startup issue. The goal is not to suppress VRIK, replace it, weaken VRIK/HIGGS/
PLANCK compatibility, or redesign gameplay synchronization.

## Verified evidence

| Fact | Status and evidence |
| --- | --- |
| Current fresh Windows package hash is `586541e2a7e1d02b39737074db5c229bb79b6150b4d7e58e16261a4ba6566dd9`; it includes the fixed-capacity loader trace. | [verified: package/installed hash and `strings`] |
| Default package audit with full SKSEVR, Address Library, VRIK, HIGGS, and PLANCK passed. | [verified: `audit_built_package.py --require-installed-prerequisites --require-vrik --require-higgs --require-planck`] |
| The normal launch loads PLANCK, HIGGS, and all three STVR bridge DLLs, then stops at VRIK. | [verified: current `sksevr.log`] |
| Excluding all STVR bridge DLLs in a previous control run left the VRIK stop point unchanged. | [verified: recorded control run and restored target files] |
| `LdrLoadDll(VRIK.dll)` remains pending around 59 seconds. VRIK's nested API-set/kernel requests return. | [verified: current `tp_client.log`, VRIK sequence 89 and child sequences 90-94] |
| A current `WINEDEBUG=trace+module,+timestamp` run logs VRIK `process_attach START` and `MODULE_InitDLL(... VRIK.dll, PROCESS_ATTACH) - CALL`, but no matching `RETURN` or `END` before launcher timeout. | [verified: `<SkyrimVR>/logs/stvr-wine-module-20260712T1837Z.log`, VRIK lines 16019-16026 and end of file] |
| Wine has mapped/relocated VRIK, initialized its security cookie, allocated its TLS slot, and resolved KERNEL32/USER32 before the non-returning entrypoint call. | [verified: same module trace lines 15994-16022] |
| The VRIK PE TLS directory has an 8-byte template and an `AddressOfCallbacks` table at RVA `0xCEAA8`, but that table's first pointer is zero. | [verified: local `llvm-readobj --coff-tls-directory`; direct PE parser, 2026-07-12] |
| VRIK's PE entrypoint is `_DllMainCRTStartup`; user `DllMain` only records module handle and returns; PDB shows 191 `.CRT$XCU` initializer slots. | [verified: prior Sol PDB/DLL inspection; retained to be rechecked] |
| The launcher currently maps SkyrimVR over its own image and masks selected module/file APIs. Its `LdrGetDllFullName` hook is disabled and its current implementation is contract-invalid; `LdrGetDllHandleEx` has a byte-vs-wchar length defect. | [verified: `Code/immersive_launcher/stubs/FileMapping.cpp`] |
| No game client or debugger process remains. The remote server is healthy and unrelated. | [verified: post-timeout process check; server container state] |

## What the evidence rules out

* The fault is not a missing static/import dependency, a failed VRIK SKSE query
  or load callback, HIGGS/PLANCK's direct provider API, a STVR bridge writer,
  server connection, or native TLS callbacks.
* It is not a simple slow load: the same operation remains in VRIK attach for
  roughly 59 seconds until deliberate termination.

## Leading failure family

VRIK is blocked inside `_DllMainCRTStartup` under SKSEVR's loader lock. The
highest-probability subfamily is CRT/static constructor behavior; the remaining
alternatives are loader-notification/process-attach bookkeeping or a constructor
that observes the mapper's incomplete main-module identity.

This does not establish which of the 191 static initializers is active, nor
does it show any identity query by VRIK.

## Open decisions

### 1. What is the smallest next action?

**Current lean:** obtain one instruction-level/static-initializer observation
without changing production mapper behavior. The module trace has already
reached its maximum value, and speculative PEB/LDR repairs would be broad.

Options:

1. A non-source debugger procedure that pauses at VRIK `_DllMainCRTStartup` or
   the active initializer and identifies the current instruction/frame using
   the matching PDB.
2. A bounded code diagnostic that records VRIK entrypoint/initializer progress
   without logging, allocation, or waits under loader lock.
3. A correctness repair to main-module PEB/LDR identity, only if the static
   evidence establishes an identity-dependent call.
4. A launcher-architecture replacement using conventional SkyrimVR process
   creation/injection.

Rejected now: enabling the current full-name hook; editing Wine LDR structures;
longer timeout; disabling VRIK; bridge changes; and a full Wine relay trace.

### 2. What production change would be warranted after the next observation?

**Current lean:** none is justified yet. A legitimate repair must preserve
loader-lock correctness, remain compatible with VRIK/HIGGS/PLANCK, and not
alter VRIK's code or behavior. If the issue is a STVR mapping invariant, specify
the full set of process-identity consistency changes, not a single API hook.

### 3. Does current manual mapping fundamentally preclude this VRIK release?

**Current lean:** unproven. VRIK's process attach proves the manual mapping
does not categorically prevent native plugin load. Determine whether a concrete
invariant or call proves this before proposing a launcher rewrite.

## Requested review output

Verify the source, logs, current VRIK PE/PDB metadata, and the GE-Proton module
trace semantics before critique. Return a prioritized recommendation for one
next action, including exact commands or exact source files/functions if it
requires code, expected observations, falsifying outcomes, and whether it
requires another Windows rebuild. Explicitly review CRT static initialization,
loader lock, TLS, PEB/LDR identity, MinHook, and Wine/NTSync. Do not edit,
build, launch, deploy, or modify the server.
