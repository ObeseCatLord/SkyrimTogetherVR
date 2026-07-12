# Senior Review Brief: HIGGS Initial Snapshot Crash

## Goal and boundary

Remove the post-SKSEVR-launch access violation while retaining HIGGS, PLANCK,
VRIK, and all Skyrim Together VR bridges. The goal is a minimal HIGGS bridge
lifecycle correction, not a change to the newly working official SKSEVR Steam
shim or a broad HIGGS integration rewrite.

## Verified evidence

| Fact | Evidence |
| --- | --- |
| The new Steam shim works. | `sksevr_steam_loader.log`: IAT found and patched, `HookMain` reached. |
| SKSEVR and all required plugins load. | `sksevr.log`: PLANCK, HIGGS, the three STVR bridges, and VRIK each report `loaded correctly`. |
| The crash is after mapped game entry. | `tp_client.log`: shim helper completed, then `entering mapped Skyrim VR gameMain`, then `c0000005` 906 ms later. |
| The fault address is HIGGS RVA `0x55B47`. | Process mapping gives base `0x6ffffa160000`; fault is `0x6ffffa1b5b47`. |
| The RVA symbolizes to `HiggsPluginAPI::HiggsInterface001::IsTwoHanding`. | Local `higgs_vr.pdb` with `llvm-symbolizer`; source path `higgs_vr/src/pluginapi.cpp:358`. |
| The faulting instruction dereferences a null runtime pointer. | Disassembly at RVA `0x55B47`: `mov rax, [global]`, then `cmp byte ptr [rax+0xdc5], 0x0e`. |
| The STVR HIGGS bridge calls `IsTwoHanding` in its first snapshot. | `Code/higgs_bridge/main.cpp:400-411`. |
| That snapshot is called immediately after callback registration. | `RegisterCallbacks` calls `CaptureHiggsSnapshot` at `Code/higgs_bridge/main.cpp:425-440`. |
| The bridge also registers `OnPostVrikPostHiggs`, whose callback takes snapshots. | `Code/higgs_bridge/main.cpp:420-438`. |
| HIGGS is acquired and callbacks are registered during SKSE `PostLoad`/`PostPostLoad`. | `Code/higgs_bridge/main.cpp:601-629`. |

## Current implementation

`RequestHiggsInterface()` gets the HIGGS interface during `PostLoad` or
`PostPostLoad`, stores it, and calls `RegisterCallbacks()`. Registration adds
event and post-VRIK/post-HIGGS callbacks, then immediately calls every live
HIGGS query through `CaptureHiggsSnapshot()`. The writer thread only serializes
the last cached snapshot and does not itself call the HIGGS API.

## Decision

### Current lean: remove only the immediate live snapshot

Keep interface acquisition, callback registration, and writer startup unchanged.
Remove the direct `CaptureHiggsSnapshot()` at the end of `RegisterCallbacks()`.
Let `OnPostVrikPostHiggs()` become the first live API query, because it is the
HIGGS-provided post-update ordering callback. Before that point the bridge emits
an available interface but no live snapshot.

Alternatives considered:

* Add a generic SKSE `DataLoaded` gate: rejected provisionally because its exact
  relation to HIGGS runtime-state initialization is unverified, while the HIGGS
  callback encodes the intended ordering directly.
* Disable HIGGS or the bridge: rejected because the user requires HIGGS and the
  bridge, and symbol evidence isolates the premature bridge query.
* Guard HIGGS calls with SEH: rejected because it would hide an invalid lifecycle
  call and risk corrupting an external plugin.

## Scope and review request

Verify the code and evidence above. Decide whether the narrow deferral is safe
and sufficient, or whether an additional lifecycle/readiness gate is required.
Identify only must-fix issues before a Windows rebuild. Do not re-review SKSE
shim timing, manual mapping, networking, server behavior, or unrelated bridges.

## Senior Review Disposition

Adopt the narrow deferral. Remove only the direct `CaptureHiggsSnapshot()` call
at the end of `RegisterCallbacks()`. Retain the HIGGS interface request,
callback registration, writer thread, and `OnPostVrikPostHiggs()` snapshot path.
No generic SKSE `DataLoaded` gate is justified: the HIGGS post-update callback is
the specific ordering contract, while `DataLoaded` has no verified stronger
relationship to HIGGS runtime-state initialization.
