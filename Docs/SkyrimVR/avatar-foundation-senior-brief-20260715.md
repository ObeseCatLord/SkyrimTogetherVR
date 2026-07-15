# Canonical Avatar Foundation Senior Review Brief

Date: 2026-07-15

## Goal And Scale

Review the first mutation-capable Skyrim Together VR gameplay slice before it
is expanded into equipment, combat, magic, or physical interaction. This is a
solo-maintained compatibility port, so prefer a small defensible correction
over a new general-purpose entity framework. The target is still full Skyrim
Together gameplay parity; this review is limited to ensuring the P0 avatar
foundation will not encode an identity or actor-lifetime defect into every
later domain.

The reviewer is read-only. Verify the cited implementation before critiquing
the framing. Return prioritized findings and a recommended coherent patch
boundary; do not edit files.

## Environment Facts

| Fact | Evidence |
|---|---|
| Target runtime | Skyrim VR 1.4.15 with SKSEVR |
| Native boundary | alandtse CommonLibVR `ng` commit `d36bc08cae5d445804d4c22ccd49fec4e9dfbdc4` |
| Client/server base | Skyrim Together Reborn, original behavior preserved on branch `original-skyrim-together` |
| Current source | `main` at `e945b834`; mutation implementation was built from `dda9cad8` |
| Build state | Clean WinBoat/MSVC gameplay build passed 491 assertions in 26 cases; package/evidence audits had zero failures |
| Runtime state | No two-client avatar mutation evidence exists for this CommonLib build |
| Existing architecture decision | `Docs/SkyrimVR/full-gameplay-parity-senior-disposition-20260715.md` |
| EnTT version | xmake pins EnTT 3.10.0; its `entt::entity` uses 20 entity bits and 12 version bits |

## Verified Evidence

1. `[verified: source and EnTT 3.10 header]` Skyrim Together wire `ServerId`
   values are the integral representation of server `entt::entity` values.
   EnTT exposes `entt::to_entity`, `entt::to_version`, and `construct`; its
   32-bit entity mask is `0xFFFFF` with version shift 20.
2. `[verified: VRAvatarService.cpp:754-762]` The current bridge identity maps
   `EntityId = uint64_t(ServerId) + 1` and
   `EntityGeneration = (ServerId >> 20) + 1`.
3. `[verified: AvatarManager.cpp:272-298]` Adapter avatar keys contain session,
   lifecycle epoch, `EntityId`, and `EntityGeneration`. Its generation ledger
   is keyed by session, epoch, and `EntityId`, without generation.
4. `[verified: consequence of 1-3]` Reusing one EnTT entity slot changes the
   packed `ServerId`, so the current `EntityId` also changes. The ledger then
   cannot compare old and new generations for the same canonical slot; the
   separately transmitted generation is redundant rather than authoritative.
5. `[verified: CharacterSpawnRequest]` Spawn messages already carry packed
   `ServerId`, mapped `BaseId`, position/rotation, appearance, inventory,
   factions, actor values, player ID, and player/death/draw state. The first VR
   slice currently consumes only packed ID, player ID, cell, and root transform.
6. `[verified: VRAvatarService.cpp:229-285,578-607]` The mapped client tracks
   remote avatars by full packed `ServerId`, but submits the local player's
   `LocalActorBaseFormId` for every remote avatar instead of the spawn
   message's mapped `BaseId` or appearance.
7. `[verified: AvatarManager.cpp:102-173]` The CommonLib adapter validates the
   local cell/worldspace and NPC base, calls
   `PlayerCharacter::PlaceObjectAtMe(base, false)`, converts the result to an
   `ActorHandle`, applies position/angle/scale, and stores only that handle.
8. `[verified: AvatarManager.cpp:341-347]` Destruction resolves the handle and
   calls `Disable()` followed by `SetDelete(true)`.
9. `[verified: command/event sources]` Create/destroy are action-ordered;
   transforms are sequence-ordered. The mapped client currently auto-assigns
   global bridge action/sequence numbers and does not retain expected command
   IDs per avatar. Result events echo the command identity.
10. `[verified: lifecycle sources]` Disconnect submits and synchronously pumps
    `RetireEpoch` before clearing transport session identity. New game,
    pre-load, and detected cell changes retire adapter avatars on the command
    owner and advance the lifecycle epoch.
11. `[verified: build definitions]` Only avatar-sync/gameplay targets request
    remote-avatar network capabilities and compile actor targets on. Other
    gameplay mutation services remain disabled.
12. `[verified: original branch]` Original Skyrim Together builds remote
    actors through its flat-runtime `Actor::New`/appearance path, which is not
    valid for Skyrim VR and cannot be reused as-is.

## Open Decisions

### 1. Canonical entity split

Current lean: correct the mapped-client boundary to derive stable bridge
`EntityId = entt::to_entity(entt::entity{ServerId}) + 1` and
`EntityGeneration = entt::to_version(...) + 1`; reconstruct the packed server
ID from result events with `entt_traits<entt::entity>::construct`. Add unit
tests for slot reuse, version rollover representation, and round-trip.

Alternative considered: add explicit entity ID/generation fields to every wire
message now. Rejected for this slice because the packed server ID already
contains both pieces and changing every original protocol domain creates a
large compatibility migration before its necessity is proven.

### 2. Generation ordering and wrap

Current lean: keep the adapter ledger, but define comparison semantics using
the exact finite EnTT version domain rather than plain integer `<`/`>` if wrap
can occur in a process. At minimum reject contradictory live generations and
bind handles to exact equality.

Alternative considered: rely only on packed IDs and delete the generation
ledger. Rejected because delayed commands after slot reuse must be rejected at
the engine boundary even if mapped-client state is wrong.

### 3. Actor factory visual base

Current lean: retain CommonLib `PlaceObjectAtMe` as the only currently encoded
VR factory for the acceptance slice, but stop silently claiming remote visual
identity. Either map and validate the spawn `BaseId` for non-player NPCs, or
make the deliberate player-template clone explicit until an appearance
adapter exists. Do not consume appearance/inventory in this patch.

Alternative considered: port original `Actor::New` and appearance mutation
immediately. Rejected because it reintroduces the flat Actor/allocator/layout
dependencies that the CommonLib boundary was created to remove.

### 4. Command/result correlation and retry

Current lean: store pending create/destroy action IDs and last submitted root
sequence per avatar, and accept result events only when their identity matches
the pending operation. Add bounded create retry for transient inactive/missing
cell states only if the server spawn remains current; never retry malformed,
missing-form, or stale-session results.

Alternative considered: rely on FIFO event order. Rejected because lifecycle,
network, and bridge queues are independent and later domains will increase
interleaving.

### 5. Despawn and failed retirement

Current lean: keep `ActorHandle` resolution plus `Disable`/`SetDelete` for
temporary `PlaceObjectAtMe` refs, but require an explicit temporary-ref
invariant and diagnostics. A failed disconnect retirement must prevent a new
session from enabling avatar capability until cleanup succeeds or lifecycle
reset removes old handles.

Alternative considered: clear mapped-client maps and trust session keying to
make old actors harmless. Rejected because stale visible actors and leaked
temporary references are still gameplay failures.

## Suspected Overlap

- Decisions 1 and 2 may collapse into one canonical identity helper shared by
  service code and tests.
- Decisions 4 and 5 may collapse into one per-avatar state machine with
  explicit pending operation identity and retirement state.
- A recommendation to change bridge ABI version is in bounds only if the
  existing POD fields cannot express the corrected semantics.

## Requested Review Output

1. Verify the evidence and identify any incorrect premise.
2. Rank concrete correctness/crash/desync findings, with file and line evidence.
3. State whether the canonical entity split should be fixed without a wire or
   bridge ABI revision.
4. Specify the smallest coherent implementation that is safe to build once,
   including tests and static audit rails.
5. Identify any first-runtime-test blocker in the actor factory/despawn or
   lifecycle path that this brief missed.

## Not In Scope

- Equipment, inventory, actor values, combat, projectiles, magic, quests,
  animation, VRIK skeleton application, FBT, HIGGS, or PLANCK mutations.
- Flat D3D overlay/UI work.
- Server deployment, launcher controls, or Monado input automation.
- Re-reviewing the already adopted overall CommonLib boundary.
- Running Skyrim.
