# Tick-Bridge Cadence Owner Link Review

## Goal

Choose the smallest ABI-correct correction for the cadence-owner native used by
the stale-save startup-quest migration. This is a solo operator VR port. The
immediate target is one safe timer that reaches `World::Update`; it is not a
full multiplayer-parity decision.

## Verified Facts

| Fact | Evidence |
| --- | --- |
| The default client and the three compatibility bridges compile. | Windows build on 2026-07-12 completed `SkyrimTogetherVRClient`, `VrikBridge`, `HiggsBridge`, and `PlanckBridge`. |
| The tick bridge fails only at link. | MSVC reports unresolved `UnpackValue<long>` and `GetTypeID<long>` from `NativeFunction1<StaticFunctionTag, bool, SInt32>` in `Code/vr_tick_bridge/main.cpp`. |
| Boolean Papyrus natives are explicitly supported by this bridge. | `main.cpp:23-36` supplies `PackValue<bool>` and `GetTypeID<bool>`. Existing `NativeFunction0<..., bool>` registrations link in the prior build. |
| The failure was introduced by the owner argument. | `ClaimCadence(Int aiOwner)` in `GameFiles/SkyrimVR/scripts/source/SkyrimTogetherVRTickBridge.psc:4` maps to `SInt32` and the one `NativeFunction1` registration at `main.cpp:375`. |
| Two fixed owners are all that exist. | The original startup quest always claims `1`; the migration quest always claims `2`. The native rejects any other value. |
| The two-timer guard must apply during `OnUpdate`. | Both scripts claim before `Tick()` and return without rescheduling after a failed claim. |

## Decision

Keep `ClaimCadence(Int)` and define only the exact pinned-SDK
`UnpackValue<SInt32>` and `GetTypeID<SInt32>` specializations locally. This
preserves the Papyrus contract while avoiding the SDK's broad `PapyrusArgs.cpp`
translation unit and its unrelated globals.

### Alternatives

- Add `SInt32` `UnpackValue`/`GetTypeID` specializations locally. **Adopted:**
  the exact implementations in the pinned SKSEVR SDK require no unrelated
  global state and satisfy the two missing linker symbols.
- Use two fixed no-argument natives. **Reject:** this changes an otherwise
  correct Papyrus contract and does not remove a real runtime risk.
- Use one no-argument native whose caller is inferred. **Reject:** this needs
  fragile script identity inspection and can not distinguish timers safely.
- Drop the cadence guard. **Reject:** a stale save can retain the old timer and
  create two `World::Update` paths.

## Review Questions

1. Verify that two `NativeFunction0<StaticFunctionTag, bool>` registrations are
   ABI/link-safe with this SKSEVR SDK and this bridge's existing specializations.
2. Identify hidden Papyrus save/PEX/native-registration hazards of retaining
   `ClaimCadence(Int)` in an unreleased package. The migration PEX still must
   be compiled before deployment.
3. State the exact source/audit changes required, and whether any broader
   cadence, save lifecycle, or package risk is still uncovered by this fix.

## Not In Scope

- Do not re-review connection-only versus full gameplay parity.
- Do not reinstate original SE VM hooks or add a recurring native task.
- Do not edit, build, deploy, or restart processes.

## Output

Verify before critique. Lead with a go/no-go and any ABI blocker. Cite paths
and line numbers. End with a minimal pre-build checklist; maximum 900 words.
