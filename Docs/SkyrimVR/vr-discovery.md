# VR Discovery Observation

The VR targets enable the `DiscoveryService` handoff behind `TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE=1`.

In the default connection-only package this is a narrow observation stage. It
discovers local cell/grid/location changes, but actor-handle enumeration is
disabled until the explicit avatar-sync/gameplay targets. The gameplay package
keeps the same handoff while normal gameplay services are active.

## What Runs

- `DiscoveryService::VisitCell()`
- `DiscoveryService::VisitForms()`
- `LocationChangeEvent`, `CellChangeEvent`, `GridCellChangeEvent`
- `ActorAddedEvent` and `ActorRemovedEvent`

In the default VR target these events have no character-spawn or ownership consumer. `DiscordService` can still observe location changes because it is already part of the connection-only service set. In `SkyrimTogetherVRGameplay.exe`, these events also feed the normal gameplay consumers.

## Observation-Only Differences

When built as `TP_SKYRIM_VR && TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE`, discovery skips the normal SkyrimTogether connection-error enforcement for:

- `bad_uGridsToLoad`
- `non_default_install`

Those checks are tied to flat Skyrim Together assumptions and would reject FUS-style VR modlists before runtime validation can begin. Reintroduce them only if there is a VR-specific compatibility gate for the selected modlist and server policy.

## Discovery Handoff File

The service writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.discovery
```

The file is updated when discovery state changes, and at least once per second while the service is ticking. Worldspace, cell, location, and actor reference IDs are read through the CommonLib-informed form accessors rather than raw shifted `TESForm::formID` fields. Current fields:

```ini
ready=1
actorCount=12
actorLimit=64
currentGrid=0,0
centerGrid=0,0
cachedWorldSpaceFormId=60
cachedInteriorCellFormId=0
playerCellFormId=62
playerWorldSpaceFormId=60
locationFormId=0
actor.0.formId=20
```

Only the first 64 discovered actor form IDs are written. This is intended as a launcher, external overlay, or future debug/proxy-avatar input surface, not as authoritative sync state.

The runtime handoff audit and runtime evidence checklist validate this file structurally for default, avatar-sync, and gameplay packages. The check requires the grid fields, cached worldspace/interior-cell ids, player cell/worldspace ids, location id, actor count, actor limit, and any `actor.N.formId` rows to parse consistently. Actor rows are validated by count, cap, contiguous index order, and positive form ids rather than by exact actor identity because loaded actors are volatile between runs.

## Still Disabled

- `CharacterService`
- actor assignment/spawn/ownership
- movement sync
- object sync
- inventory, magic, combat, weather, map, party, and actor-value services
- behavior-variable patch initialization
- flat D3D overlay/input hooks
