# Skyrim VR Character-Creation Accept Failure: Senior Review Brief

## Goal and scope

Make the direct Linux/Monado Skyrim Together VR launch complete vanilla/Realm of
Lorkhan character creation with Index controllers. The user can highlight `OK` in
the `Finish and name your character?` dialog, but pressing any controller button
or keyboard input closes the dialog and returns to character editing. It never
opens the VR naming keyboard. This is a solo installation; prefer a minimal,
reproducible fix over importing the full FUS modlist.

Do not review Skyrim Together networking, bridge code, HIGGS/PLANCK behavior, or
server state. Read-only review only; do not edit files.

## Verified facts

| Claim | Status | Evidence |
| --- | --- | --- |
| The failure still occurs after the first Index-to-Oculus facade change. | [verified: user headset test] | User reports the exact same character-creation failure after the relaunch. |
| Skyrim VR is using legacy OpenVR controller state rather than an action manifest. | [verified: XRizer trace] | Repeated `IVRSystem020::GetControllerState`; no `SetActionManifestPath` in the focused launch trace. |
| Monado detects both Index controllers and XRizer has focused Index interaction profiles. | [verified: XRizer trace] | `/interaction_profiles/valve/index_controller` for both hands. |
| The selected FUS Cangar map has Oculus columns for locomotion and menu acceptance. | [verified: installed map] | `Data/Interface/Controls/PC/controlmapvr.txt` lines 31, 79-80. |
| The new local XRizer build reports Index hardware as `oculus_touch` only for the STVR wrapper. | [verified: launcher environment/build] | `XRIZER_OPENVR_KNUCKLES_AS_OCULUS_TOUCH=1`; local release contains this option; wrapper selects the exact runtime. |
| The facade currently changes `ProfileData::properties()` only; it leaves legacy action bindings as `Knuckles::legacy_bindings`. | [verified: local source] | `src/input/devices.rs::ProfileData::new`; `src/input/profiles/knuckles.rs`. |
| XRizer publishes the trigger pressed bit from `trigger_click`, while its analog trigger axis always comes from `trigger`. | [verified: local source] | `src/input/legacy.rs:read_button(SteamVR_Trigger, trigger_click)` and `rAxis[1] = trigger`. |
| For Knuckles, `trigger_click` binds `/user/hand/*/input/trigger/click`; the Oculus profile binds its trigger click to trigger value. | [verified: local source] | `knuckles.rs::legacy_bindings`; `oculus_touch.rs::legacy_bindings`. |
| FUS's `VR Menu Mouse Fix` and `Skyrim VR Tools` are now installed in the direct game. | [verified: deployed files/SKSE log] | FUS source hashes match direct files; `sksevr.log` says both plugins loaded correctly. |
| MenuMouseFix registered its `SkyrimVRTools` callback on this launch. | [verified: plugin log] | `Documents/My Games/Skyrim VR/SKSE/MenuMouseFix.log`: `SkyrimVRTools Init Message recived with valid data, registering for callback`. |
| MenuMouseFix is configured with `ClickButton=33` (OpenVR Axis1/trigger) and logging disabled. | [verified: deployed INI] | `Data/SKSE/Plugins/MenuMouseFix.ini`. |
| The FUS profiles contain both dependency mods, but their listed `VR Menu Mouse Fix` entry is disabled; RaceMenu and Skyrim VR Tools are enabled. | [verified: FUS profile] | `profiles/Panda's Sovngarde/modlist.txt` lines 49 and 1748-49. |
| The direct profile does not include FUS RaceMenu. | [verified: direct plugin/SKSE inventory] | No `RaceMenu*.esp` or `skeevr.dll` in direct game. |

## Evidence from current source

1. `MenuMouseFix` only has the configured event source available. If XRizer's
   Knuckles `/trigger/click` path stays inactive on Monado, `ClickButton=33` does
   not synthesize the mouse click despite the analog trigger axis moving.
2. Switching the controller facade to Oculus properties restores Skyrim's Oculus
   `controlmapvr.txt` column selection, but does not automatically change the
   OpenXR paths that fill legacy OpenVR button state.
3. The dialog's behavior is consistent with an absent `Accept` event: a cancel or
   generic menu-dismiss event closes the confirmation and returns to the editor.
4. Keyboard acceptance also appears ineffective. This may be an independent
   vanilla VR UI focus limitation, a missing VR character-menu compatibility
   component, or evidence that the visible cursor needs a real mouse-click event.

## Decision to review

Choose the narrowest safe next fix, ranked by likelihood and reversibility:

1. Change XRizer's **Knuckles-as-Oculus** facade so its legacy trigger/button
   compatibility events match the chosen Oculus facade, while retaining Index
   poses and thumbsticks. This may require explicit threshold conversion rather
   than directly binding a bool action to an OpenXR float path.
2. Configure `MenuMouseFix` to use a known-good existing button ID (A or grip) and
   turn on its diagnostic logging for one headset input cycle. Rejected as the
   only solution if it cannot provide a click event on the selected physical button.
3. Deploy the exact FUS RaceMenu package and activate its plugins. Rejected as the
   first blind fix because FUS's MenuMouseFix entry is disabled and the observed
   fault is specifically input event delivery.
4. Restore the old VRIK control map or use the Vive facade. Rejected because FUS
   explicitly selects Cangar and its Oculus columns match the Index-as-Oculus
   strategy; Vive has a trackpad main axis.

## Reviewer questions

1. Is the current evidence enough to conclude that `trigger_click` is the likely
   missing physical event, or is a MenuMouseFix logging launch mandatory first?
2. What XRizer implementation preserves real Index OpenXR bindings but reliably
   supplies the Oculus-compatible legacy trigger/A/Grip event contract?
3. Does MenuMouseFix require a specific `ClickButton` ID under Index/Monado that
   conflicts with Cangar's menu cancel mapping? Cite code/config evidence.
4. Should RaceMenu be installed now, and if so, which FUS files/plugins are the
   minimum safe set for SKSEVR 2.0.12 / Skyrim VR 1.4.15?
5. Give a precise stop/edit/build/relaunch/headset-test sequence. Call out any
   uncertainty that needs a user test rather than a speculative patch.

## Review Disposition

| Senior recommendation | Disposition | Reason |
| --- | --- | --- |
| Log MenuMouseFix before modifying XRizer. | Adopted | The current log proves initialization only. It does not prove that the physical trigger reaches OpenVR button 33. |
| Keep `ClickButton=33`; do not use grip as a temporary click key. | Adopted | Grip is Cangar's Oculus cancel button, so it would confound the confirmation dialog. |
| If the button-33 event is absent, change only the Oculus-facade trigger-click binding. | Adopted | MenuMouseFix logged cursor movement but no mouse press/release during the headset test. The implementation is limited to the active facade and preserves Index poses, sticks, A/B, and non-STVR behavior. |
| Add RaceMenu, restore the VRIK map, or switch to the Vive facade. | Rejected for this cycle | None isolates the unproven physical trigger-to-menu-click boundary. |
