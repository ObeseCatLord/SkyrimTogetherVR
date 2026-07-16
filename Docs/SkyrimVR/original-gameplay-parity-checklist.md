# Skyrim Together VR Gameplay Parity Checklist

Updated: 2026-07-16

## Status Rules

The target is original `original-skyrim-together` gameplay on Skyrim VR
1.4.15, plus VR embodiment and compatibility with VRIK/FBT, HIGGS, and PLANCK.

- `[x] Source` means the native producer, original protocol translation,
  server path, receiver, validation, and lifecycle behavior are written.
- `[x] Build` means that exact source revision compiled and passed its static,
  unit, package, and evidence audits on Windows.
- `[x] Runtime` means two Skyrim VR clients visibly proved the behavior against
  the matching dedicated server revision.
- A diagnostic relay is not gameplay parity. It may supplement, but cannot
  replace, the original canonical message that owns mutation.

The current source tranche has not been built or runtime-tested. Build and
runtime boxes below therefore remain open even when source is complete.

## Native Boundary

- [x] Source: maintained alandtse `CommonLibVR` `ng` v4.37.0 is pinned by
  repository, tag, commit, runtime, SKSEVR, executable hash, and Address Library
  hash.
- [x] Source: the mapped client owns networking and canonical entities; the
  CommonLib SKSEVR plugin exclusively owns game pointers, retained handles,
  events, and engine mutation; the server owns authority and interest routing.
- [x] Source: gameplay implemented natively by original Skyrim Together remains
  native in the mapped client or CommonLib SKSEVR plugin. Papyrus is limited to
  thin Skyrim UI, quest, and compatibility adapters and never owns replicated
  state, retries, identity, ordering, authority, or engine-hook behavior.
- [x] Source: no VR mutation depends on appended desktop `ActorExtension`
  storage, flat desktop actor construction, or Papyrus-maintained state.
- [x] Source: the fixed bridge ABI carries nonce, connection generation,
  lifecycle epoch, entity generation, action ID, and sequence ID.
- [x] Source: exact-match gameplay protocol revision 7 gates the token-bound NPC
  ownership and final-equipment wire layouts before either endpoint decodes them.
- [x] Source: generated CommonLib aliases and curated VR overrides are
  collision-aware and fail closed on unverified addresses or prologues.
- [ ] Build: verify the current ABI/capability revision and curated address
  overlay in the final gameplay package.
- [ ] Runtime: prove clean attach, owner-thread pumping, disconnect, reconnect,
  load, new game, and shutdown without stale commands or leaked references.

## Original Gameplay

### Actors, Movement, And Animation

- [x] Source: local assignment, remote create/despawn, visibility recovery,
  retained-handle identity, movement interpolation, stop, heading, and
  interior/exterior cell and worldspace transfer.
- [x] Source: named humanoid graph snapshots use bounded assembly, ordering,
  rollback, stale rejection, and quarantine.
- [ ] Source: exact `ActorMediator::PerformAction` capture/replay is blocked for
  the final build. The current prototype lacks `GameId` form translation,
  generation-bound capability renegotiation, and verified `TESActionData`
  constructor/callee/destructor ownership on VR.
- [x] Source: draw/sheath and the supported idle, jump, sneak, sprint,
  ragdoll, furniture, and mount animation events replay through CommonLib.
- [x] Source: package changes are captured for both owned NPCs and the local
  player; local-player package transport retries until accepted.
- [x] Source: incoming scripted object animations replay through the Skyrim VM.
  Upstream's outbound producer is compiled out by `OBJECT_ANIM_SYNC=0`; its
  desktop Address Library IDs are registration code in VR, so no guessed hook
  is installed.
- [ ] Build: compile and audit exact-action, movement, graph, and package paths.
- [ ] Runtime: two clients prove spawn, move, stop, turn, cell transfer,
  animation, package, leave, and reconnect without jitter, echo, or duplicates.

### Appearance, Equipment, And Inventory

- [x] Source: race, sex, weight, dynamic name, head parts, body skin tint,
  level, and essential state replicate and replay on dynamic remote actor bases.
- [x] Source: bounded full worn-equipment snapshots include armor, weapons,
  shields, ammo, left/right spells, and shout or power state.
- [x] Source: each VR equipment change is one protocol-revision-7 final-state
  transaction. The server validates the complete worn inventory and magic
  selection before one authoritative mutation/notify, and the receiver reserves
  begin/items/end as one bridge-ring batch before staged CommonLib application.
- [x] Source: inventory pickup, removal, drop, reconciliation, object inventory,
  ownership, and quest-item handling use original requests.
- [x] Source: incoming equipment, inventory, and appearance replay uses typed
  CommonLib APIs and session-scoped baselines.
- [ ] Source: full face tint masks, face morphs, texture overlays, and hair
  color. CommonLib exposes local-player tint data but no verified remote actor
  face-generation transaction that can safely apply these fields.
- [ ] Mixed-client source: desktop clients do not yet receive a synthesized
  legacy equipment delta stream for a VR-owned final-state transaction. That
  fanout is deliberately fail-closed to avoid partial multi-message mutation;
  VR clients still consume desktop transaction-zero equipment notifications.
- [ ] Build: compile and audit appearance/equipment/inventory translation.
- [ ] Runtime: prove convergence after equip, pickup/drop, container transfer,
  reconnect, cell change, death, and save/load.

### Actor State, Death, And Respawn

- [x] Source: health, magicka, stamina, maximum values, health deltas, level,
  essential/dead state, combat skill experience, factions, and draw state.
- [x] Source: owned NPC state, inventory, faction, package, movement, death,
  and ownership snapshots are bounded and transactionally published.
- [x] Source: local death and respawn preserve original gold-loss chunks,
  bounty clearing, spell/shout restoration, resurrection, cell centering,
  delayed knockdown, and temporary god-mode protection.
- [x] Source: local resurrection happens once; the server respawn request
  retries independently, and all pending state is lifecycle/session scoped.
- [x] Source: mount ownership and mount requests preserve actor ordering; zero
  mount is retained only as local cancellation because the original protocol
  has no dismount request.
- [ ] Source: native black fade around respawn. Candidate VR Address Library
  rows do not identify a verified five-argument callable equivalent.
- [ ] Build: compile and audit actor-state/death/respawn paths.
- [ ] Runtime: prove damage, death, gold loss, respawn, simultaneous deaths,
  mount, disconnect during respawn, and reconnect convergence.

### Objects And World References

- [x] Source: cell object discovery and bounded assignment for doors and
  containers, including player-home filtering and complete inventory snapshots.
- [x] Source: activation, lock/open state, object inventory, ownership,
  teleport, and temporary reference handling use original protocol messages.
- [x] Source: object, NPC, equipment, graph, and text producers
  reserve their complete ring transaction before publishing; queue pressure
  cannot leave an unrecoverable partial commit.
- [x] Source: transient incoming text and commands use bounded retry queues with
  nonce, generation, and epoch validation.
- [ ] Build: compile and audit object assignment and transaction publication.
- [ ] Runtime: prove concurrent activation, lock, container, cell reload, and
  disconnect recovery with zero destructive partial snapshots.

### Combat, Projectiles, And Magic

- [x] Source: health change remains the canonical damage channel; raw VR and
  PLANCK hit observations never apply a second damage mutation.
- [x] Source: combat target start/stop, actor-value updates, and PvP policy are
  validated and replayed through retained remote actor identity.
- [x] Source: complete projectile launch data is captured from the native launch
  hook, translated to the original request, and recreated through CommonLib.
- [x] Source: spell cast, interrupt, target effect, add/remove spell, source,
  target, hostility, dual-cast, and ownership paths use original messages.
- [x] Source: local `Actor::RemoveSpell` capture is pinned to exact VR RVA and
  prologue and suppresses authoritative remote replay echo.
- [x] Source: HIGGS/PLANCK diagnostics are deduplicated from canonical
  inventory, actor-value, projectile, and magic mutations.
- [ ] Build: compile and audit combat/projectile/magic hooks and address pins.
- [ ] Runtime: prove melee, bow, spell, concentration, shout, healing, hostile
  effect, death, and respawn under latency without duplicate damage/effects.

### Quests, Dialogue, Party, And World State

- [x] Source: quest start/stop/stage policy, suppression, party gating, and
  original quest requests.
- [x] Source: dialogue voice, subtitle metadata/text, player dialogue, chat,
  packages, waypoints, teleport/admin responses, and bounded retries.
- [x] Source: server calendar, time, timescale, weather, difficulty, greetings,
  and world-encounter settings apply and restore on lifecycle reset.
- [x] Source: connect/disconnect, party state, player list, command-file control,
  and VR companion controls do not require the desktop D3D overlay.
- [ ] Source/product: restore Vivox voice chat or define a supported equivalent.
- [ ] Build: compile and audit quest/dialogue/party/world-state paths.
- [ ] Runtime: prove quest, dialogue, chat, party, waypoint, teleport, time,
  weather, server restart, save/load, and reconnect behavior.

## VR Embodiment And Mod Compatibility

### VRIK And FBT

- [x] Source: HMD, hands, pelvis, thighs, calves, and feet are validated,
  sequenced, relayed, and applied to the canonical remote actor skeleton.
- [x] Source: world/local-space conversion, orthonormal basis checks, root
  generation, bounded pending state, and ragdoll write suppression are present.
- [x] Source: mixed clients without FBT fall back to available head/hand/body
  nodes without changing actor identity.
- [ ] Source: remote VRIK finger curls and VRIK calibration. The public VRIK
  interface has no remote-actor application API; samples remain explicitly
  unsupported instead of being sent to the local-player API.
- [ ] Runtime: mixed VRIK, SkyrimVR-FBT, and non-FBT clients prove stable pose,
  tracker loss/recovery, seated/standing transitions, and save/load.

### HIGGS

- [x] Source: HIGGS grab, pull, and drop use canonical object identity and
  keyframed/dynamic motion transitions; stash/consume durable mutation remains
  owned by canonical inventory deltas.
- [x] Source: callback ordering and independent action ledgers prevent vanilla
  inventory/activation events from double-applying HIGGS actions.
- [ ] Runtime: two clients contend for, pull, drop, stash, and consume the same
  objects under latency without duplication or stuck motion state.

### PLANCK

- [x] Source: PLANCK-compatible hit classification feeds diagnostics while
  canonical health/effect messages own damage.
- [x] Source: network skeleton writes stop while a remote actor is in ragdoll,
  preventing pose replication from fighting PLANCK physics.
- [ ] Source: direct remote physical grab/ragdoll replay. No stable public
  remote-actor PLANCK API is available, and invoking local-player physics entry
  points for a remote actor is unsafe.
- [ ] Runtime: PLANCK and non-PLANCK clients prove hit/damage deduplication,
  ragdoll stability, recovery, and compatibility with HIGGS/VRIK.

## Robustness And Delivery

- [x] Source: multi-record command/event batches reserve contiguous ring ranges;
  commit records are last and producers cannot interleave transactions.
- [x] Source: spawn results, text retries, respawn, package changes, equipment
  baselines, suppression windows, and remote ledgers are bounded and
  lifecycle-scoped.
- [x] Source: client messages enter a bounded owner-thread queue whose packets
  are tagged with the connection attempt and authenticated generation; stale
  packets are dropped and queue acceptance is explicit to retrying producers.
- [x] Source: transport latches its owner thread and rejects off-thread sends;
  mapped stateful and inventory producers retain one global FIFO order across
  their bounded queues, move coalesced last-value state to the newest order,
  and force a lifecycle rebase instead of silently dropping a full retry queue.
- [x] Source: native local capture is explicitly armed only after authenticated
  canonical local entity assignment. Per-field baselines advance only after
  event-ring acceptance, including health/experience deltas and complete
  equipment transactions.
- [x] Source: remote final equipment tracks CommonLib result action IDs,
  commits transaction replay state only after a successful end result, and
  retries failed or acknowledgement-timed-out application a bounded number of
  times. Spawn-state result tracking uses the same bounded timeout/resync rule.
- [x] Source: server equipment replay ledgers clean up on player/entity removal
  and fail closed at capacity; cached VR appearance replay applies normal
  cell/range interest filtering after every committed grid/interior/exterior
  transition and replays both sides of newly established interest.
- [x] Source: NPC ownership transfer uses an expiring, session-bound, single-use
  server grant. VR waits for a complete mapped snapshot and explicit completion
  acknowledgement before promoting local ownership; desktop clients echo the
  same token through their native ownership path.
- [x] Source: every static native staging owner, including final equipment,
  clears state on explicit epoch retirement and native lifecycle transition.
- [x] Source: WinBoat build automation imports the exact package/evidence pair,
  validates revision and hashes, refreshes the local-agent handoff ZIP, and
  checks that ZIP after every successful build.
- [x] Source: host and WinBoat cleanup locks prevent cleanup during a build;
  scheduled disk-pressure cleanup removes only reproducible project output and
  bounded caches while preserving source, games, current handoffs, and evidence.
- [x] Review: the final Sol max architecture, ABI, concurrency, lifecycle,
  protocol, and crash-surface review is dispositioned in
  `full-gameplay-source-postfix-senior-disposition-20260716.md`.
- [ ] Build: commit and push a clean source revision, then run one final WinBoat
  gameplay build and all unit/static/package/evidence audits.
- [ ] Deploy: install that exact package locally and deploy the exact matching
  server revision, with one and only one server container.
- [ ] Runtime: complete the two-client domain matrix on Windows and Linux
  Proton/UMU with Monado, including Index bindings and controller navigation.
- [ ] Compatibility: validate HIGGS, PLANCK, VRIK, SkyrimVR-FBT, Realm of
  Lorkhan, DevBench, and the selected FUS native DLL set.
- [ ] Release: publish the audited package as a prerelease with source revision,
  dependency lock, server instructions, runtime checklist, known limitations,
  and rollback procedure.

## Next Stage Order

1. Finish integration review of the accepted Sol findings without compiling or
   launching.
2. Run source/static checks, regenerate address artifacts, commit, and push the
   reviewed revision.
3. Run the single WinBoat gameplay build; let the build wrapper refresh and
   validate the handoff ZIP automatically.
4. Deploy the exact client/server pair and execute the runtime matrix above.
