# Skyrim VR CommonLib Layout Audit

This audit compares the local Skyrim Together RE shims against the CommonLibSSE-NG VR headers in `_refs/CommonLibSSE-NG`.

The default VR target still runs in connection-only mode. Treat all findings below as blockers for enabling normal gameplay sync hooks until they are either fixed, isolated behind VR-specific accessors, or proven against Skyrim VR disassembly.

## Summary

| Area | Local status | VR risk | Action |
| --- | --- | --- | --- |
| `NiAVObject` | Data layout and VR-only virtual boundary modeled | Medium | Data layout now branches for VR tail offsets; CommonLib names/signatures are used for the first add-on virtual slots, and unapproved calls are quarantined by audit. |
| `NiTObjectArray` | Field names and offsets aligned | Low/Medium | The local array shim now uses CommonLib field names and offset assertions, avoiding misleading `length`/`capacity` labels before broader scenegraph traversal work. |
| `NiNode` | Children array layout aligned | Medium | Children now use the CommonLib `NiTObjectArray<NiPointer<NiAVObject>>` shape with SE/VR offset and size assertions; direct child-array access is quarantined by audit. |
| `TESCamera`/`PlayerCamera` | Base camera layout aligned and player-camera tail quarantined | Medium | `TESCamera` now uses the CommonLib camera-root/current-state/enabled storage and `PlayerCamera` no longer shadows those fields with stale SE-era duplicates; direct camera layout access is quarantined by audit. |
| `NiCamera` | Runtime-data and projection path aligned | Medium | `NiCamera` now records the CommonLib SE/VR world-to-camera/runtime-data2 layout, routes instance projection through the CommonLib static projection helper, and fails closed when projection globals are unresolved. |
| `TESForm` | Identity fields aligned and wrapped | Low/Medium | Form flags, form id, in-game flags, and form type match CommonLib offsets; staged VR observer identity reads and activation, equipment, object, observer, and cell-filter form-type decisions now use accessors; broader virtual names/signatures remain simplified. |
| `TESFullName`/`BGSKeywordForm` | Name and keyword components wrapped | Low | Full-name string storage and keyword array/count reads use CommonLib-aligned component accessors. |
| `TESGlobal` | Value field aligned and wrapped | Low | Global value storage matches CommonLib at `0x34`; player, party, calendar, debug, and authentication time paths use the accessor. |
| `Calendar`/`TimeData` | Global pointer fields aligned and wrapped | Low/Medium | The CommonLib `Calendar` prefix matches the local `TimeData` global pointers through `rawDaysPassed`; local tail padding remains larger and is not treated as portable. |
| `TES` singleton | Grid/cell buffers wrapped | Medium | Discovery/debug paths use CommonLib-aligned grid and image-space-list accessors; broader TES virtual/base semantics remain simplified. |
| `PlayerCharacter` | VR node block and selected runtime data wrapped | High | `VR_NODE_DATA`, `INFO_RUNTIME_DATA::skills`, and `GAME_STATE_DATA::difficulty` have accessors; tint/objective runtime storage still needs VR mapping. |
| `TESActorBase`/`TESActorBaseData` | Selected actor-base fields aligned and wrapped | Medium | Essential flags and NPC faction-list reads use CommonLib-aligned accessors; broader actor-base virtuals/template semantics remain unported. |
| `TESRaceForm`/`TESNPC` | Selected NPC tail fields aligned and wrapped | Medium | NPC initialization defaults, template/face NPC traversal, default outfit reads, original-race clearing, and head-part lookup use CommonLib-aligned accessors; broader NPC virtuals and facegen semantics remain unported. |
| `BGSOutfit`/armor body slots | Outfit array and body slot mask wrapped | Medium | Default outfit traversal and body-piece checks use CommonLib-aligned accessors; broader `TESObjectARMO` hierarchy and armor mutation remain unported. |
| `EffectSetting`/`MagicItem`/`TESQuest`/`TESWorldSpace` | Selected owner name/keyword/state fields wrapped | Low/Medium | Spell-effect keyword/archetype classification, dynamic enchant effect traversal, quest/worldspace names, quest type/stage/priority reads, and NPC name logging use CommonLib-informed accessors. |
| `MagicCaster`/`ActorMagicCaster` | Spell target/current-spell/caster fields wrapped | High | Spell-cast event capture, interrupt capture, magic service actor lookup, and player debug current-spell display use CommonLib-informed accessors; hook addresses, virtual calls, and remote spell mutation still need VR validation. |
| `TESObjectREFR` | Early CommonLib offsets wrapped by VR accessors | Medium/High | Staged VR observers may use the accessors; broad direct member use and virtual/base semantics remain blocked. |
| `Actor` | Selected CommonLib offsets wrapped by VR accessors | High | Staged read-only player state may use the accessors; normal actor sync remains blocked. |
| `CombatController` and combat tail | Selected CommonLib offsets wrapped by VR accessors | High | Target observation/debug reads can use the accessors; target selector internals and mutation hooks remain blocked. |
| `Projectile::LaunchData` | CommonLib field layout asserted | Medium/High | Struct shape is reconciled; projectile hooks/replay still need VR call-site validation and runtime testing. |
| `BSScript::IVirtualMachine` | Bind slot/size reconciled | Medium/High | Native binding uses the CommonLib slot/return shape and logs failures; broader VM virtual calls still need VR validation. |
| `ScriptEventSourceHolder` and active events | Active event-source offsets/payloads asserted | Medium | Connection-only event observers use CommonLib-shaped event payloads and event-source accessors; broader event send/push paths still need validation. |
| `IMenu`/`MenuControls` | CommonLib field offsets asserted | Medium | Layout blockers are reduced; keep input/menu hooks disabled until VR menu behavior and call sites are tested. |
| Inventory/container access | Entry/list offsets asserted and traversal wrapped | High | Equipped-form observation is allowed; full inventory sync remains blocked on validating inventory call sites, packet flow, and remote equipment mutation. |

## Patched Low-Risk Layout

`Code/client/Games/Skyrim/NetImmerse/NiAVObject.h` now has explicit `NiTransform` and `NiBound` fields instead of an opaque pad. The important offsets are asserted in both targets:

- `world` is `0x7C` for SE and VR.
- `userData` is `0xF8` for SE.
- `userData` is `0x110` for VR.
- VR `NiAVObject` size is `0x138`; SE size remains `0x110`.

`Code/client/Games/Skyrim/VR/VRPlayerPose.cpp` now reads `NiNode::world` through that local layout instead of duplicating the raw `0x7C` transform pointer math.

The local vtable declaration now includes the Skyrim VR-only `Unk_VRFunc` slot before the normal `NiAVObject` add-on virtuals, matching CommonLibSSE-NG's VR slot count at that boundary. The first add-on slots now use CommonLib's `PerformOp`, `AttachProperty`, `SetMaterialNeedsUpdate`, `SetDefaultMaterialNeedsUpdateFlag`, and `GetObjectByName` names/signatures. `GetObjectByName` remains the only approved add-on virtual call in the staged VR avatar path; `audit_commonlib_layout.py` fails direct calls to `Unk_VRFunc`, `PerformOp`, `AttachProperty`, `SetMaterialNeedsUpdate`, or `SetDefaultMaterialNeedsUpdateFlag` outside the shim declaration.

`Code/client/Games/Skyrim/NetImmerse/NiNode.h` now stores children as `NiTObjectArray<NiPointer<NiAVObject>>`, matching CommonLib's child-array ownership shape. The important offsets are asserted:

- SE `children` at `0x110`, `NiNode` size `0x128`.
- VR `children` at `0x138`, `NiNode` size `0x150`.

`Code/client/Games/Skyrim/NetImmerse/NiTObjectArray.h` now uses CommonLib-style field names:

- `_data` at `0x8`
- `_capacity` at `0x10`
- `_freeIdx` at `0x12`
- `_size` at `0x14`
- `_growthSize` at `0x16`

The range helpers iterate to `_capacity`, which preserves the previous behavior while making the field semantics match CommonLib.

`TESCamera::GetNiCamera()` now uses `NiNode::GetChildrenData()` and unwraps each `NiPointer` entry before checking RTTI. Direct `NiNode::children` access outside the shim is blocked by `audit_commonlib_layout.py`.

`Code/client/Games/Skyrim/Camera/TESCamera.h` now matches the CommonLib base camera storage used by VR:

- rotation input at `0x08`
- translation input at `0x10`
- zoom input at `0x1C`
- `cameraRoot` at `0x20`
- current camera state pointer storage at `0x28`
- enabled flag at `0x30`
- `TESCamera` size `0x38`

`PlayerCamera` keeps the CommonLib `0x168` size but its `0x38..0x168` tail is opaque until individual fields are needed and validated. This removes the old duplicated `cameraNode`/`state` fields that could shadow the base `TESCamera` storage and make camera reads come from the wrong offsets. `TESCamera::GetNiCamera()` now uses `GetCameraRoot()`, and `audit_commonlib_layout.py` fails stale camera field declarations or direct camera layout access outside the shims.

`Code/client/Games/Skyrim/NetImmerse/NiCamera.h` now carries the CommonLib camera runtime-data shape used by the current projection helpers:

- SE `worldToCam` at `0x110`, `runtimeData2` at `0x150`, size `0x188`.
- VR `worldToCam` at `0x138`, VR frustum pointer at `0x178`, opaque array block at `0x180`, `runtimeData2` at `0x1CC`, size `0x208`.
- `RuntimeData2::port` at `0x24`, matching the `NiCamera::WorldPtToScreenPt3` input port.

`NiCamera::WorldPtToScreenPt3(const NiPoint3&, ...)` now uses the object runtime-data matrix and port instead of the old unvalidated instance function address `70639`, and it now writes X/Y/Z to distinct output fields. The static projection helper uses CommonLib's `Offset::NiCamera::WorldPtToScreenPt3` ID `70640` and returns false if that function pointer or the matrix pointer is unresolved. `HUDMenuUtils::WorldPtToScreenPt3()` also returns false if its global matrix/port pointers are unavailable before calling into the projection helper.

## High-Risk Class Gaps

`RuntimeLayout`

`Code/client/Games/Skyrim/RuntimeLayout.h` is the start of a typed ABI translation layer. It centralizes generic offset access helpers (`Ref`, `Value`, `Store`, and `Ptr`) and the object/reference/cell/process/player/actor/projectile/menu/inventory/renderer offset traits currently proven from CommonLibSSE-NG:

- `TESObjectREFRCommonLibNgOffsets` and `TESObjectREFRLocalShimOffsets` for the early object/base-form, transform, parent-cell, loaded-data, and extra-data-list fields
- `TESFormCommonLibNgOffsets` and `TESFormLocalShimOffsets` for source-file storage, form flags, form id, in-game form flags, form type, and size
- `TESFullNameCommonLibNgOffsets` and `BGSKeywordFormCommonLibNgOffsets`, plus their local shim traits, for full-name string storage and keyword array/count storage
- `TESGlobalCommonLibNgOffsets` and `TESGlobalLocalShimOffsets` for global value storage and size
- `CalendarCommonLibNgOffsets` and `TimeDataLocalShimOffsets` for game-year/month/day/hour/days-passed/time-scale global pointers and raw days-passed storage
- `TESCameraCommonLibNgOffsets`, `TESCameraLocalShimOffsets`, `PlayerCameraCommonLibNgOffsets`, and `PlayerCameraLocalShimOffsets` for the base camera root/state/enabled fields and the opaque player-camera runtime tail used by camera projection helpers
- `NiCameraCommonLibNgOffsets` and `NiCameraLocalShimOffsets` for the SE/VR world-to-camera matrix, VR frustum pointer, runtime-data2 port, and camera object size used by projection helpers
- `TESActorBaseDataCommonLibNgOffsets`, `TESActorBaseDataLocalShimOffsets`, `TESActorBaseCommonLibNgOffsets`, and `TESActorBaseLocalShimOffsets` for actor-base flags, faction-list storage, actor-data subobject offset, and the local/CommonLib actor-base sizes used by current faction and essential-flag paths
- `TESRaceFormCommonLibNgOffsets`, `TESRaceFormLocalShimOffsets`, `TESNPCCommonLibNgOffsets`, and `TESNPCLocalShimOffsets` for race-form storage and the selected NPC tail fields used by current initialization, outfit, template/face, and head-part paths
- `BGSOutfitCommonLibNgOffsets`, `BGSBipedObjectFormCommonLibNgOffsets`, and `TESObjectARMOCommonLibNgOffsets`, plus their local shim traits, for default outfit traversal and armor body-slot checks
- `EffectSettingCommonLibNgOffsets`, `MagicItemCommonLibNgOffsets`, `TESQuestCommonLibNgOffsets`, and `TESWorldSpaceCommonLibNgOffsets`, plus their local shim traits, for magic-effect full-name/keyword/archetype reads, spell keyword/effect-array reads, dynamic enchant effect traversal, quest names/type/priority/current-stage fields, and worldspace names
- `SpellItemCommonLibNgOffsets` and `SpellItemLocalShimOffsets` for the `SPIT` data tail fields used by spell-cast/add-target/projectile filtering
- `MagicCasterCommonLibNgOffsets` and `ActorMagicCasterCommonLibNgOffsets`, plus their local shim traits, for desired-target, current-spell, state, caster-actor, magic-node, and casting-source fields used by gated spell-cast/interrupt paths
- `ActiveEffectCommonLibNgOffsets` and `ActiveEffectLocalShimOffsets` for spell/effect/target handles, elapsed time, duration, magnitude, flags, and casting-source fields used by active-effect debug and perk-adjustment paths
- `ValueModifierEffectCommonLibNgOffsets` and `ValueModifierEffectLocalShimOffsets` for the actor-value id and modifier value fields used by the health-effect hook
- `MagicTargetAddTargetDataCommonLibNgOffsets` and `MagicTargetAddTargetDataLocalShimOffsets` for the add-target hook/replay argument payload used by magic-effect synchronization staging
- `TESObjectCELLCommonLibNgOffsets`, `TESObjectCELLLocalShimOffsets`, `LoadedCellDataCommonLibNgOffsets`, and `LoadedCellDataLocalShimOffsets` for cell flags, reference storage, worldspace, loaded-cell-data, and encounter-zone fields
- `TESCommonLibNgOffsets` and `TESLocalShimOffsets` for the TES singleton's grid-cell pointer, center/current exterior grid coordinates, interior/exterior buffers, and active image-space modifier list
- `AIProcessCommonLibNgOffsets`, `AIProcessLocalShimOffsets`, `MiddleProcessCommonLibNgOffsets`, and `MiddleProcessLocalShimOffsets` for the process pointers used by staged equipment, active-effect, commanding-actor, and direction reads
- `PlayerCharacterInfoRuntimeDataCommonLibNgOffsets`, `PlayerCharacterGameStateDataCommonLibNgOffsets`, `PlayerCharacterVRCommonLibNgOffsets`, and `PlayerCharacterLocalShimOffsets` for VR node/runtime data and the current SE-era local player shim fields
- `ActorCommonLibNgOffsets` for non-AE/VR CommonLib offsets
- `ActorLocalShimOffsets` for the current flat local shim offsets
- `CombatControllerCommonLibNgOffsets` and `CombatControllerLocalShimOffsets` for the combat group, inventory, attacker/target handles, target selector array, active target selector, and cached attacker fields used by gated combat/debug paths
- `CombatTargetCommonLibNgOffsets`, `CombatGroupCommonLibNgOffsets`, and `CombatInventoryCommonLibNgOffsets`, plus their local shim traits, for target handles, attacker counts, group targets, and inventory maximum range reads used by gated combat/debug paths
- `CombatTargetSelectorLocalShimOffsets` for the current local target-selector fields; CommonLibSSE-NG exposes RTTI/vtable IDs and controller pointer-array storage for selectors but not the selector class member layout, so selector reads are centralized through a documented local VR shim rather than treated as CommonLib-proven
- `BSScriptIVirtualMachineCommonLibNgSlots` and `BSScriptIVirtualMachineLocalShimSlots` for the Papyrus VM slots and object size used by the VR native-binding path
- `BSTEventSourceCommonLibNgOffsets`, `ScriptEventSourceHolderCommonLibNgOffsets`, and `EventDispatcherManagerLocalShimOffsets` for the event-source size and active event source offsets used by connection-only observers
- `ProjectileCommonLibNgOffsets` and `ProjectileLocalShimOffsets` for the shifted runtime `power` field
- `IMenuCommonLibNgOffsets`, `UICommonLibNgOffsets`, and `MenuControlsCommonLibNgOffsets`, plus their local shim traits, for the menu flags, menu-stack/map, pause counter, and menu-control toggle bytes used by gated menu/input paths
- `ExtraContainerChangesCommonLibNgOffsets`, `TESContainerCommonLibNgOffsets`, `InventoryEntryCommonLibNgOffsets`, and `ExtraDataListCommonLibNgOffsets`, plus their local shim traits, for the CommonLib inventory-change, base-container, equipped-entry, and extra-data-list storage used by the staged inventory paths
- `ExtraTextDisplayDataCommonLibNgOffsets` and `ExtraTextDisplayDataLocalShimOffsets` for custom item display-name extra data used by future inventory name replay
- `EquipDataLocalShimOffsets`, `MagicEquipDataLocalShimOffsets`, and `ShoutEquipDataLocalShimOffsets` for the local native equip hook argument payloads; CommonLibSSE-NG exposes `ActorEquipManager` method signatures, but not these hook payload structs, so they remain local/tentative
- `BSGraphicsRendererDataCommonLibNgOffsets` and `BSGraphicsRendererCommonLibNgOffsets`, plus their local shim traits, for the CommonLib renderer-data window/device/context/render-target offsets used by render bring-up

The current `TESForm`, `TESFullName`, `BGSKeywordForm`, `TESGlobal`, `TimeData`, `TESCamera`, `PlayerCamera`, `NiCamera`, `TESActorBaseData`, `TESActorBase`, `TESRaceForm`, `TESNPC`, `BGSOutfit`, `BGSBipedObjectForm`, `TESObjectARMO`, `EffectSetting`, `MagicItem`, `SpellItem`, `MagicCaster`, `ActorMagicCaster`, `ActiveEffect`, `ValueModifierEffect`, `MagicTarget::AddTargetData`, `TESQuest`, `TESWorldSpace`, `TESObjectREFR`, `TESObjectCELL`, `TES`, `AIProcess`, `MiddleProcess`, `PlayerCharacter`, `Actor`, `CombatController`, `CombatTarget`, `CombatGroup`, `CombatInventory`, `BSScript::IVirtualMachine`, `EventDispatcherManager`, `Projectile`, `IMenu`, `UI`, `MenuControls`, `ExtraContainerChanges`, `TESContainer`, `InventoryEntry`, `ExtraDataList`, `ExtraTextDisplayData`, local equip hook payloads, and `BSGraphics::RendererData` semantic getters/setters or slot guards use these traits instead of carrying raw offsets directly in their class headers or hook bodies. This does not make class hierarchy, virtual dispatch, or the incompatible cell reference/combat containers portable by itself, but it gives future upstream merges a smaller boundary: gameplay code should call semantic accessors, and layout translation should stay in the runtime-layout layer plus the shim getters.

`PlayerCharacter`

CommonLibSSE-NG documents VR runtime data starting at `PlayerCharacter + 0x3D8`, with `VR_NODE_DATA` at `+0x3F0`. The local `PlayerCharacter` shim now has a typed `VRNodeData` view for the CommonLib VR node block and asserts the captured node offsets:

- `SpellOrigin` at `VR_NODE_DATA + 0x78`
- `ArrowOrigin` at `VR_NODE_DATA + 0x88`
- `LeftWandNode` at `VR_NODE_DATA + 0xA0`
- `LeftValveIndexControllerNode` at `VR_NODE_DATA + 0xB0`
- `LeftWeaponOffsetNode` at `VR_NODE_DATA + 0xC0`
- `SecondaryMagicOffsetNode` at `VR_NODE_DATA + 0xF0`
- `SecondaryMagicAimNode` at `VR_NODE_DATA + 0xF8`
- `RightWandNode` at `VR_NODE_DATA + 0x108`
- `RightValveIndexControllerNode` at `VR_NODE_DATA + 0x118`
- `RightWeaponOffsetNode` at `VR_NODE_DATA + 0x128`
- `PrimaryMagicOffsetNode` at `VR_NODE_DATA + 0x148`
- `PrimaryMagicAimNode` at `VR_NODE_DATA + 0x150`
- `UprightHmdNode` at `VR_NODE_DATA + 0x190`
- `BowAimNode` at `VR_NODE_DATA + 0x1D8`
- `BowRotationNode` at `VR_NODE_DATA + 0x1E0`

The local VR pose capture deliberately reads only these documented node pointers from that range.

The local `PlayerCharacter` class is still an SE-era flat layout for most fields, but the current high-risk direct reads have been narrowed:

- `GetSkillsData()` uses CommonLib's VR `INFO_RUNTIME_DATA::skills` field at `PlayerCharacter + 0xFD0 + 0xE0`, which resolves to the `PlayerSkills*` pointer at `PlayerCharacter + 0x10B0`.
- `Skills` now records CommonLib's `PlayerSkills::Data` payload offsets: global XP at `0x0`, global level threshold at `0x4`, per-skill data at `0x8`, legendary levels at `0xE0`, and size `0x128`; `SkillView` and player skill-experience reads use the accessors instead of direct payload fields.
- `GetDifficultyData()` uses CommonLib's VR `GAME_STATE_DATA::difficulty` at `PlayerCharacter + 0x11F4 + 0x0`.
- `GetTints()` returns an empty list in VR because CommonLib does not currently expose a validated VR tint-list path.
- `GetObjectives()` returns an empty list in VR because the VR quest/objective storage in `PLAYER_RUNTIME_DATA` is not yet mapped.
- `CanReadTintData()` and `CanReadObjectiveData()` return false in VR so assignment packets mark these payloads unavailable instead of treating the empty fallback lists as authoritative.

The remaining raw SE-era `PlayerCharacter` members must not be trusted on VR until they are ported to CommonLib-style relocated runtime-data accessors.

The staged VR observation paths now avoid the raw SE-era `locationForm` member. `DiscoveryService` and `DiscordService` use `GetCurrentLocation()` instead, and the VR discovery/save-load status writers use `GetParentCellEx()` rather than direct parent-cell reads. This keeps the current file-bus/status surface on game-function and `TESObjectREFR` accessor paths while the full VR `PLAYER_RUNTIME_DATA` layout remains unported.

`TESActorBase`/`TESActorBaseData`

CommonLibSSE-NG and the local shim agree on the actor-base data prefix used by current essential-flag and faction-list paths:

- actor base data subobject at `TESActorBase + 0x30`
- actor-base flags at `TESActorBaseData + 0x08`
- level at `TESActorBaseData + 0x10`
- base template/owner-like pointer at `TESActorBaseData + 0x30`
- faction array at `TESActorBaseData + 0x40`
- `TESActorBaseData` size `0x58`
- `TESActorBase` size `0x150`

The local `TESActorBaseData` shim now exposes `GetFlagsData()`, `SetFlagsData()`, and `GetFactionsData()`, and `TESActorBase` exposes `GetActorBaseData()`. `Actor::SetEssentialEx()` and `Actor::GetFactions()` use those accessors instead of reaching through `pBase->actorData` or `pNpc->actorData.factions`. This does not validate broader actor-base virtuals, template-form behavior, race/skin/keyword subobjects, or NPC face data.

`TESRaceForm`/`TESNPC`

CommonLibSSE-NG and the local shim agree on the selected NPC tail fields currently touched by Skyrim Together:

- race form component at `TESNPC + 0x150`
- race pointer at `TESRaceForm + 0x08`
- class at `TESNPC + 0x1C0`
- combat style at `TESNPC + 0x1D8`
- original-race/local overlay-race slot at `TESNPC + 0x1E8`
- face NPC/local template slot at `TESNPC + 0x1F0`
- default outfit at `TESNPC + 0x218`
- head-parts array at `TESNPC + 0x238`
- head-part count at `TESNPC + 0x240`
- body tint color at `TESNPC + 0x246`
- relationships at `TESNPC + 0x250`
- face data/local face morphs at `TESNPC + 0x258`
- tint layers/local tail pointer at `TESNPC + 0x260`
- `TESNPC` size `0x268`

The local `TESRaceForm` shim now exposes `GetRaceData()`/`SetRaceData()`, and `TESNPC` exposes accessors for the fields above that are already used. NPC creation/initialization now copies class, combat style, race, and default outfit through those accessors; the facegen-forcing original-race clear uses `SetOriginalRaceData(nullptr)`; `GetHeadPart()` uses the head-part array/count accessors; and `Actor::ShouldWearBodyPiece()` reads the default outfit through `GetDefaultOutfitData()`.

This is still a narrow data-layout port. It does not validate `TESNPC` virtual overrides, the exact semantic difference between the local `npcTemplate` name and CommonLib's `faceNPC` name, tint-layer ownership, facegen update calls, or remote NPC mutation in VR.

`BGSOutfit`/Armor Body Slots

CommonLibSSE-NG and the local shim agree on the default outfit storage used by the current body-piece fallback:

- `BGSOutfit::outfitItems` at `0x20`, size `0x38`
- `BGSBipedObjectForm` slot mask at component offset `0x08`, armor type at `0x0C`, size `0x10`
- `TESObjectARMO` CommonLib `BGSBipedObjectForm` subobject at `0x1B0`
- `TESObjectARMO` slot mask at `0x1B8`

The local `BGSOutfit` shim now exposes `GetOutfitItemsData()`, `BGSBipedObjectForm` exposes `GetSlotMaskData()`/`HasPartData()`, and `TESObjectARMO::IsBodyPiece()` reads the CommonLib slot-mask offset instead of the raw local `slotType` member. `Actor::ShouldWearBodyPiece()` uses `GetOutfitItemsData()` plus `TESForm::GetFormTypeData()`, and the armor inventory filters now use `GetFormTypeData()` instead of raw `formType` reads.

The local `TESObjectARMO` class is still not a full CommonLib armor hierarchy; it only exposes the slot mask needed by the existing body-piece check. Keep armor mutation/equip replay gated until `TESBipedModelForm`, armor addons, equip type, keyword form, and armor template behavior are validated for VR.

`TESForm`

CommonLibSSE-NG and the local shim agree on the `TESForm` identity storage used by the staged observers:

- source-file container at `0x08`
- form flags at `0x10`
- form id at `0x14`
- in-game form flags at `0x18`
- form type at `0x1A`
- size `0x20`

The local `TESForm` shim now exposes `GetFormFlagsData()`, `SetFormFlagsData()`, `GetFormIdData()`, `GetInGameFormFlagsData()`, `SetInGameFormFlagsData()`, and `GetFormTypeData()`. Its own disabled/temporary/consumable/friendly-hit helpers and save/change helpers use those accessors. Core synced actor/reference/equipment/player/combat/projectile/magic/container/animation paths, the VR `DiscoveryService` observation path, Discord presence location tracking, transport authentication, player-service spell/power/race identity caching, gated quest-service identity reads, weather diagnostics, and debug/diagnostic form-id display paths now use these accessors for form identity, form type, and form flags instead of direct member reads; `Tools/SkyrimVR/audit_commonlib_layout.py` guards those paths.

`TESGlobal`

CommonLibSSE-NG and the local shim agree on the current global value storage:

- value at `0x34`
- size `0x38`

The local `TESGlobal` shim now exposes `GetValueData()` and `SetValueData()`. Player-service connection/disconnection, party-state writes, calendar sync, authentication time capture, and calendar debug display use these accessors instead of the raw union member.

`Calendar`/`TimeData`

CommonLibSSE-NG names this singleton `Calendar`; the local shim names the same early object `TimeData`. The proven shared prefix is:

- game year global pointer at `0x08`
- game month global pointer at `0x10`
- game day global pointer at `0x18`
- game hour global pointer at `0x20`
- game days passed global pointer at `0x28`
- time scale global pointer at `0x30`
- midnights passed/raw local field at `0x38`
- raw days passed at `0x3C`

CommonLib's documented `Calendar` size is `0x40`; the local `TimeData` shim still pads to `0xC8`. The current VR-safe accessors only expose the aligned global pointers. Calendar sync, authentication time capture, and calendar debug display call `GetGameYearData()`, `GetGameMonthData()`, `GetGameDayData()`, `GetGameHourData()`, `GetGameDaysPassedData()`, and `GetTimeScaleData()` before reading or writing the underlying `TESGlobal` values.

`TESObjectREFR`

CommonLib models `TESObjectREFR` as:

- `TESForm`
- `BSHandleRefObject`
- `BSTEventSink<BSAnimationGraphEvent>`
- `IAnimationGraphManagerHolder`

The local shim embeds similar concepts as members after `TESForm`. Some field offsets may line up, but base-pointer adjustment, virtual dispatch, and event sink calls are not equivalent. This affects activation, inventory, animation, loaded 3D, cell movement, disable/enable, and handle policy code.

The local shim now provides narrow VR-aware accessors for the early fields whose offsets match CommonLibSSE-NG's non-AE/VR layout:

- object/base form at `0x40`
- rotation at `0x48`, with a narrow mutation wrapper for staged remote-spawn rotation setup
- position at `0x54`
- parent cell at `0x60`
- loaded data at `0x68`
- extra data list at `0x70`

In the non-VR target these accessors return the existing local members. In the VR target they read or write by the CommonLib offsets. `GetCellId()`, `GetWorldSpace()`, `GetExtraDataList()`, `MoveTo()`, `GetParentCellEx()`, object discovery/activation checks, cell reference filtering, pickup-hook item identification, remote-spawn rotation setup, and character-assignment packet construction now use these accessors so the staged VR observers and assignment staging can read identity, cell, position, and rotation without depending on the rest of the local flat class size.

Remaining risk: this does not make `TESObjectREFR`'s inheritance model, virtual slots, animation graph holder, loaded-data model, or handle policy fully CommonLib-compatible. Keep object mutation and full activation/inventory/object sync disabled until those are ported.

`TESObjectCELL`

CommonLibSSE-NG models `TESObjectCELL` late fields as runtime data. The local shim still exposes flat members, and the local `ReferenceData` helper does not match CommonLib's `BSTSet<NiPointer<TESObjectREFR>>` reference container.

The local cell shim now exposes narrow VR-aware accessors for the CommonLib non-AE/VR offsets used by staged paths:

- cell flags at `0x40`
- references at `0x80`, deliberately not traversed by the VR target yet
- worldspace at `0x120`
- loaded cell data at `0x128`

`TESObjectREFR::GetCellId()`, `TESObjectREFR::GetWorldSpace()`, `TESObjectREFR::MoveTo()`, `Actor::Create()`, object player-home filtering, and VR activation telemetry now use those accessors instead of raw `TESObjectCELL` fields. In the VR target, `TESObjectCELL::GetReferenceData()` returns `nullptr` so full object discovery cannot walk the incompatible local reference-array shim by accident. The non-VR fallback reference-array helper now exposes local `GetReferenceArrayData()`, `GetCapacityData()`, and `GetReferenceAtData()` wrappers so `GetRefsByFormTypes()` does not reach through `pReferenceData->refArray` or `pReferenceData->capacity` directly. Keep full object discovery and object sync gated until the cell reference container is ported to the CommonLib runtime-data shape.

The nested loaded-cell-data shim now also records CommonLibSSE-NG's `LOADED_CELL_DATA::encounterZone` at `0x160` and size `0x180`. Object player-home filtering reads the encounter zone through `LoadedCellData::GetEncounterZoneData()` instead of `pLoadedCellData->encounterZone`.

`TES` singleton

The local `TES` singleton shim now records the CommonLib-aligned offsets used by staged exterior-cell discovery and debug helpers:

- grid-cell array pointer at `0x78`
- center grid X/Y at `0xB0`/`0xB4`
- current grid X/Y at `0xB8`/`0xBC`
- interior cell at `0xC0`
- interior/exterior cell buffers at `0xC8`/`0xD0`
- active image-space modifiers at `0x108`

`DiscoveryService`, the remote spawn range check, the player debug grid readout, the clear-screen-effects debug action, and the player revive fallback now use `GetCenterGridXData()`, `GetCenterGridYData()`, `GetCurrentGridXData()`, `GetCurrentGridYData()`, or `GetActiveImageSpaceModifiersData()` instead of raw TES singleton fields. CommonLib leaves some of the `0xB0..0xBC` fields unnamed, so the semantic names come from the local shim and current game behavior rather than CommonLib naming. The broader `TES` inheritance/event-sink model and reference iteration helpers remain unported for VR.

`Actor`

CommonLib models the non-AE `Actor` base chain as:

- `TESObjectREFR`
- `MagicTarget`
- `ActorValueOwner`
- `ActorState`
- two event sinks
- `IPostAnimationChannelUpdateFunctor`

The local shim inherits only from `TESObjectREFR` and embeds the other subobjects as fields. This is the main blocker for normal gameplay sync. Movement, combat, actor values, magic casting, actor inventory, kill/resurrect, mount, package, and animation hooks must remain gated until this is fixed or replaced with narrow VR-safe calls.

CommonLibSSE-NG sizes non-AE/VR `TESObjectREFR` as `0x98`; the local shim is still `0xA0`. That shifts the local embedded `Actor` members by `+0x8` from CommonLib. For example, CommonLib places `MagicTarget` at `0x98`, `ActorValueOwner` at `0xB0`, `ActorState` at `0xB8`, `currentProcess` at `0xF0`, cached magic casters at `0x1A0`, selected spells at `0x1C0`, selected power at `0x1E0`, race at `0x1F0`, the actor flag field at `0x1FC`, and health/magicka/stamina/voice modifier blocks at `0x228`, `0x234`, `0x240`, and `0x24C`, while the local flat members sit eight bytes later and keep the old stamina-before-magicka local order.

The local `Actor` shim now exposes narrow VR-aware accessors for the selected CommonLib offsets used by connection-only staging:

- `GetActorStateData()`
- `GetMagicTargetData()`
- `GetActorValueOwnerData()`
- `GetBoolBitsData()`
- `GetCurrentProcessData()`
- `GetCombatControllerData()`
- `GetCachedMagicCasterData(slot)`
- `SetCachedMagicCasterData(slot, value)`
- `GetSelectedSpellData(slot)`
- `GetSelectedPowerOrShoutData()`
- `SetSelectedPowerOrShoutData(value)`
- `GetRaceData()`
- `GetBoolFlagsData()`
- `SetBoolFlagsData(value)`
- `GetHealthModifiersData()`
- `GetMagickaModifiersData()`
- `GetStaminaModifiersData()`
- `GetVoiceModifiersData()`
- `GetTemporaryHealthModifierData()`

`ActorState` also exposes packed flag helpers (`GetFlags1Data()`, `GetFlags2Data()`, and `SetFlagsData()`) so animation sync no longer reaches into the embedded packed state fields directly.

The staged VR inventory/auth/player-service reads use these accessors instead of raw `pPlayer->actorState`, `pPlayer->magicItems`, `pPlayer->equippedShout`, `pPlayer->race`, or `pPlayer->currentProcess` reads. Additional read paths in character assignment, actor-value setup, actor level-mod extra data, actor save bool-bits staging, inventory weapon-draw updates, magic spell lookup/effect application, magic equipment snapshots, magic caster cache generation, magic queue actor-name logging, overlay health-percentage calculation, beast-form/vampire-lord detection, animation event capture/replay/serialization, behavior-variable graph compatibility, interpolation direction updates, actor spawn logging, spawned-reference base-form lookup, player NPC initialization, commanded-actor setup, essential/mount/commanded flag helpers, debug/diagnostic views, faction reads, body-piece checks, facegen base-form lookup, unequip-all selected shout clearing, and pickup hooks now use the CommonLib-informed accessors instead of the shifted flat fields. Normal actor sync is still blocked because the class hierarchy, base-pointer adjustment, broader actor-value/magic-target virtual semantics, event sinks, process model, AI packages, animation graph holder, animation mutation methods, and deeper process model are not yet ported.

`CombatController`

CommonLibSSE-NG's non-AE/VR `CombatController` layout diverges from the current local shim after the aim-controller block. The shared early fields stay aligned (`combatGroup` at `0x0`, `inventory` at `0x10`, attacker handle at `0x28`, target handle at `0x2C`, and `startedCombat` at `0x35`), but the target selector tail differs:

- CommonLib non-AE/VR target selectors at `0x98`
- CommonLib non-AE/VR active target selector at `0xB0`
- CommonLib non-AE/VR cached attacker at `0xC8`
- CommonLib non-AE/VR size `0xD8`
- local shim target selectors at `0xA0`
- local shim active target selector at `0xB8`
- local shim cached attacker at `0xD0`
- local shim size `0xE0`

The local `CombatController` shim now exposes `GetCombatGroupData()`, `GetInventoryData()`, `GetAttackerHandleData()`, `GetTargetHandleData()`, `GetStartedCombatData()`, `GetTargetSelectorsData()`, `GetActiveTargetSelectorData()`, `SetActiveTargetSelectorData()`, and `GetCachedAttackerData()`. `CombatController::UpdateTarget()` and the debug combat view use those accessors rather than direct shifted controller fields.

The immediate combat tail now wraps the CommonLib-proven fields the debug/target update paths already read:

- `CombatTarget` target handle at `0x0`, attacker count at `0xA4`, flags at `0xA6`, size `0xA8`
- `CombatGroup` targets array at `0x8`, size `0x168`
- `CombatInventory` maximum range at `0x1B8`, size `0x1C8`

`CombatController::UpdateTarget()` and the debug combat view now use these accessors rather than direct target-selector, group, target, or inventory fields. `CombatTargetSelector` itself remains a local-shim wrapper only: CommonLibSSE-NG exposes selector RTTI/vtable IDs and the controller's selector array, but not a full selector class layout header. The selector now exposes VR `RuntimeLayout` accessors for controller, target handle, priority, and flags, so current combat target-update reads are centralized, but those offsets are still local/tentative rather than CommonLib-proven. Keep normal combat sync and combat target mutation gated until `CombatTargetSelector`, combat target scoring helpers, `SetTarget`, and the disabled combat target update hook are validated against Skyrim VR.

`InventoryEntryData`, `ExtraContainerChanges`, `TESContainer`, and `ExtraDataList`

The current VR target now has static assertions for the inventory entry/list pieces used by the read-only `VRInventoryService`:

- `ExtraContainerChanges` remains `0x18`, with the `changes` pointer at `0x10`.
- `ExtraContainerChanges::Entry` now matches CommonLib's `InventoryEntryData` shape: object/form at `0x0`, extra-list pointer at `0x8`, count/count-delta at `0x10`, size `0x18`.
- `ExtraContainerChanges::Data` matches CommonLib's `InventoryChanges` storage size and first fields: entry list at `0x0`, owner/parent at `0x8`, total weight at `0x10`, armor weight at `0x14`, size `0x20`.
- `TESContainer::Entry` matches CommonLib's `ContainerObject` storage: count at `0x0`, object/form at `0x8`, extra data at `0x10`, size `0x18`.
- `TESContainer` is asserted as `0x18`, with entries at `0x8` and count at `0x10`.
- `InventoryEntry` matches CommonLib's `InventoryEntryData` offsets used by `MiddleProcess` equipped-object pointers.
- `ExtraDataList` asserts the vptr/data/presence/lock offsets used by extra-data reads.
- `AIProcess` exposes `GetMiddleProcessData()` for the `0x8` middle-high pointer that CommonLib names `middleHigh`, with the offset recorded in `RuntimeLayout`.
- `MiddleProcess` routes the current narrow CommonLib-style offsets through `RuntimeLayout`: direction/rotation tail at `0xB8`, active effects at `0x1A0`, commanding actor at `0x218`, equipped left/right entry offsets at `0x220` and `0x260`, and equipped ammo at `0x268`.

This is enough for the current staged observer to read equipped weapon/ammo form pointers and magic/power form fields, and for the remaining actor helper paths to avoid direct `currentProcess->middleProcess` reach-through. The local `ExtraContainerChanges::Data` now exposes CommonLib-style `GetEntryList()`, `FindEntryByFormId()`, and `VisitInventory()` wrappers, and `TESObjectREFR::GetItemCountInInventory()`/`GetInventory()` use those wrappers instead of raw `GetContainerChanges()->entries` traversal.

`EquipManager` hook payloads now have local `RuntimeLayout` traits and semantic accessors for the fields used by equipment event capture:

- `EquipData` extra-data list at `0x00`, count at `0x08`, slot at `0x10`, slot-to-replace at `0x18`, queue/force/play/apply flags from `0x20` through `0x23`, and size `0x28`
- `MagicEquipData` equip slot at `0x00`, queue/force flags at `0x08`/`0x09`, and size `0x10`
- `ShoutEquipData` unknown pointer/flag at `0x00`/`0x08`, and size `0x10`

`EquipHook`, `UnEquipHook`, `EquipSpellHook`, and `UnEquipSpellHook` now read count, slot, and queue state through those accessors instead of direct `apData->count`, `apData->pSlot`, `apData->pEquipSlot`, or `apData->bQueueEquip` member access. These payload layouts are not CommonLib-proven; they must be checked against VR disassembly before normal equipment hooks are enabled for VR.

This still does not enable full inventory or animation sync in the default VR target. `TESObjectREFR::GetInventory()` still calls game functions and extra-data helpers that need VR runtime validation, and remote packet/equipment, process-direction, and animation mutation paths remain disabled until spawned actor ownership, process lifetime, HIGGS interaction, and inventory call sites are tested.

`Projectile::LaunchData`

The local `LaunchData` shim now follows the CommonLibSSE-NG field sequence and asserts every field used by projectile launch/replay:

- `origin` at `0x08`
- `contactNormal` at `0x14`
- projectile base at `0x20`
- shooter at `0x28`
- combat controller at `0x30`
- weapon/ammo source at `0x38`/`0x40`
- `angleZ`/`angleX` at `0x48`/`0x4C`
- `unk50`, `desiredTarget`, `unk60`, and `unk64` at `0x50` through `0x64`
- parent cell at `0x68`
- spell at `0x70`
- casting source at `0x78`
- `pad7C` at `0x7C`
- enchantment and poison at `0x80`/`0x88`
- area, power, and scale at `0x90` through `0x98`
- launch bools from `alwaysHit` at `0x9C` through `forceConeOfFire` at `0xA2`

The old local `fYAngle` slot was incorrect: CommonLib identifies `0x50` as `unk50`, not a third angle. The normal projectile protocol still carries `YAngle` for compatibility, but local `LaunchData` no longer reads or writes it into the game structure. The previous unknown bool at `0x7C` was also padding, and the bool at `0x9F` is now named `chainShatter`.

`Projectile::LaunchData` now exposes CommonLib-backed getters/setters for the launch payload fields used by Skyrim Together. `HookLaunch` reads spell, shooter, origin, projectile base, weapon/ammo, parent cell, casting source, area, power, scale, and launch flags through those accessors, and `CombatService` constructs remote replay launch data through the matching setters instead of writing raw `launchData.*` fields.

The local `Projectile` shim also exposes `GetPowerData()` and `SetPowerData()` for the CommonLib runtime `power` field at `0x188`. The local flat member still sits at `0x190`, so `Projectile::Launch()` and `HookLaunch()` now use the accessors instead of reading or writing `fPower` directly.

Projectile launch sync should still stay disabled for VR. The struct shape is better documented and guarded, but `Projectile::Launch`, `HookLaunch`, the null-shooter inline patch, remote projectile replay, runtime projectile ownership, and HIGGS-sensitive projectile behavior still need VR call-site validation and in-game testing.

`BSScript::IVirtualMachine`

The local `IVirtualMachine` vtable now records the CommonLibSSE-NG slot indices used by current Skyrim Together code:

- `GetScriptObjectType1` at slot `0x09`
- `BindNativeMethod` at slot `0x18`
- `SendEvent` at slot `0x24`
- `GetObjectHandlePolicy` at slot `0x2D`

The local shim now matches CommonLib's `0x10` interface object size by carrying the intrusive ref-count tail, and `BindNativeMethod` returns `bool` like CommonLib. The VR native-binding hook uses that return value and logs the Papyrus class/function name when a Skyrim Together native fails to bind.

CommonLib's VR declaration still has more precise object/type smart-pointer signatures and VR-only methods around the bound-object region. The current VR bring-up path only installs the native binding hook and then calls `BindNativeMethod`. Keep broader Papyrus/object-handle usage disabled or audited before relying on `GetScriptObjectType1`, `SendEvent`, or object handle calls in VR gameplay systems.

`ScriptEventSourceHolder` and active events

CommonLibSSE-NG names the event-source singleton `ScriptEventSourceHolder`; the local shim still calls it `EventDispatcherManager`. The active connection-only sources have the same `0x58` stride as CommonLib's `BSTEventSource<T>` and are now routed through accessors:

- activate event source at `0x58`
- hit event source at `0x5D8`
- load-game event source at `0x688`
- magic-effect-apply event source at `0x738`
- spell-cast event source at `0xE18`
- player-bow-shot event source at `0xE70`

The VR tail differs from SE: CommonLib VR stops at `TESSwitchRaceCompleteEvent` and sizes the holder as `0x1238`, while SE includes `TESFastTravelEndEvent` at `0x1238` and sizes the holder as `0x1290`. The local struct now branches that tail on `TP_SKYRIM_VR`.

The active event payload shims now match the CommonLib field shapes used by the observers:

- `TESActivateEvent`: activated object and action ref as two `NiPointer<TESObjectREFR>` fields, size `0x10`
- `TESHitEvent`: target, cause, source, projectile, and the CommonLib-shaped flag byte/padding word at `0x18`, size `0x20`
- `TESMagicEffectApplyEvent`: target, caster, and magic effect form id, size `0x18`
- `TESSpellCastEvent`: object and spell id, size `0x10`
- `TESPlayerBowShotEvent`: weapon, ammo, shot power, and sun-gazing flag, size `0x10`

`VRActivationService`, `VRCombatService`, `VRMagicService`, `VRProjectileService`, `VRSaveLoadService`, and the VR discovery load-game observer register through those event-source accessors and read payloads through semantic getters. This does not validate event sending, all event types, or `PushEvent` call sites for VR.

`IMenu` and `MenuControls`

`IMenu` broadly matches CommonLib's early virtuals and flag fields. `RuntimeLayout` records the menu-flag/input-context/delegate offsets plus the VR-only trailing fields at `0x30`, `0x34`, and `0x38` so the VR size is `0x40` while the SE size remains `0x30`. Menu flag readers and writers, including the debug menu-stack dump and pause/freeze helpers, now go through `GetMenuFlagsData()`/`SetMenuFlagsData()` instead of reaching into `uiMenuFlags` directly.

`UI` now asserts the CommonLib menu-stack and menu-map offsets through `RuntimeLayout`: `menuStack` at `0x110`, `menuMap` at `0x128`, and `numPausesGame` at `0x160`. Those paths now use `GetMenuStackData()` and `GetMenuMapData()` rather than direct member traversal.

`MenuControls` now matches the CommonLibSSE-NG storage used by the current local toggle path, with the offsets recorded in `RuntimeLayout` and `SetToggle()` routed through `SetToggleData()`:

- `isProcessing` at `0x80`
- `beastForm` at `0x81`
- `remapMode` at `0x82`
- `unk83` at `0x83`
- `unk84` at `0x84`
- size `0x88`

The current local use only writes the byte at `0x83` through `SetToggle`, and input services are skipped in connection-only mode. The byte is intentionally named `unk83` rather than `canBeOpened` because CommonLib does not assign that semantic name.

`Inventory/container shims`

`ExtraContainerChanges`, `TESContainer`, `InventoryEntry`, and `ExtraDataList` now route the CommonLib-style inventory storage through `RuntimeLayout` traits. `TESObjectREFR::GetInventory()` and equipped-object reads use `GetEntryList()`, `VisitInventory()`, `GetEntriesData()`, `GetFormData()`, `GetCountData()`, and `InventoryEntry::GetObjectData()` instead of reaching through raw `entries`, `form`, `count`, or `pObject` members. `ExtraDataList::New()`, `Contains()`, `GetByType()`, `Add()`, `GetCount()`, and `SetType()` use `GetDataData()`, `GetBitfieldData()`, and `GetLockData()`.

`TESLeveledList` records the CommonLib `SimpleArray<LEVELED_OBJECT>` data pointer at `0x08`, the entry form pointer at `0x00`, the entry count byte at `0x12`, and size `0x28`. `Actor::ShouldWearBodyPiece()` now reads the first leveled-list entry through `GetEntriesData()`, `GetEntryCountData()`, and `LEVELED_OBJECT::GetFormData()` rather than `pLeveledListA` or the entry's raw form field. The leveled-item branch uses the already-checked `FormType::LeveledItem` to perform the narrow `TESLevItem` downcast, avoiding a runtime `Cast<TESLevItem>` lookup while that RTTI ID remains unresolved in the VR address sources.

`ExtraFactionChanges` records the faction-change array at `0x10`; CommonLib's full object is larger (`0x38`) because it also stores crime-faction state after the array, while the local shim only needs the shared array prefix. `Actor::GetFactions()` now reads extra faction changes through `GetEntriesData()` rather than `pChanges->entries`.

Inventory extra-data payloads now have CommonLib-backed accessors for the fields used by `TESObjectREFR::GetItemFromExtraData()`:

- `ExtraCount::count` at `0x10`
- `ExtraCharge::charge` at `0x10`
- `ExtraHealth::health` at `0x10`
- `ExtraEnchantment::enchantment`, `charge`, and `removeOnUnequip` at `0x10`, `0x18`, and `0x1A`
- `ExtraPoison::poison` and `count` at `0x10` and `0x18`
- `ExtraSoul::soul` at `0x10`; the VR path reads this as the CommonLib byte-sized enum value
- `ExtraTextDisplayData::displayName`, `displayNameText`, `ownerQuest`, `ownerInstance`, `temperFactor`, and `customNameLength` at `0x10`, `0x18`, `0x20`, `0x28`, `0x2C`, and `0x30`
- `EffectItem` magnitude/area/duration, base effect, and raw cost at `0x00`, `0x04`, `0x08`, `0x10`, and `0x18`

`TESObjectREFR::GetItemFromExtraData()`, `EffectItem` classification helpers, `MagicItem` spell classification/effect lookup, `MagicTarget` add-target event capture, and dynamic `EnchantmentItem` construction now use those accessors instead of directly reading or writing extra-data payload and effect-item members while building inventory, magic, and enchantment snapshots.

The old custom-display-name read/write blocks in `TESObjectREFR` remain disabled because they were already marked as crash-prone. They now document the accessor path (`GetDisplayNameStringData()`, `SetDisplayNameData()`, `SetCustomNameLengthData()`, `SetOwnerInstanceData()`, and `SetTemperFactorData()`) for future revalidation instead of preserving raw field writes.

This still does not validate the full inventory mutation hook set for VR. It only narrows the current observation/equipment paths to the CommonLib-backed offsets and keeps full inventory synchronization gated until the related call sites are tested.

`MagicCaster` and `ActorMagicCaster`

CommonLibSSE-NG's non-AE/VR `MagicCaster` prefix matches the local shim used here: sound handles at `0x08`, desired target at `0x20`, current spell at `0x28`, state at `0x30`, timers/cost fields through `0x44`, and size `0x48`. `ActorMagicCaster` also matches the current local tail fields used by Skyrim Together: caster actor at `0xB8`, magic node at `0xC0`, cost charged at `0xF0`, casting source at `0xF4`, flags at `0xF8`, and size `0x100`.

`MagicCaster` now exposes `GetDesiredTargetData()`, `GetCurrentSpellData()`, and `GetStateData()`. `ActorMagicCaster` exposes `GetCasterActorData()`, `GetMagicNodeData()`, and `GetCastingSourceData()`. `Actor` exposes `GetCachedMagicCasterData()` and `SetCachedMagicCasterData()` for the cached caster array. `HookSpellCast`, `HookInterruptCast`, `MagicService`, `Actor::GenerateMagicCasters()`, and the player debug current-spell display use those accessors instead of direct `pCasterActor`, `pCurrentSpell`, `hDesiredTarget`, `eCastingSource`, or `Actor::casters[]` member reads.

This reduces the data-layout risk for future magic bring-up work, but it does not validate the `SpellCast`/`InterruptCast` hook addresses, `CastSpellImmediate`, `InterruptCast`, caster virtual dispatch, remote spell-cast replay, or magic-target mutation in Skyrim VR. Those paths must stay gated until their VR call sites are checked and tested in game.

`SpellItem`

CommonLibSSE-NG's `SpellItem` places `MagicItem` at `0x00`, `BGSEquipType` at `0x90`, `BGSMenuDisplayObject` at `0xA0`, `TESDescription` at `0xB0`, and the `SPIT` data block at `0xC0`. The local shim now carries the full tail through casting perk, with casting type at `0xD0`, delivery at `0xD4`, cast duration at `0xD8`, range at `0xDC`, casting perk at `0xE0`, and size `0xE8`.

`SpellItem` now exposes `GetCastingTypeData()` and `GetDeliveryData()`. `MagicService` spell-cast/add-target filters and the projectile launch filter use `GetCastingTypeData()` instead of reading `eCastingType` directly.

This narrows the spell data boundary used by current sync gating, but it does not validate `SpellItem` virtual dispatch or all `SPIT` data consumers in Skyrim VR.

`EnchantmentItem`

CommonLibSSE-NG's `EnchantmentItem` places the `ENIT` data block immediately after `MagicItem` at `0x90`. The local shim now records the same block: cost override at `0x90`, flags at `0x94`, casting type at `0x98`, charge override at `0x9C`, delivery at `0xA0`, spell type at `0xA4`, charge time at `0xA8`, base enchantment at `0xB0`, worn restrictions at `0xB8`, and size `0xC0`.

`EnchantmentItem` now exposes CommonLib-backed getters for the full `ENIT` payload: cost override, flags, casting type, charge override, delivery, spell type, charge time, base enchantment, and worn restrictions.

This narrows the enchantment data boundary used by inventory/equipment and dynamic enchantment construction, but it does not validate the created-object manager call targets or full enchantment mutation behavior in Skyrim VR.

`MagicTarget::AddTargetData`

CommonLibSSE-NG's `MagicTarget::AddTargetData` matches the local hook/replay payload size and the fields used by Skyrim Together: caster at `0x00`, spell/magic item at `0x08`, effect payload at `0x10`, source at `0x18`, post-creation callback at `0x20`, results collector at `0x28`, explosion location at `0x30`, magnitude at `0x3C`, unknown float at `0x40`, casting source at `0x44`, area-target byte at `0x48`, dual-cast byte at `0x49`, and size `0x50`.

`AddTargetData` now exposes CommonLib-backed getters/setters for caster, spell, effect, magnitude, unknown float, casting source, and dual-cast state. `HookAddTarget` and `MagicService` remote-effect replay construction use those accessors instead of direct `arData.pCaster`, `arData.pSpell`, `arData.pEffectItem`, `arData.fMagnitude`, `arData.bDualCast`, or `data.*` payload writes.

This narrows the add-target data argument boundary, but it does not validate the `AddTarget`, `CheckAddEffect`, `FindTargets`, or perk hook addresses against Skyrim VR.

`ActiveEffect`

CommonLibSSE-NG's `ActiveEffect` layout matches the local shim size and field offsets currently used by Skyrim Together: caster handle at `0x34`, spell at `0x40`, effect payload at `0x48`, target at `0x50`, elapsed time at `0x70`, duration at `0x74`, magnitude at `0x78`, flags at `0x7C`, and casting source at `0x88`, with size `0x90`.

`ActiveEffect` now exposes CommonLib-backed getters/setters for caster handle, spell/effect/target pointers, elapsed time, duration, magnitude, flags, and casting source. `HookAdjustForPerks`, the invisibility finish hook, and the disabled form-debug active-effect snippet use those accessors instead of direct `pSpell`, `pTarget`, `fElapsedSeconds`, `fDuration`, `fMagnitude`, or `uiFlags` member reads/writes.

This narrows the active-effect data boundary, but it does not validate the active-effect hook targets, virtual dispatch, or remote effect application behavior in Skyrim VR.

`ValueModifierEffect`

CommonLibSSE-NG's `ValueModifierEffect` tail matches the local shim: actor-value id at `0x90`, value at `0x94`, and size `0x98`. The shim now exposes `GetActorValueIndexData()`, `GetValueData()`, and `SetValueData()`, and the health-effect hook uses `GetActorValueIndexData()` rather than reading `actorValueIndex` directly.

This only validates the data tail used by the hook. The `ApplyActorEffect` hook address and effect application behavior still need VR disassembly/runtime validation before normal active-effect synchronization is enabled.

`Save/load form buffers`

`BGSSaveGameBuffer`, `BGSSaveFormBuffer`, `BGSLoadGameBuffer`, and `BGSLoadFormBuffer` now have `RuntimeLayout` traits matching CommonLibSSE-NG's buffer/form-data layout:

- save buffer pointer/size/position at `0x8`, `0x10`, and `0x14`
- save form header at `0x18`, with packed change flags at `0x1B`
- load buffer pointer/size/position at `0x8`, `0x10`, and `0x24`
- load form id, size, form pointer, change flags, old change flags, and version at `0x28`, `0x2C`, `0x38`, `0x40`, `0x44`, and `0x4B`

`BGSSaveFormBuffer::WriteId`, `BGSLoadFormBuffer_LoadFormId`, and the NPC serialize/deserialize wrappers now use semantic buffer/form/change-flag accessors instead of direct `buffer`, `capacity`, `position`, `changeFlags`, `formId`, or `form` member writes. The save header's packed change-flag field is written through unaligned `RuntimeLayout` helpers because it starts at byte offset `0x1B`.

`BGSSaveLoadManager::SaveData` is not exposed by the current CommonLib references used here. The old `BGSSaveLoadManager::Save` stub now reaches `flags` and `saveName` through accessors that preserve the existing non-VR behavior but return/no-op on VR, so the VR target does not touch the unverified `SaveData` payload.

This only validates Skyrim Together's overridden form-id serialization path. It does not validate Bethesda's full save/load manager policy, transport reconnect/disconnect behavior, actor cleanup, or the full gameplay save/load hook set for VR.

`RendererData`

CommonLibSSE-NG models `BSGraphics::RendererData` with the same early renderer offsets used by the local render hook: forwarder at `0x38`, immediate context at `0x40`, render windows at `0x48`, render targets at `0xA48`, depth stencils at `0x1FA8`, and cube-map render targets at `0x26C8`. The local shim now records these in `RuntimeLayout` and exposes `GetRendererData()`, `GetForwarderData()`, `GetContextData()`, and `GetRenderWindowData()`.

`Hook_Renderer_Init` now uses those accessors before handing the D3D11 swap chain/device/context to `RenderSystemD3D11`. This does not enable the flat overlay for VR, but it keeps render bring-up logging and future VR-safe overlay work from depending on raw `self->Data` and `RendererData` member reads.

Do not enable menu/input hooks for VR until `MenuControls`, `InputEvent`, and VR menu stack behavior are tested in-game.

## Verification Notes

Focused Windows-ABI syntax checks passed for the Skyrim VR `NiAVObject.h` branch with `TP_SKYRIM_VR=1`.

The `PlayerCharacter::VRNodeData` offset assertions are local compile-time assertions, but the full `PlayerCharacter.h` include graph still cannot be isolated under the Linux mingw compiler because the surrounding Skyrim form hierarchy depends on the MSVC build environment and unresolved Tilted PCH context.

The full `VRPlayerPose.cpp` source cannot be meaningfully compile-checked with the local Linux mingw compiler because the surrounding Skyrim form hierarchy depends on MSVC C++ object layout. The Windows SKSE DLL target still needs a real MSVC/Windows SDK xmake environment.

`Tools/SkyrimVR/audit_commonlib_layout.py` now includes `CharacterService.cpp`, `ObjectService.cpp`, `WeatherService.cpp`, `MagicService.cpp`, `ActorValueService.cpp`, `SubtitleManager.cpp`, `SaveLoad.cpp`, `Forms.cpp`, behavior-variable compatibility, selected debug views, and weather hook logging so full actor/object/weather/magic/actor-value/subtitle/save-load identity paths do not reintroduce raw `formID`, `formType`, `baseForm`, `parentCell`, `position`, `rotation`, `actorState`, `flags`, lock-level, process, magic-caster, or save/load buffer member reads while those systems remain gated for VR. It also checks `NiTObjectArray` field names/offsets, direct calls to the quarantined `NiAVObject` virtual placeholders, direct access to `NiNode::children`, stale camera field declarations, direct camera layout access outside the camera shims, and the `NiCamera` projection boundary. String and character literals are ignored before matching so diagnostic text can still use game terminology without masking real member access. `Tools/SkyrimVR/audit_vr_services.py` separately checks that the staged VR observers, authentication paths, behavior-variable compatibility, and debug/weather surfaces use the CommonLib-aware accessors and do not reintroduce raw shifted player/reference member reads for `baseForm`, `parentCell`, `position`, `rotation`, `actorState`, `magicItems`, `equippedShout`, `currentProcess`, raw `formID`, or the SE-era `PlayerCharacter::locationForm`.
