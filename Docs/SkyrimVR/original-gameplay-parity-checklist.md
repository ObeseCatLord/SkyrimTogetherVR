# Skyrim Together VR Gameplay Parity Checklist

Updated: 2026-08-19

## Status Rules

The target is original `original-skyrim-together` gameplay on Skyrim VR
1.4.15, plus VR embodiment and compatibility with VRIK/FBT, HIGGS, and PLANCK.

- `[x] Source` means the native producer, original protocol translation,
  server path, receiver, validation, and lifecycle behavior are written.
- `[x] Build` means that exact source revision compiled and passed its static,
  unit, package, and evidence audits on Windows.
- `[x] Runtime` means two Skyrim VR clients visibly proved the behavior against
  the matching dedicated server revision.
- `[~] Source pending` means a repair is designed or partially implemented but
  not yet integrated; build and runtime are necessarily pending.
- Source completion is implementation evidence only. It never transfers a
  prior revision's build checkbox, proves the current unbuilt worktree, or
  supplies one-client or paired runtime credit.
- A diagnostic relay is not gameplay parity. It may supplement, but cannot
  replace, the original canonical message that owns mutation.

**Current release classification:** connection/bootstrap alpha only. Until the
matching two-client gameplay matrix supplies runtime evidence, no source row,
historical build, single-client connection result, loader/readiness audit, or
diagnostic relay can promote this release to gameplay-ready status.

Commit `b8305b3b` passed the candidate and exact-HEAD WinBoat gameplay builds,
4,218 assertions across 76 tests, source readiness, package, and build-evidence
audits. Its matching ARM64 server and handoff-only Linux/Monado client then
passed the trusted one-client gameplay-bootstrap gate. Build checkboxes below
apply to that exact binary revision; two-client runtime checkboxes remain open.

`Docs/SkyrimVR/code-review-remediation-20260815.md` is the current authority
for the review disposition and gates. `parity-safety-stage-20260802.md` remains
dated historical context, not current build or runtime proof.

Current runtime record: `b8305b3b` authenticated on protocol revision 15 with
all mandatory native parity capabilities negotiated, lifecycle and rehydration
ready, and no bridge ring drops or rejected commands. This does not check off
the two-client gameplay runtime items below. See
`runtime-connection-result-20260818-b8305b3b.md`.

## 2026-08-19 Exact-Equivalence Source Update

- [x] Source: activation, calendar, equipment authority, summon authority,
  remote temporary-save exclusion, local drop capture, and weather native
  access now have pinned Skyrim VR 1.4.15.0 contracts: direct RVA (or direct
  Calendar ID 35402), executable fingerprint, text/page span, and entry
  prologue. These are not new CommonLib aliases; `VersionDb` removes the known
  false VR IDs for summon, flee, dialogue response, drop, and detection.
- [ ] Runtime: static source equivalence does not prove final-equipment queued
  recovery, drop-event synchronicity, weather/save lifecycle targets, or
  remote AI detection/flee behavior. Two current-revision clients must prove
  those paths before this release changes classification.
- [ ] Optional extension: direct remote PLANCK physical grab/ragdoll replay is
  outside desktop parity pending a stable public API or separate native design.
  PLANCK damage deduplication and pose/physics non-interference remain runtime
  gates.

## Native Boundary

- [x] Source: the current CommonLib source merges alandtse `CommonLibVR`
  `ng` 6.3.4 at upstream `7bdcb9efe` into project branch commit `586eac1f4`.
  The last compiled candidate used 6.3.1 at `108836139`; the
  former 6.1.1 / `612394bda3e2674da585831702308d571cf991b6` pin is historical.
  The dependency remains pinned by repository, commit, runtime, SKSEVR,
  executable hash, and Address Library hash.
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
- [x] Source: mapping ABI is 23. ABI 20 introduced
  `CommandStatus::Degraded`; later assignment/bootstrap records raised it again,
  so readers reject every incompatible fixed bridge layout.
- [x] Source: exact-match gameplay protocol revision 17 separates VR process
  identity, full gameplay-client intent, and mandatory native-parity admission.
  It additionally identity-authorizes attacker-originated NPC health deltas
  against an owned player entity and rejects replayed action nonces while retaining token-bound
  NPC ownership, final-equipment, version-3
  full-body/finger-pose wire layouts, and
  bounded ordered HIGGS mutation batches
  before either endpoint decodes them. Body formats 1 and 2 remain decodable
  inside a revision-17 endpoint for fixture and persisted-data compatibility.
- [x] Source: bridge capability revision 34 requires aggregate local capture
  event-sink registration and native quest mutation before a gameplay client
  can authenticate. Optional direct relays cannot masquerade as canonical
  gameplay readiness.
- [x] Source: HIGGS authority is transactional and post-fanout, receiver replay
  tails publish atomically, conflict/unresolvable skips are bounded, reconnect
  rebases retained events, and direct VRGrab does not throttle discrete edges.
- [x] Source: HIGGS callbacks are the sole transient physical-mutation producer;
  ambiguous Bethesda grab events cannot duplicate the same gesture, while
  stash/consume remain canonical inventory mutations.
- [x] Source: generated CommonLib aliases and curated VR overrides are
  collision-aware and fail closed on unverified addresses or prologues.
- [x] Build: `b8305b3b` compiled and audited mapping ABI 22, capability
  behavior, and the curated address overlay in candidate and clean packages.
- [ ] Runtime: prove clean attach, owner-thread pumping, disconnect, reconnect,
  load, new game, and shutdown without stale commands or leaked references.

## Original Gameplay

### Actors, Movement, And Animation

- [x] Source: local assignment, remote create/despawn, visibility recovery,
  retained-handle identity, movement interpolation, stop, heading, and
  interior/exterior cell and worldspace transfer.
- [x] Source: named humanoid graph snapshots use bounded assembly, ordering,
  rollback, stale rejection, and quarantine.
- [x] Source: exact `ActorMediator` action capture and replay use verified Skyrim
  VR 1.4.15 `PerformAction`, complex-action, animation-variable, constructor,
  singleton, context, and vtable targets. Capability publication and every raw
  call fail closed unless the complete RVA, prologue, call-sequence, layout, and
  live-object contract validates. Generic animation graph/event fallback remains
  available when exact actions are unavailable.
- [x] Source: draw/sheath and the supported idle, jump, sneak, sprint,
  ragdoll, furniture, and mount animation events replay through CommonLib.
- [x] Source: package changes are captured for both owned NPCs and the local
  player; local-player package transport retries until accepted.
- [x] Source: generated desktop-to-VR actor-AI aliases for desktop IDs 37577,
  39643, and 42704 are rejected because exact Skyrim VR 1.4.15 disassembly
  proves that their correlated RVAs are unrelated functions. Remote avatar
  admission requires observed AI disable; periodic authority reconciliation is
  defense in depth rather than the first ownership boundary.
- [x] Source: incoming scripted object animations replay through the Skyrim VM.
  Upstream's outbound producer is compiled out by `OBJECT_ANIM_SYNC=0`; its
  desktop Address Library IDs are registration code in VR, so no guessed hook
  is installed.
- [x] Build: `b8305b3b` compiled and audited the fail-closed exact-action
  boundary plus the movement, graph, and package paths.
- [ ] Runtime: two clients prove spawn, move, stop, turn, cell transfer,
  animation, package, leave, and reconnect without jitter, echo, or duplicates.

### Appearance, Equipment, And Inventory

- [x] Source: a tint-bearing appearance transaction validates and applies race,
  sex, weight, dynamic name, head parts, morphs, hair, face texture, body skin
  tone, and full FaceGen tint-mask composition on dynamic remote actor bases.
  The compositor is pinned to official SKSEVR 2.0.12 Skyrim VR 1.4.15 targets
  and fails closed before any raw call if its ABI or executable evidence differs.
- [x] Source: a complete appearance transaction is marked applied only after 3D
  rebuild and tint composition succeed. Explicit transient commit failures
  retry the complete idempotent snapshot with a fresh bridge transaction
  sequence and bounded backoff; permanent failures and ambiguous timeouts remain
  terminal because engine-owned 3D/material state cannot be rolled back safely.
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
- [x] Source: remote FaceGen texture-mask composition uses the verified VR
  renderer, `TESTexture`, render-target, tint factory, and mask-application ABI;
  no translated desktop Address Library row is trusted for this path.
- [x] Source: the first mixed VR final-equipment legacy baseline is seeded
  before owner inventory mutation. It is session/generation-bound and gated by
  the negotiated VR capability so legacy delta fanout starts from the real
  pre-mutation state. Pure diff tests are written but unrun.
- [x] Build: `b8305b3b` compiled and audited the degraded appearance and
  mixed-client equipment paths, including the pure diff tests.
- [ ] Runtime: prove convergence after equip, pickup/drop, container transfer,
  reconnect, cell change, death, and save/load.

### Actor State, Death, And Respawn

- [x] Source: health, magicka, stamina, maximum values, health deltas, level,
  essential/dead state, combat skill experience, factions, and draw state.
- [x] Source: owned NPC state, inventory, faction, package, movement, death,
  and ownership snapshots are bounded and transactionally published.
- [x] Source: local death and respawn preserve original gold-loss chunks,
  bounty clearing, selected left/right spells and shout/power restoration,
  exterior center-grid or interior parent-cell placement, native move,
  delayed knockdown, and temporary god-mode protection in desktop order. The
  `DispelAllSpells` (ID 34512, RVA `0x0557070`) and `GetCOCPlacementInfo`
  (ID 19075, RVA `0x027A4C0`) gates are exact Skyrim VR 1.4.15 decrypted
  targets; the generated `0x0579DF0` and `0x0294070` mappings are overridden.
- [x] Source: local resurrection happens once; the server respawn request
  retries independently, and all pending state is lifecycle/session scoped.
- [x] Source: mount ownership and mount requests preserve actor ordering; zero
  mount is retained only as local cancellation because the original protocol
  has no dismount request.
- [x] Source: respawn fade uses the verified Skyrim VR 1.4.15 native
  `FadeOutGame` target and prologue, failing closed when unavailable. Death and
  respawn also capture and restore essential/no-bleedout state around the
  canonical resurrection flow.
- [x] Build: `b8305b3b` compiled and audited actor-state/death/respawn paths.
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
- [x] Build: `b8305b3b` compiled and audited object assignment and transaction
  publication.
- [ ] Runtime: prove concurrent activation, lock, container, cell reload, and
  disconnect recovery with zero destructive partial snapshots.

### Combat, Projectiles, And Magic

- [x] Source: health change remains the canonical damage channel; raw VR and
  PLANCK hit observations never apply a second damage mutation.
- [x] Source: actor-value updates and the supported PvP policy are validated
  through retained remote actor identity. `SetCombatTarget` is deliberately
  unsupported and fail-closed; it must not be counted as complete or replayed.
- [x] Source: complete projectile launch data is captured from the native launch
  hook, translated to the original request, and recreated through CommonLib.
- [x] Source: spell cast, interrupt, target effect, add/remove spell, source,
  target, hostility, dual-cast, and ownership paths use original messages.
- [x] Source: local `Actor::RemoveSpell` capture is pinned to exact VR RVA and
  prologue and suppresses authoritative remote replay echo.
- [x] Source: HIGGS/PLANCK diagnostics are deduplicated from canonical
  inventory, actor-value, projectile, and magic mutations.
- [x] Build: `b8305b3b` compiled and audited combat/projectile/magic hooks and
  address pins.
- [ ] Runtime: prove melee, bow, spell, concentration, shout, healing, hostile
  effect, death, and respawn under latency without duplicate damage/effects.

### Quests, Dialogue, Party, And World State

- [x] Source: quest start, stage, and terminal stopped transitions use the
  synchronous Skyrim VR native stage function mapped by alandtse from desktop
  ID 24482/25004 to VR RVA `0x03803D0`. Exact bounded start/stage suppression
  tokens prevent echo, desktop mutation ordering is preserved, and success is
  reported only after native postconditions hold.
- [x] Source: dialogue and party/waypoint handling remain separately available
  in the bridge; disabling `QuestMutation` does not disable those domains.
- [x] Source: dialogue voice, subtitle metadata/text, player dialogue, chat,
  packages, waypoints, teleport/admin responses, and bounded retries.
- [x] Source: verified speech and subtitle hooks suppress engine-owned output
  for managed remote speakers outside explicit network replay. No guessed
  `ProcessResponse` hook is installed; candidate RVA `0x681F80` remains blocked
  until its ABI, desktop correspondence, and callers are proven.
- [x] Source: server calendar, time, timescale, weather, difficulty, greetings,
  and world-encounter settings apply and restore on lifecycle reset.
- [x] Source: VR command-file producers for `set_time`,
  `teleport_to_player`, and `admin_teleport` validate their inputs and require
  a stable authenticated transport before sending the original requests. They
  compiled in the `b8305b3b` gameplay package.
- [x] Source: connect/disconnect, party state, player list, command-file control,
  and VR companion controls do not require the desktop D3D overlay.
- [x] Source: literal tracked-branch voice parity is preserved. Both current and
  `original-skyrim-together` builds default to `TP_VIVOX=0`; the original branch
  contains only an optional `Services/Vivox` build hook while the proprietary,
  gitignored SDK/source is absent. Shipping voice remains a separate product and
  licensing task, not missing behavior from the reviewable desktop source branch.
- [x] Build: `b8305b3b` compiled and audited the fail-closed quest lane and the
  dialogue/party/world-state paths.
- [ ] Runtime: prove quest, dialogue, chat, party, waypoint, teleport, time,
  weather, server restart, save/load, reconnect, remote native-speech
  suppression, and local speech preservation.

## VR Embodiment And Mod Compatibility

### VRIK And FBT

- [x] Source: HMD, hands, pelvis, spine0/1/2, neck, clavicles, upper arms,
  forearms, thighs, calves, and feet are validated, sequenced, relayed, and
  applied to the canonical remote actor skeleton.
- [x] Source: world/local-space conversion, orthonormal basis checks, root
  generation, bounded pending state, and ragdoll write suppression are present.
- [x] Source: mixed clients without FBT fall back to available head/hand/body
  nodes without changing actor identity.
- [x] Source: body format 3 captures the final post-VRIK/post-HIGGS hierarchy,
  including SkyrimVR-FBT world-only spine corrections converted back to
  parent-local rotations. Remote application writes the body parent-first,
  updates the hierarchy, then derives HMD/hand endpoints against the updated
  parent worlds so upstream body changes do not displace controller targets.
- [x] Source: post-VRIK/post-HIGGS local rotations for the 30 named finger-chain
  nodes use a sparse, versioned quaternion payload and an atomic chunk/commit
  bridge transaction. The remote bridge re-resolves the current actor root,
  validates sequence/root generation and matrices, and suppresses writes during
  ragdoll. VRIK camera/calibration diagnostics remain observation-only because
  its public interface exposes local-player state but no remote-actor solver API.
- [x] Source: format-2/3 joint data must match its enclosing body capture sequence
  and root generation. Visual frames keep at most one admitted and one
  replaceable pre-admission batch per remote player; 256 of 512 result-owner
  slots and 64 of 192 pending-work slots remain reserved for state-changing
  gameplay. A maximum format-3 frame owns 112 results, and admitted pose work is
  never replayed.
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
- [x] Source: PLANCK interface revision 1 acquisition occurs only at SKSE
  PostPostLoad, as required by its public API, without polling transient hit
  pointers. Diagnostic handoff refreshes ride the existing VRIK/HIGGS
  game-thread callback; no unload-unsafe writer thread exists. Canonical engine
  hit events remain the sole replicated damage authority.
- [ ] External API: direct remote physical grab/ragdoll replay. No stable public
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
- [x] Source: the WinBoat candidate-build helper creates a disposable proof
  revision before any persistent commit. It does not update a handoff artifact.
  The subsequent clean committed build is authorized to generate and audit the
  private Linux/Windows handoff archive.
- [x] Source: host and WinBoat cleanup locks prevent cleanup during a build;
  scheduled disk-pressure cleanup removes only reproducible project output and
  bounded caches while preserving source, games, current handoffs, and evidence.
- [x] Source: startup CRT ownership, startup readiness, PE-loader hardening,
  `Movement` `std::bit_cast`, projectile regression, sender-derived server
  authorization, bounded two-client diagnostics, and cross-platform handoff
  installers are integrated with focused tests. Candidate and clean build,
  readiness and package/evidence audits pass at `b8305b3b`; handoff finalization
  and multi-client runtime status remain pending.
- [x] Review: the Sol max/xhigh architecture, ABI, concurrency, lifecycle,
  protocol, crash-surface, and original-branch parity review found no remaining
  P0; accepted P1 source blockers were resolved before building.
- [x] Build: `b8305b3b` passed the WinBoat candidate and exact-HEAD builds plus
  compile, unit/static, package, and evidence audits.
- [x] Deploy: exact package `b8305b3b` passed target prerequisite/readiness
  checks and connected to its matching single-container Foundry server.
- [ ] Runtime: complete the two-client domain matrix on Windows and Linux
  Proton/UMU with Monado, including Index bindings and controller navigation.
- [ ] Compatibility: validate HIGGS, PLANCK, VRIK, SkyrimVR-FBT, Realm of
  Lorkhan, DevBench, and the selected FUS native DLL set.
- [ ] Release: publish the audited package as a prerelease with source revision,
  dependency lock, server instructions, runtime checklist, known limitations,
  and rollback procedure.

## Next Stage Order

1. Finalize and audit the private Linux/Windows handoff from the accepted
   `b8305b3b` build and gameplay-bootstrap evidence.
2. Execute and retain
   the Windows/Linux two-client gameplay matrix. Only that evidence can remove
   the connection/bootstrap-alpha restriction.
