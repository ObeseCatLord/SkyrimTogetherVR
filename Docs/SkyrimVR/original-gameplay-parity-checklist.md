# Skyrim Together VR Original Gameplay Parity Checklist

Updated: 2026-07-15

## Goal And Status Rules

The target is behavioral parity with the `original-skyrim-together` branch on
Skyrim VR 1.4.15, plus VRIK/FBT, HIGGS, and PLANCK behavior where VR needs
state that desktop Skyrim does not have.

- `[x] Source`: implemented and covered by no-launch source/tests.
- `[x] Build`: compiled in the current audited Windows gameplay package.
- `[x] Runtime`: proven in Skyrim VR with retained evidence.
- `[ ]`: still required. A telemetry file or relayed packet does not count as
  gameplay parity until the remote game state is visibly and semantically
  applied.

The canonical implementation boundary is fixed:

- mapped client: networking, replicated state, authority, interpolation;
- CommonLib gameplay bridge: native handles, events, validated game mutation;
- server: canonical entities, interest routing, ownership, deduplication;
- no `ActorExtension` append, flat-runtime `Actor::New`, or raw SE layout cast
  is permitted in the VR path.

## Phase 0: Avatar And Session Foundation

- [x] Source: Skyrim VR/SKSEVR/CommonLib/address inputs are locked.
- [x] Source: protocol revision, capability negotiation, server nonce,
  connection generation, lifecycle epoch, action ID, and sequence ID exist.
- [x] Source: fixed-width POD gameplay bridge with bounded command/event rings.
- [x] Source: CommonLib owner-thread command pump and process-lifetime sinks.
- [x] Source: CommonLib bridge distinguishes SkyrimVR.exe/address-library
  `1.4.15.0` from SKSEVR's plugin-interface `1.4.15.1`, validates SKSEVR
  2.0.12/release 60 before CommonLib initialization, and locks the distinction
  with unit/static audits.
- [x] Source: checked packed EnTT slot/version identity, including server ID 0.
- [x] Source: canonical remote humanoid create/root/spatial-transfer/despawn path
  using `CreateReferenceAtLocation`, retained adapter identity, and post-move
  cell/worldspace verification.
- [x] Source: exact result correlation, bounded create recovery, fail-closed
  retirement, visibility re-spawn, and interpolation convergence.
- [x] Build: canonical-avatar commit `94544550` compiled on WinBoat; Windows
  `TPTests` passed 526 assertions in 30 cases and the paired package/evidence
  audits passed. See `windows-gameplay-build-result-20260715-canonical-avatar.md`.
- [ ] Runtime: two clients prove unique spawn, move, turn, stop, leave, despawn.
- [ ] Runtime: repeat reconnect, load, cell transition, and entity reuse ten
  times with no duplicate/stale actor, leaked handle, or queue corruption.
- [ ] Runtime: prove `CreateReferenceAtLocation`, player-template clone, and
  `Disable`/`SetDelete` do not persist unsafe references in saves.
- [ ] Replace `player_template_fallback` with remote appearance/race/sex data.

No later mutation capability is release-enabled until the Phase 0 runtime gate
passes.

## Phase 1: Original Gameplay Semantics

### Movement And Animation

- [x] Source: local/remote movement and pose observation/relay diagnostics.
- [x] Source: authoritative root movement, heading, stop, and retained-identity
  cell/worldspace transfer with independent sequence ordering, stale-tick
  rejection, bounded pending spawns, destination-pinned acknowledgement timeout,
  and applied/faulted acknowledgements.
- [x] Source: complete named humanoid animation graph variable snapshots in both
  directions, with bounded five-chunk assembly, finite-value validation,
  supersession recovery, preimage rollback, and per-avatar quarantine.
- [x] Build: bridge ABI v2 commit `d201a3f8` compiled on WinBoat; Windows
  `TPTests` passed 571 assertions in 33 cases, the 503-file gameplay package
  passed its package audit, and the paired evidence archive passed with zero
  warnings or failures. See
  `windows-gameplay-build-result-20260715-movement-animation.md`.
- [ ] Port exact graph-event/action replay after `ActorMediator` targets and
  global pointer shapes are semantically and ABI verified for Skyrim VR.
- [ ] Port draw/sheath, idle, jump, sneak, sprint, swim, ragdoll, and furniture
  state without enabling unvalidated desktop hooks.
- [x] Source: validate the current actor/process/animation CommonLib API and
  owner-thread contract; runtime semantics remain gated below.
- [ ] Runtime: two clients prove locomotion and animation state without jitter,
  stuck movement, duplicate events, or animation feedback loops.
- [ ] Runtime: prove one retained-handle interior/exterior spatial transfer, one
  deliberate stale-tick rejection, zero graph/command ring drops, and zero
  spatial/animation rejection counters.

### Appearance, Equipment, And Inventory

- [x] Source: equipment observation and VR equipment relay diagnostics.
- [ ] Apply race, sex, weight, head parts, tint masks, face data, and player name.
- [ ] Apply equipped weapons, spells, shields, ammo, armor, powers, and draw state.
- [ ] Port inventory deltas, containers, trading, drops, pickups, and ownership.
- [ ] Define equip/inventory conflict and rollback behavior under latency.
- [ ] Keep HIGGS stash/consume/drop events deduplicated against vanilla inventory
  events.
- [ ] Runtime: equipment and inventory converge after reconnect, cell change,
  death, trading, container transfer, and save/load.

### Actor State, Death, And Respawn

- [ ] Apply health, magicka, stamina, maximum values, essential/dead state,
  level, experience, factions, and combat state.
- [ ] Port death, bleedout, revive, respawn, player summon, and ownership transfer.
- [ ] Port mount state and rider/mount lifecycle ordering.
- [ ] Validate actor-value and death APIs through CommonLib, not flat member data.
- [ ] Runtime: simultaneous damage/death/respawn converges without immortal,
  duplicated, or permanently disabled actors.

### Objects, Activation, And World References

- [x] Source: activation/grab observation and relay diagnostics.
- [ ] Port canonical object assignment, reference creation, and interest routing.
- [ ] Apply activation for doors, levers, harvestables, books, furniture, and
  containers with action-ID deduplication.
- [ ] Apply lock state, open state, object inventory, ownership claim/transfer,
  pickup, drop, and disabled/deleted state.
- [ ] Define authority for persistent references versus temporary dropped items.
- [ ] Runtime: object state remains identical after concurrent activation,
  disconnect, cell unload/reload, and save/load.

### Combat, Projectiles, And Magic

- [x] Source: combat-hit, projectile-intent, and magic-effect observation relays.
- [ ] Apply melee hit intent/result, block, stagger, critical, hostility, and
  damage exactly once.
- [ ] Create and reconcile arrows, bolts, missiles, beams, explosions, and impact
  state using validated VR projectile APIs.
- [ ] Port spell cast, interrupt, concentration, magic-effect application,
  shouts/powers, caster/target ownership, and hostile/healing semantics.
- [ ] Define server authority and prediction so vanilla, PLANCK, and relayed hit
  events cannot apply damage twice.
- [ ] Runtime: two clients prove melee, bows, spells, shouts, death, and respawn
  under latency with no duplicate projectile/effect/damage.

### Quests, Dialogue, Party, And Social State

- [ ] Port quest stage/log synchronization and scripted quest update policy.
- [ ] Port dialogue requests, subtitles, player dialogue, and package changes.
- [ ] Runtime-validate party create/invite/join/leave/kick/leader behavior in VR.
- [ ] Runtime-validate player list, chat, waypoints, teleport/admin commands, and
  remote player join/leave/cell notifications.
- [ ] Restore voice/Vivox behavior or document a replacement with equivalent
  user-visible functionality.

### World State And Persistence

- [ ] Apply server time, timescale, calendar, weather, and current-weather sync.
- [ ] Port exterior grid/interior cell transitions and worldspace interest rules.
- [ ] Implement connected save/load recovery, resync, reconnect, and stale actor
  cleanup without serializing adapter handles.
- [ ] Audit Papyrus/script patches and native bindings used by original gameplay.
- [ ] Runtime: host/client world state converges across fast travel, interiors,
  long sessions, save/load, server restart, and client reconnect.

## Phase 2: VR Embodiment And Physics

### VRIK And Full Body Tracking

- [x] Source: VRIK detection/interface and local/remote pose relay diagnostics.
- [x] Source: Skyrim VR FBT protocol/source compatibility has been catalogued.
- [ ] Apply HMD, hands, weapon offsets, pelvis, feet, spine, and tracker pose to
  the canonical remote actor behind an explicit update-order barrier.
- [ ] Blend/fallback cleanly for players without VRIK or FBT.
- [ ] Network calibration/body-scale changes and seated/standing transitions.
- [ ] Runtime: mixed VRIK/FBT/non-FBT clients remain stable and visually correct.

### HIGGS

- [x] Source: HIGGS API detection and observation-only relay diagnostics.
- [ ] Define authoritative grab/pull/drop/stash/consume/two-hand object ownership.
- [ ] Apply remote HIGGS interactions without invoking unsupported local-player
  APIs or fighting Skyrim Together object ownership.
- [ ] Deduplicate HIGGS callbacks against vanilla activation/inventory/projectile
  events.
- [ ] Runtime: two clients contend for the same object safely under latency.

### PLANCK

- [x] Source: PLANCK detection/interface and observation-only hit diagnostics.
- [ ] Validate nontrivial PLANCK hit-data ABI against the installed version.
- [ ] Map physical hits, grabs, ragdolls, and damage into canonical combat action
  IDs and authority rules.
- [ ] Prevent remote pose application from fighting PLANCK ragdoll/physics state.
- [ ] Runtime: PLANCK and non-PLANCK clients exchange hits without duplicate
  damage, unstable ragdolls, or crashes.

## Phase 3: Product And Release Parity

- [x] Source: VR-only launcher targets, package/evidence audits, and WinBoat build
  automation exist.
- [x] Runtime baseline: a client has previously authenticated to the dedicated
  server; every new mutation revision still needs fresh evidence.
- [ ] Provide original-equivalent connect/disconnect/player-list/chat controls in
  VR without relying on the flat D3D11 overlay.
- [ ] Validate keyboard, Index controller, and common community binding profiles.
- [ ] Validate HIGGS, PLANCK, VRIK, FBT, Realm of Lorkhan, DevBench, and the
  selected FUS native-DLL set against the final gameplay package.
- [ ] Validate Windows native launch and Linux Proton/UMU plus Monado launch.
- [ ] Validate server compatibility, password/mod checks, Docker deployment,
  restart, logs, and one-server-only operation.
- [ ] Restore installer/update/release behavior and publish reproducible audited
  prerelease packages with rollback instructions.

## Per-Domain Definition Of Done

Every unchecked mutation domain must include all of the following before it is
marked complete:

1. original-branch behavior trace and protocol inventory;
2. CommonLib VR read/mutation API with no unsafe flat layout dependency;
3. server authority, interest, ordering, and deduplication contract;
4. session/epoch/entity/action or sequence identity validation;
5. queue overflow, timeout, reconnect, load, and stale-handle behavior;
6. VRIK/HIGGS/PLANCK interaction policy where relevant;
7. unit/static tests and a clean audited Windows gameplay build;
8. two-client semantic runtime evidence on the dedicated server;
9. documentation and capability promotion only after evidence passes.

## Immediate Next Actions

1. Build, audit, and deploy the CommonLib bridge runtime-gate correction that
   supersedes `d201a3f8`; prove the bridge remains loaded and reaches Ready.
2. Prove one automated client reaches Realm of Lorkhan and receives a nonzero
   server player ID before establishing the isolated second client.
3. Run the Phase 0 plus movement/graph two-client acceptance matrix against
   that exact client/server source revision.
4. Fix any actor lifecycle, spatial-transfer, or graph-application defect found
   in retained runtime evidence before adding another mutation domain.
5. Semantically verify and implement exact animation/action replay, then proceed
   to appearance/equipment/inventory in the dependency order above.
