# HIGGS Read-Only Endpoint Crash: Senior Review Brief

## Goal and scale

Remove the deterministic Skyrim VR startup crash in `SkyrimTogetherVRHiggsBridge`
without weakening the tick-bridge trust boundary or requiring broader architecture
changes. This is a compatibility plugin for end-user Windows and Linux/Proton
installs; keep the solution small and auditable rather than adding framework-level
machinery.

## Verified evidence

| Claim | Status and verification |
| --- | --- |
| The 2026-07-14 active-mode launch reached HIGGS/PLANCK and renderer initialization, then crashed before the main menu. | `[verified: fresh tp_client.log and crash_UTC_2026-07-14_22-59-27.dmp]` |
| WineDbg places the exception at `SkyrimTogetherVRHiggsBridge+0x6916`, called from `+0x648a`, then HIGGS `TriggerPostVrikPostHiggsCallbacks`. | `[verified: WineDbg native stack]` |
| The deployed DLL/PDB correspond to implementation revision `220bc4bca709bf1fa208984d1fd4b2b5b773210a`. | `[verified: deployment manifest, PDB source paths, and package audit]` |
| RVA `0x6916` is `lock cmpxchg %ecx, 0xc(%rdi)` in `ReadEndpointState`; `+0x648a` is the return site immediately after `ResolveBodyCaptureCallback`. | `[verified: llvm-objdump plus llvm-pdbutil symbols/lines]` |
| `MapEndpoint` maps the endpoint with `FILE_MAP_READ`. | `[verified: Code/higgs_bridge/main.cpp:271-272]` |
| `InterlockedCompareExchange(ptr, 0, 0)` is a read-modify-write operation even when the replacement equals the comparison value, so the page must be writable. | `[verified: emitted locked cmpxchg and exception at the mapped State field]` |
| The endpoint `State` field is 32-bit, volatile, and fixed at offset `0x0c`; `Endpoint` is aligned to 8. | `[verified: Code/vr_common/VRTickBridge.h static assertions]` |
| The standalone `SkyrimTogetherVRTickBridge`, which also maps with `FILE_MAP_READ`, reads State through an aligned volatile load followed by `MemoryBarrier()`. | `[verified: Code/vr_tick_bridge/main.cpp:156-161 and :204]` |
| The publisher owns a read/write view and writes State with `InterlockedExchange`. | `[verified: Code/client/VRTickBridge.cpp:31-39, :62, :96, :131, :141]` |
| The HIGGS callback performs no mapping or locking; it validates ABI, PID, reserved fields, executable callback range, epoch, and sequence before calling the client body-capture callback. | `[verified: Code/higgs_bridge/main.cpp:285-354]` |
| The HIGGS endpoint view is intentionally retained for process lifetime. | `[verified: g_endpoint ownership and no teardown unmap in Code/higgs_bridge/main.cpp]` |

## Current diagnosis

`ReadEndpointState` accidentally copied the publisher-side interlocked read idiom
onto a consumer view that is deliberately read-only. The first post-HIGGS callback
therefore faults before body pose capture is dispatched. The crash is unrelated to
HIGGS vtable order, PLANCK, the VM-update hook, server protocol, or character
creation.

## Open decisions

### 1. State read implementation

**Current lean:** replace the locked RMW with the same volatile-load plus
`MemoryBarrier()` implementation already used by `SkyrimTogetherVRTickBridge`.
This preserves a read-only view and provides a consistent acquire-like consumer
pattern for the Windows ABI used here.

- Option A: volatile aligned load, then `MemoryBarrier()` (current lean).
- Option B: map the endpoint read/write and retain `InterlockedCompareExchange`.
- Option C: use `std::atomic_ref` over the POD field.

Rejected lean: Option B unnecessarily grants write access to an untrusted companion
DLL. Option C introduces C++ object-model and cross-module/process assumptions into
an intentionally plain Win32 POD ABI.

### 2. Scope of the patch

**Current lean:** change only `ReadEndpointState`, add a static audit assertion for
the non-RMW read and read-only mapping pairing, and document the runtime proof.

- Option A: narrow fix and regression audit (current lean).
- Option B: refactor both tick consumers into a shared helper.
- Option C: disable body capture or remove the HIGGS callback path.

Rejected lean: Option B broadens a runtime-sensitive patch immediately before VR
testing. Option C discards required HIGGS/PLANCK/VRIK/FBT integration instead of
fixing the identified synchronization bug.

### 3. Lifetime and validation hardening

**Current lean:** do not add callback deregistration or endpoint unmapping in this
patch. SKSE plugins are process-lifetime modules, HIGGS exposes no callback removal
API, and the mapping is a process-lifetime endpoint. Retain the existing ABI/PID/RVA
and `VirtualQuery` validation.

Open concern: determine whether a cheap `VirtualQuery` check of the endpoint view
itself is warranted before every callback, or whether that adds cost without
covering a realistic lifetime transition.

## Suspected overlap

Decisions 1 and 3 overlap only if the endpoint can be retired and unmapped while
HIGGS callbacks still run. Current code retires State but does not unmap until
process teardown, so they should remain separate.

## Requested review

1. Verify the instruction/source mapping and challenge the root cause.
2. Rank the three implementation options for the state read under MSVC and Wine.
3. Audit the callback, endpoint lifetime, and teardown path for another likely
   startup/runtime fault that would force an additional rebuild.
4. Identify the smallest useful regression check that can run without launching
   Skyrim.
5. Return prioritized findings with file/line evidence and a recommended patch
   shape. Do not edit files.

## Depth budget and not-list

Spend the deep review on Win32 memory semantics, ABI correctness, endpoint lifetime,
and the immediately adjacent callback path. Do not re-review server networking,
Skyrim Together gameplay replication, VR offsets, character-creation automation,
the HIGGS interface vtable order already covered by the upstream-header audit, or
unrelated documentation.
