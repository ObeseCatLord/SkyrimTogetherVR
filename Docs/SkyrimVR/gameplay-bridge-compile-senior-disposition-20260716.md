# Gameplay Bridge Compile Senior Review Disposition

## Outcome

The Sol-max review covered all 18 translation units selected by the gameplay
bridge target and all 19 relevant local headers. It confirmed the two initially
observed MSVC failure families and expanded the undefined-macro family from
three known sites to six. The subsequent clean MSVC build exposed one additional
invalid enum member in `ActorActionHooks.cpp`; that compiler-discovered issue is
included below rather than treating the original review as exhaustive.

## Disposition

| Recommendation | Decision | Implementation |
|---|---|---|
| Remove every bridge use of undefined `TP_UNUSED` | Adapted and adopted | Replaced all six sites in `ActorWorldManager.cpp`, `EventCapture.cpp`, `LocalGameplayCapture.cpp`, and `VRBodyPoseManager.cpp` with `static_cast<void>(...)`; call expressions still execute |
| Scope the `GameplayAction::Mount` case | Adopted | Added a case-local block around the `RE::NiPointer<RE::Actor>` lifetime |
| Replace nonexistent `ChunkAcceptResult::Rejected` | Adopted after clean MSVC build | `StageGraph` now maps `ChunkAcceptResult::Malformed` to `CommandStatus::Malformed`; the protocol enum has no `Rejected` member |
| Add a compatibility `TP_UNUSED` macro | Rejected | The CommonLib plugin should not acquire a Tilted-PCH dependency for six ordinary discarded expressions |
| Speculatively change GLM aggregates, CommonLib calls, exports, or link libraries | Rejected | Full source verification found no concrete blocker in those families |
| Re-review server/network/gameplay architecture | Not in scope | Prior reviews cover those decisions; this pass was strictly compile/link focused |

## Main-Agent Verification

- Confirmed exactly six `TP_UNUSED` tokens existed in the gameplay bridge and
  that three wrapped call expressions whose side effects must be preserved.
- Confirmed the `Mount` case declared a non-trivial `RE::NiPointer` before later
  case labels without a local block.
- Confirmed `xmake.lua` uses a top-level `*.cpp` glob. All current bridge sources
  are flat, so all 18 translation units are selected; the earlier brief's word
  "recursively" was imprecise.
- Confirmed the target does not configure its own xmake PCH. Local `pch.h` is
  included through `BridgeEndpoint.h`; CommonLib's conditional generated PCH is
  separate.
- Audited all scoped-enum member expressions in `vr_common` and
  `vr_gameplay_bridge` against their declarations after the MSVC failure. The
  `ChunkAcceptResult::Rejected` expression was the only nonexistent member.

## Verification Gate

Run a clean WinBoat compile of `SkyrimTogetherVRGameplayBridge` for the pushed
fix revision, then run the complete clean WinBoat gameplay build. The release
gate remains: unit tests, all client and bridge targets, package audit, build
evidence audit, deterministic Linux gameplay archive validation, and local
handoff audit must all succeed before deployment.
