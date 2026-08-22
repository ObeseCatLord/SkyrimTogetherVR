# Skyrim Together VR protocol and controls final review brief

## Goal and scope

Review the current uncommitted source integration before its single authoritative
WinBoat/MSVC build. The goal is a complete Skyrim VR port of reviewable desktop
Skyrim Together behavior, with VR-specific controls and embodiment extensions,
without advertising runtime parity before paired-client evidence exists. This is
a solo-operated mod project; reject unnecessary enterprise ceremony.

Review only the current diff and directly relevant call paths. Do not build,
launch, deploy, package, edit files, or repeat broad historical address audits.

## Verified evidence

| Claim | Status and evidence |
|---|---|
| Baseline | [verified: git] HEAD is `bcfdd366`; the worktree is intentionally uncommitted and `git diff --check` passes. |
| Build policy | [verified: source/runbook] GitHub workflows are manual-only and must not be triggered. WinBoat/MSVC is authoritative after review. |
| Protocol boundary | [verified: source] Gameplay protocol is 18, mapping ABI is 24, capability revision remains 34. Active runtime validators pin revision 18. |
| Capability claim | [verified: source] Bit 25 is renamed from `NativeGameplayParity` to `NativeGameplayCore` without changing its value; comments exclude UI, admin UX, optional embodiment, and runtime parity claims. |
| Animation configuration | [verified: source] VR loads the desktop `Data/SkyrimTogetherRebornBehaviors` convention, resolves ordered variable names, computes a descriptor digest and direction index, and rejects mismatched descriptors at application. Desktop and VR serialization carry the same metadata. |
| Fixed ABI | [verified: compile-time source assertions] Both changed bridge payloads remain 80 bytes; actor-action values move to `0x34`. |
| Server commit | [verified: source after Terra finding] `SwapAnimationVariables` now swaps descriptor digest and direction index as well as vectors. |
| Controller UI | [verified: source] The lesser power opens a native MessageBox flow for connection, player, party/invitation, travel/admin, and chat actions. Commands are revalidated against lifecycle/session and current service state. |
| VR text | [verified: source] A private OpenVR overlay owns keyboard events and validates bounded UTF-8. Failure is optional and no longer changes a successful gameplay command-pump result. |
| Party semantics | [verified: source] Requests validate current state and report send acceptance. Invite acceptance remains asynchronous and preserves the invite until server transition/expiry. Decline is deliberately local because no wire message exists. |
| Audit status | [verified: two independent read-only audits] Most gameplay domains have substantive producer/server/consumer code. Paired-client behavior is not yet proven. Exact VR `ProcessResponse` and direct remote PLANCK physical replay remain unimplemented because no verified ABI/public replay contract exists. |

## Environment facts

| Fact | Value |
|---|---|
| Repository | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR` |
| Runtime | Skyrim VR 1.4.15.0, SKSEVR 2.0.12 |
| CommonLib | Pinned alandtse CommonLibSSE-NG submodule |
| Build | Windows MSVC through the WinBoat candidate/clean scripts |
| Deployment | Explicitly out of scope until the build and review pass |
| Runtime proof | One-client bootstrap exists only for earlier protocol 17; protocol 18 has no runtime evidence |

## Open decisions and current lean

1. Descriptor identity. Lean: retain bounded ordered-name digest plus local
   recomputation. Rejected count-only identity because equal shapes can have
   different semantics.
2. Native menu callback identity. Lean: retain the byte-slot callback with a
   strict one-outstanding-menu invariant; external opens are rejected while the
   controller owns a flow. Rejected the richer callback overload because its VR
   relocation is not present in the pinned address library.
3. Optional VR keyboard failure. Lean: fail the chat request only and preserve
   the core command pump. Rejected turning optional UI loss into a networking
   fault.
4. Release classification. Lean: connection/bootstrap alpha until a current
   two-client domain matrix passes. Rejected source-completeness as runtime proof.

Potential overlap: animation protocol/bridge ABI/runtime validators are one
release boundary and should be reviewed together.

## Requested review

Verify first, then return prioritized P0/P1/P2 findings with exact file/line
evidence and concrete fixes. Focus on serialization compatibility, fixed-layout
ABI, descriptor lifecycle and relay, menu callback lifetime, OpenVR thread/event
ownership, party async state, source-vs-runtime claims, and missing behavior
tests. Select the highest-leverage issue for deeper analysis. Separate a source
blocker from a runtime-only gate. Maximum 1,500 words.
