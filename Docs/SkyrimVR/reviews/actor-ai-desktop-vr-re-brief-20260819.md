# Actor AI Desktop-to-VR Reverse-Engineering Review Brief

Date: 2026-08-19

## Goal and scope

Determine whether Skyrim Together VR needs exact SkyrimVR.exe equivalents for
three inherited desktop actor hooks, or whether the port's stronger typed remote
actor authority already subsumes them. This is a focused, read-only Sol-max
review. Do not edit, build, launch, deploy, package, or broaden into general
port review.

## Verified evidence

| Claim | Status and evidence |
|---|---|
| Desktop suppresses remote-player detection of the local player | **Verified:** `original-skyrim-together:Code/client/Games/Skyrim/Actor.cpp`, `HookUpdateDetectionState`, desktop Address Library ID 42704. |
| Desktop suppresses dialogue response processing when the talking actor is a remote player | **Verified:** same source, `HookProcessResponse`, desktop ID 39643. |
| Desktop forces `IsFleeing` false for every actor classified as a player | **Verified:** same source, `HookIsFleeing`, desktop ID 37577. The inherited comment itself questions why remote players sometimes have the flee flag. |
| The current VR bridge does not install those three exact hooks | **Verified:** source search under `Code/vr_gameplay_bridge`; remote actors are instead registered as managed, have AI disabled by `AvatarManager`, and are protected by actor-authority hooks. |
| Generated VR correlation for ID 37577 is not `Actor::IsFleeing` | **Verified:** `../_refs/vr_address_tools/addrlib.csv` maps it to VR RVA `0x62C830`; exact decrypted 1.4.15 disassembly is a small wrapper around calls at `0x635D40` and `0x636010`, not the desktop function body. |
| Generated VR correlation for ID 39643 is not dialogue `ProcessResponse` | **Verified:** it maps to VR RVA `0x6D96F0`; the function copies three floats from a global object to global storage and returns. |
| Generated VR correlation for ID 42704 is not detection-state update | **Verified:** it maps to VR RVA `0x767A40`; the function copies twelve floats from `rdx` to `rcx` and returns. |
| Binary input is pinned | **Verified:** legal SkyrimVR.exe 1.4.15.0 source SHA-256 is `6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971`; analysis uses its Steam-CEG-decrypted image at `/tmp/SkyrimVR.ceg-decrypted.exe`. |
| Runtime proof does not cover these semantics | **Verified:** accepted evidence is one-client bootstrap only; no two-client detection/dialogue/fleeing behavior proof exists. |

## Open decisions

1. **Detection:** current lean is that disabling remote AI should suppress more
   behavior than the desktop one-way detection hook, but that this is not yet an
   equivalence proof. Decide whether an exact hook remains a gameplay parity
   blocker and identify the most reliable binary-analysis path to the real VR
   function.
2. **Dialogue response:** current lean is that this requires an exact or
   behaviorally equivalent guard because remote dialogue/audio behavior is
   user-visible and AI disable alone may not gate queued voice responses.
3. **Fleeing:** current lean is to defer an exact hook unless static analysis
   shows a live remote-player path, because the desktop code itself labels the
   rationale unresolved. Decide whether this is required literal parity,
   defensive compatibility, or obsolete behavior.
4. **Admission policy:** generated correlations are rejected. Recommend the
   minimum fail-closed target contract: exact RVA, executable range, runtime,
   prologue, ABI/call-shape evidence, and optional caller validation.

## Required output

Return a concise prioritized table. For each behavior include severity, whether
current VR authority subsumes it, remaining user-visible failure, exact RE
method/target, source change needed before build versus runtime-only evidence,
and an acceptance test. Verify claims against source and binary first. Clearly
separate proven facts from inference. Maximum 1,500 words.

Do not re-review networking, quests, executable loading, health policy,
retirement, evidence tooling, packaging, or general style. Those are covered by
separate workstreams.
