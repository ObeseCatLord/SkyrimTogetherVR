# Senior Brief: Fully Functional Index Input on Linux/Monado

## Goal and scale

Make the user’s Valve Index controllers fully functional in Skyrim VR when launched
through the SkyrimTogetherVR Linux wrapper. The target experience is the FUS input
configuration: both joysticks, menu navigation, trigger confirmation, and expected
VRIK-compatible controls must work. This is a solo-operator game installation, but the
result should be a reproducible wrapper/install configuration, not an undocumented
one-off desktop tweak.

The active goal remains connecting SkyrimTogetherVR to a server. Character creation is
blocked by controller input, so this is now the critical path.

## Verified environment facts

| Claim | Status | Evidence |
| --- | --- | --- |
| The game is running under UMU/GE-Proton on Linux and uses XRizer instead of SteamVR. | [verified: `/proc` environment] | Active Windows process `SkyrimTogetherVR.exe` has `PROTON_VR_RUNTIME=/home/obesecatlord/.local/share/envision/duplicateoflighthouse/target/release`. |
| Active XRizer is Supreeeme/xrizer commit `3131956`, version `v0.5-17-g3131956-dirty`. | [verified: Git/runtime log] | `/home/obesecatlord/.local/share/envision/duplicateoflighthouse/.git`; `$XDG_STATE_HOME/xrizer/xrizer.txt`. |
| Monado/XRizer reports a focused session and detected `valve/index_controller` for both hands. | [verified: runtime log] | XRizer log lines for session `FOCUSED` and both hand interaction profiles. |
| The user reports joystick movement/turning do not work; trigger confirmation fails in character creation, leaving the UI stuck. | [verified: user runtime report] | Current user report. |
| The direct installation currently uses FUS’s selected control map (`Controller Bindings - Cangar - Spellsiphon`). | [verified: file hash] | `Data/Interface/Controls/PC/controlmapvr.txt` SHA-256 `98c35a...`; exact source in FUS has same hash. Old map is backed up at `Data/STVR-Backups/FUS-Cangar-Controller-Bindings-20260713T041949Z/controlmapvr.txt`. |
| FUS’s current MO2 profiles select Cangar Spellsiphon and explicitly disable the older `VRIK Rift-Index-WMR Controller Bindings` mod. | [verified: FUS profile files] | `.../Skyrim VR MO2/profiles/{Panda's Sovngarde,Sneedrim}/modlist.txt`, lines 54 and 57. |
| Active `SkyrimPrefs.ini` enables motion controllers, direct wand movement, and gamepad support. | [verified: current config] | `.../Documents/My Games/Skyrim VR/SkyrimPrefs.ini`: `bUsingMotionControllers=1`, `bDirectMovementWithWands=1`, `bGamepadEnable=1`. |
| The older VRIK Index binding package states that Index users need a SteamVR community binding in addition to its game data files. | [verified: local package documentation] | `.../mods/VRIK Rift-Index-WMR Controller Bindings/meta.ini` and `Controls.txt`. |
| SteamVR is not in the active input path, so SteamVR community bindings cannot be assumed to apply. | [verified: runtime configuration] | XRizer is an OpenVR-on-OpenXR runtime; no SteamVR process/config is active for this game. |
| XRizer has a custom binding mechanism, `XRIZER_CUSTOM_BINDINGS_DIR`, that loads controller-type JSON by name; it can substitute a binding file before the action manifest’s packaged default. | [verified: local source] | `README.md:78-85` and `src/input/action_manifest.rs:849-865` in active XRizer checkout. |
| Skyrim VR does not provide an obvious game-local OpenVR action manifest JSON. | [verified: game tree scan] | No `actions.json`, `*action*manifest*.json`, or `*binding*.json` under direct SkyrimVR root. |
| Active XRizer supports a legacy OpenVR input path. For Knuckles it maps OpenXR thumbstick/trigger/squeeze to old OpenVR controller state. | [verified: active XRizer source] | `src/input/profiles/knuckles.rs::legacy_bindings`, `src/input/legacy.rs::get_legacy_controller_state`. |
| XRizer logs currently show no input errors, action-manifest loading, or custom-binding loading. | [verified: runtime log] | `$HOME/.local/state/xrizer/xrizer.txt`; only input-profile changes and unrelated `ShowKeyboardForOverlay unimplemented`. |

## Relevant code facts

1. The active `Knuckles::legacy_bindings` uses `Thumbstick` for `main_xy`,
   `Trigger::Value` plus `Trigger::Click`, `Squeeze::Value`, and A/B click actions.
   `get_legacy_controller_state` returns `main_xy` as OpenVR axis 0, trigger as axis
   1, and synthesizes button events from `changed_since_last_sync`.
2. In a legacy setup, XRizer attaches its own action set in `setup_legacy_actions`.
   When a game uses an OpenVR action manifest, `SetActionManifestPath` triggers a
   session restart then attempts to load the game-specific default binding JSON,
   optionally overridden by `XRIZER_CUSTOM_BINDINGS_DIR/<controller>.json`.
3. The current launch wrapper does **not** set `RUST_LOG` or
   `XRIZER_CUSTOM_BINDINGS_DIR`. Existing logs are therefore insufficient to show
   whether Skyrim is calling legacy `GetControllerState`, whether it loads an action
   manifest through a non-obvious path, or whether an OpenXR component is inactive.
4. The game is already in a bad character-creation UI state. A clean relaunch will be
   necessary after changing input configuration.

## Decision to review

### A. What input layer must be configured?

**Current lean:** First instrument the existing XRizer runtime at `debug`/`trace` for
only input and OpenVR calls, then choose based on hard evidence:

* If SkyrimVR uses legacy controller state, fix/validate XRizer’s legacy Knuckles
  mapping and runtime OpenXR component activation. Do **not** try to install a
  SteamVR community binding that XRizer cannot use.
* If SkyrimVR loads an action manifest, extract the manifest/default binding path from
  XRizer logs and create an `XRIZER_CUSTOM_BINDINGS_DIR/knuckles.json` that mirrors a
  known-good VRIK/FUS binding. Launch the wrapper with that directory.

Other options considered:

* **Only change `controlmapvr.txt`: rejected.** It tells Skyrim what legacy button IDs
  mean but does not establish incoming OpenXR/OpenVR controller action values.
* **Run SteamVR just to use community bindings: rejected provisionally.** It defeats
  the current Linux/Monado XRizer route and may create runtime conflicts; it can be a
  fallback only if XRizer cannot reproduce the required legacy/controller contract.
* **Patch SkyrimTogetherVR: rejected.** The game’s own input fails before any STVR
  gameplay logic is required; the input fault is runtime/tooling level.
* **Use the old VRIK map instead of FUS’s selected Cangar map: rejected as first
  choice.** It may change button labels, but it does not explain inactive incoming
  joystick/trigger states.

### B. How should the result be packaged?

**Current lean:** Add a small, documented XRizer binding/config directory to the
SkyrimTogetherVR project or game install and teach `launch-skyrim-together-vr.sh` to
export it only when XRizer is the selected `PROTON_VR_RUNTIME`. Preserve a user
override. Avoid altering global XRizer configuration or unrelated games.

## Open questions the review must answer

1. Does current XRizer code have a known or apparent legacy Knuckles mapping defect
that explains thumbstick/trigger failures under Monado? Cite exact code.
2. Is the game likely in legacy OpenVR mode or action-manifest mode, and what minimal
instrumentation will prove it on the next clean launch?
3. If custom JSON is needed, where should a known-good Skyrim VR/Index OpenVR binding
be sourced from, and can FUS/VRIK data provide it? Do not invent a binding file without
an action manifest or validated schema.
4. Is FUS’s Cangar mapping appropriate for Index through XRizer, or does it rely on
SteamVR presenting buttons differently? State what to install first after evidence is
collected.
5. Recommend the exact safe sequence for: stop the stuck game, collect diagnostic
input evidence, apply configuration, relaunch, and verify full character-creation
input.

## Non-goals / not-list

Do not review SkyrimTogetherVR networking, bridge code, HIGGS/PLANCK internals,
gameplay synchronization parity, remote server configuration, or broad Proton
compatibility. Do not edit files. Return a prioritized critique with a concrete,
low-risk implementation plan and identify anything that requires a human headset test.

## Review Disposition

| Senior recommendation | Disposition | Implementation / reason |
| --- | --- | --- |
| Treat Skyrim VR as legacy OpenVR and do not install speculative action-manifest JSON. | Adopted | The captured XRizer run reaches focused Index profiles without `SetActionManifestPath`; the installed control map is therefore paired with XRizer's legacy controller-state path. No custom binding directory is deployed. |
| Retain the selected FUS Cangar map. | Adopted | Its Oculus columns explicitly carry movement, look, and menu acceptance. Replacing it with the older VRIK map would not repair the missing controller facade. |
| Add an opt-in Knuckles-as-Oculus-Touch facade, retaining Knuckles OpenXR bindings. | Adopted | XRizer now supports `XRIZER_OPENVR_KNUCKLES_AS_OCULUS_TOUCH=1`; the direct online and offline launchers set it only when their patched local XRizer runtime is selected. An explicit user value is preserved. |
| Keep the existing Vive facade available but do not use it for FUS. | Adopted | Vive reports a trackpad main axis, while the Cangar map requires Oculus thumbstick identifiers. The Oculus facade takes precedence if both compatibility variables are set. |
| Verify VRIK touchpad gestures separately from core controls. | Adopted | The expected first headset test covers locomotion, turning, menus, trigger confirmation, A/B, grip, and stick clicks. XRizer's legacy abstraction may still lack the community binding's Index touchpad aliases. |
