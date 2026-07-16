# Gameplay Bridge Compile Senior Review Disposition

## Outcome

The Sol-max review covered all 18 translation units selected by the gameplay
bridge target and all 19 relevant local headers. It confirmed the two initially
observed MSVC failure families and expanded the undefined-macro family from
three known sites to six. Exact-commit MSVC preflights then exposed additional
compiler and linker incompatibilities with the newly pinned CommonLib API; each
compiler-discovered issue is included below rather than treating a static review
as a substitute for the real Windows toolchain.

## Disposition

| Recommendation | Decision | Implementation |
|---|---|---|
| Remove every bridge use of undefined `TP_UNUSED` | Adapted and adopted | Replaced all six sites in `ActorWorldManager.cpp`, `EventCapture.cpp`, `LocalGameplayCapture.cpp`, and `VRBodyPoseManager.cpp` with `static_cast<void>(...)`; call expressions still execute |
| Scope the `GameplayAction::Mount` case | Adopted | Added a case-local block around the `RE::NiPointer<RE::Actor>` lifetime |
| Replace nonexistent `ChunkAcceptResult::Rejected` | Adopted after clean MSVC build | `StageGraph` now maps `ChunkAcceptResult::Malformed` to `CommandStatus::Malformed`; the protocol enum has no `Rejected` member |
| Explicitly construct the local-player `NiPointer` | Adopted after focused MSVC preflight | `ResolveGameplayActor` now constructs `RE::NiPointer<RE::Actor>` explicitly from the `PlayerCharacter*`, as required by alandtse CommonLib's explicit raw-pointer constructor |
| Audit all bridge smart-pointer and handle flows | Adopted after focused MSVC preflight | A bounded Terra pass found the only other raw `Actor*` assignment; `CombatMagicManager` now uses `NiPointer::reset`, while `ActorHandle::get()` flows remain valid because they return `NiPointer<T>` |
| Pass Papyrus VM arguments as values | Adopted after focused MSVC preflight | All four pointer/integer lvalues passed to `MakeFunctionArguments` in `AnimationAppearanceManager` are converted to prvalues, avoiding unsupported `FunctionArguments<T&>` instantiations; the other two bridge call sites already passed prvalues |
| Use alandtse CommonLib's no-source enum name | Adopted after focused MSVC preflight | `CombatMagicManager` now accepts `CastingSource::kNone` (numeric value 4) instead of the nonexistent older spelling `kTotal` |
| Make worn-equipment capture mutable | Adopted after focused MSVC preflight | `CaptureWornEquipment` now accepts the mutable player reference already supplied by its caller because alandtse CommonLib exposes `TESObjectREFR::GetInventory()` only as a non-const accessor |
| Read VR tint data through the verified runtime layout | Adopted after focused MSVC preflight link | alandtse CommonLib deliberately omits `PlayerCharacter::GetTintMask` and the related tint-list helpers when `ENABLE_SKYRIM_VR` is set. Local appearance capture now selects the skin-tone entry from `GetVRPlayerRuntimeData().tintMasks`, removing both the unresolved helper and an unnecessary desktop relocation call |
| Declare `minhook` at the root Windows package boundary | Adapted from follow-up Sol-max review | Added explicit `minhook v1.3.3` root ownership. It already resolved indirectly through `TiltedReverse`, so this was not the observed compile failure, but the gameplay bridge no longer depends on another target module to declare its package |
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
- Added a disposable exact-commit WinBoat bridge preflight after the enum fix.
  It exposed the raw-pointer assignment in `AvatarManager.cpp` before another
  complete package build was attempted.
- Confirmed alandtse CommonLib wraps the definitions of `GetTintMask`,
  `GetTintList`, and `GetOverlayTintMask` in `#ifndef ENABLE_SKYRIM_VR` even
  though their declarations remain visible. The VR runtime layout exposes the
  independently verified `tintMasks` array at `PlayerCharacter + 0x1208`.
- A bounded follow-up Terra review checked all 37 gameplay-bridge source and
  header files against CommonLib's VR guards. It found no other concrete
  missing-definition link blocker; live tint ordering remains a runtime test
  item because CommonLib does not document the desktop helper's VR semantics.

## Verification Gate

Run a clean WinBoat compile of `SkyrimTogetherVRGameplayBridge` for the pushed
fix revision, then run the complete clean WinBoat gameplay build. The release
gate remains: unit tests, all client and bridge targets, package audit, build
evidence audit, deterministic Linux gameplay archive validation, and local
handoff audit must all succeed before deployment.
