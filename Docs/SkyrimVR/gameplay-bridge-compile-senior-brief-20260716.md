# Gameplay Bridge Compile Senior Review Brief

## Goal And Scope

Finish the first exact-revision Windows/MSVC gameplay build without another
one-error-at-a-time retry. This is a solo-maintained Skyrim Together VR port;
the review should stay practical and focus on concrete compile/link/package
blockers in `SkyrimTogetherVRGameplayBridge`, not revisit the already-reviewed
network/gameplay architecture.

## Verified Evidence

| Claim | Status | Evidence |
|---|---|---|
| Exact source revision | verified: Git and wrapper output | `96a0ebb0`; clean detached WinBoat worktree |
| Unit suite | verified: MSVC run | 621 assertions in 35 cases passed |
| Main gameplay client | verified: MSVC run | `SkyrimTogetherVRGameplayClient` built successfully |
| Other built targets | verified: MSVC run | gameplay launcher, VRIK, HIGGS, PLANCK, and tick bridges built |
| Failing target | verified: MSVC diagnostic | `SkyrimTogetherVRGameplayBridge` |
| Missing macro | verified: MSVC C3861 and source search | `ActorWorldManager.cpp:112` uses `TP_UNUSED`; the target/CommonLib PCH does not define it |
| Switch lifetime error | verified: MSVC C2360/C2361 | `ActorWorldManager.cpp:257` initializes `mount` directly under the `Mount` case without a case scope; later labels bypass it |
| Same macro family | verified: source search | `VRBodyPoseManager.cpp:213-214` also uses `TP_UNUSED` and had not compiled before the target stopped |
| Target inclusion | verified: xmake source | `Code/vr_gameplay_bridge/xmake.lua` recursively includes the bridge `.cpp` files |
| Failed-build cleanup | verified: host/guest checks | detached worktree and temporary `.ps1` removed; no failed package retained |

## Environment Facts

| Item | Value |
|---|---|
| Compiler | Visual Studio 2022 Build Tools 17.14.33, MSVC 14.44 |
| C++ mode | C++23 |
| CommonLib | pinned `alandtse/CommonLibVR-ng` commit `d36bc08c` through `Libraries/CommonLibSSE-NG` |
| Runtime target | Skyrim VR 1.4.15 with SKSEVR 2.0.12 |
| Build entry point | `Tools/SkyrimVR/build_winboat_gameplay.sh <pushed-commit>` |
| Build policy | clean detached Windows worktree; build, tests, package audit, evidence audit, handoff audit |
| Current static gate | full gameplay readiness passes; no source/address/CommonLib audit failures |

## Current Lean And Decisions

1. Replace target-local `TP_UNUSED(expr)` with `(void)expr` or explicit ignored
   result variables. Lean: use `static_cast<void>(expr)` for calls and mark
   unused parameters `[[maybe_unused]]`; do not import a Tilted macro into the
   CommonLib plugin. Rejected: defining another project-wide macro for three
   uses, because it adds unnecessary coupling.
2. Add braces around the entire `GameplayAction::Mount` case. Lean: a scoped
   case is the smallest correct lifetime fix. Rejected: hoisting `mount` before
   the switch, because only one action needs it.
3. Audit every remaining gameplay-bridge translation unit for the same MSVC
   switch-label lifetime rule, undefined Tilted-only helpers, name hiding,
   aggregate/derived-GLM initialization, and obvious CommonLib API signature
   mismatches before rebuilding. Lean: patch only evidence-backed blockers.
   Rejected: speculative broad refactoring before the first complete build.

The missing-macro and parameter-warning cases may be one issue: target-local
code was copied from a Tilted PCH environment into a CommonLib PCH environment.
Merging their remediation is in bounds.

## Review Request

Verify the claims against the repository first. Then provide a prioritized,
bounded list of concrete compile/link risks in all files selected by
`Code/vr_gameplay_bridge/xmake.lua`. Include file and line evidence and an exact
remediation for each confirmed issue. Distinguish confirmed blockers from
hypotheses. Check whether the two current fixes should be adapted, and identify
anything likely to appear only after `ActorWorldManager.cpp` compiles.

Do not review server behavior, transport architecture, address semantics,
Papyrus design, runtime gameplay correctness, or packaging policy unless a
specific bridge compile/link issue depends on them. Do not edit files.
