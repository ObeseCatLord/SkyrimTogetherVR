# Senior Review Brief: Full Gameplay Replication for Skyrim Together VR

## Review Request

Perform a Sol-max senior architecture and implementation-planning review of the
remaining work required to make Skyrim Together VR reproduce the original
Skyrim Together gameplay experience on Skyrim VR 1.4.15. Verify every
load-bearing claim against the current worktree and original branch before
critiquing it. The output must be a prioritized, dependency-aware work program,
not a general porting essay and not a statement that the existing gameplay
package is complete.

The project is maintained by a solo operator with agent assistance. Avoid
enterprise ceremony, but require explicit ABI, lifecycle, multiplayer semantic,
and runtime evidence gates because errors execute inside SkyrimVR.exe.
The user explicitly permits replacing or substantially refactoring earlier VR
work that was created with weaker models. Preserve behavior and evidence that
is actually sound; do not preserve an abstraction merely because it already
exists.

## Goal and Completion Standard

The target end state is full multiplayer gameplay parity appropriate to Skyrim
VR, including:

- connection/session/mod mapping and reconnect across load transitions;
- remote player spawn/despawn, movement, animation, equipment, VRIK/HIGGS/FBT
  pose embodiment, and cell/world transitions;
- synchronized actors/NPCs, ownership, packages, factions, mounts, dialogue,
  subtitles, teleportation, death, respawn, and actor values;
- object discovery/assignment, activation, lock state, inventory, and scripted
  animation;
- player inventory/equipment, magic, combat, projectiles, quests, party,
  calendar/time, weather, map/waypoint, and relevant player state;
- VR-native grabbing/physical interaction without breaking HIGGS or PLANCK;
- installation and launch on Windows and Linux/Proton/Monado through a workflow
  comparable to regular Skyrim Together;
- matched client/server protocol and compatibility with the active FUS native
  DLL stack, especially VRIK, HIGGS, PLANCK, Controller Fix VR, Skyrim VR
  Tools, and SkyrimVR-FBT.

Completion requires two-client runtime evidence for semantic effects, not only
packet relay, cache files, successful compilation, address coverage, or absence
of a crash.

## Environment Facts

| Fact | Value | Status |
|---|---|---|
| Repository | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR` | `[verified: git rev-parse]` |
| Current branch/head | `main` at `a117a46aeaf6bf87463560314e9d1589deb7b15f` | `[verified: git log]` |
| Comparison branch | `original-skyrim-together` at `9d81ef07d68e4bb2bd94fca246e798a564b7fb92` | `[verified: git branch/submodule history]` |
| Skyrim VR runtime | 1.4.15, Steam app 611670 | `[verified: target config, installed executable, address tooling]` |
| Local game | `/home/obesecatlord/FasterGames/SteamLibrary/steamapps/common/SkyrimVR` | `[verified: filesystem]` |
| Linux XR path | Proton plus Monado and patched XRizer/OpenVR facade | `[verified: launch scripts and local runtime]` |
| Windows build path | WinBoat SSH and checked-in PowerShell/BAT wrappers | `[verified: source and prior build manifests]` |
| Server | one ARM64 Docker server, UDP 26099, original services plus VR relay services | `[verified: server source and deployment docs]` |
| Runtime authority | User performs in-headset verification; agents may automate menus with DevBench but must not claim visual/gameplay success without evidence | `[verified: user constraint]` |
| Build timing | Do not spend another Windows compile/rebuild until the planned code tranche is complete | `[verified: user constraint]` |

## Verified Current State

### Stable substrate

1. `[verified: installed handoff files dated 2026-07-15]` The default package
   reached `state=online`, `online=1`, `playerId=3`, lifecycle `ready`, and a
   valid interior-cell request. This proves the connection-only substrate for
   that run, not gameplay replication.
2. `[verified: Code/client/main.cpp:242-295 and TiltedOnlineApp.cpp:66-202]`
   Skyrim VR startup/update/shutdown is owned by pinned VR `WinMain` and
   `Main::Draw` paths with a lifecycle gate, task permit, and hook failure
   handling.
3. `[verified: Docs/SkyrimVR/address-audit.md]` All 284 referenced non-RTTI
   IDs have a candidate mapping. Forty-seven unused RTTI IDs remain missing and
   no missing RTTI is used by `Cast<>`.
4. `[verified: Code/server/World.cpp:36-61]` The server constructs the normal
   gameplay services and nine VR relay services.
5. `[verified: current bridge handoffs]` VRIK, HIGGS, and PLANCK bridges load;
   HIGGS callbacks/snapshots are available. PLANCK remains observation-only.

### Gameplay target is staged, not complete

1. `[verified: Code/client/xmake.lua:116-131]` Three VR clients exist. The
   gameplay target sets connection-only false and enables pose, observation,
   remote-player proxy, and actor-target flags, but leaves unvalidated hooks,
   flat overlay, validated inline patches, and skeleton writes disabled.
2. `[verified: Code/client/World.cpp:161-265]` The gameplay target constructs
   the complete original service graph plus the VR relay graph. Construction is
   not proof that those services have valid VR event producers, layouts,
   engine-call ABIs, remote entities, or mutation boundaries.
3. `[verified: Code/client/main.cpp:264-277]` Every VR target currently skips
   `TiltedOnlineApp::InstallHooks2()` because
   `TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=0`; only bring-up hooks install.
   Therefore the static `Initializer` batch that produces many original
   gameplay events is absent even in the gameplay executable.
4. `[verified: source scan]` Thirty-eight client translation units declare
   `TiltedPhoques::Initializer` work, spanning animation, references, save/load,
   actor, equip, combat, magic, projectile, UI, input, renderer, and script VM
   paths. They are not independently classified or enabled for VR.
5. `[verified: Docs/SkyrimVR/inline-patch-audit.md]` Fourteen upstream-active
   inline patch sites remain disabled and require semantic disassembly review;
   two do not even resolve to candidate call boundaries. Zero are default
   enableable.
6. `[verified: Docs/SkyrimVR/address-audit.md]` Candidate address coverage is
   not semantic proof. Seventy-five mappings come from release data and 180
   from Address Library, while the report explicitly says `addrlib.csv` rows
   need validation before gameplay hooks are enabled.
7. `[verified: current package manifest]` The newest local gameplay package was
   built on 2026-07-12 from dirty revision `6eb55313...`, before the current
   lifecycle/crash fixes. It is stale and has no accepted gameplay runtime
   evidence.
8. `[verified: runtime evidence inventory]` The newest runtime archive is a
   default observe/crash diagnostic, not avatar-sync or gameplay evidence. No
   current two-client gameplay acceptance archive exists.
9. `[verified: Games/Skyrim/Events/EventDispacther.cpp:11-40]` All
   `EventDispatcher::RegisterSink` and `UnRegisterSink` calls are hard no-ops in
   VR. This means constructed VR activation/hit/grab/load/magic/projectile
   observers do not actually subscribe to engine event sources.
10. `[verified: CommonLibSSE-NG RE/B/BSTEvent.h]` CommonLib implements
    `BSTEventSource::AddEventSink`/`RemoveEventSink` inline over three
    `BSTArray<Sink*>` fields, a `BSSpinLock`, and the notifying flag; it is not
    a relocated member call. The current comment that registration merely
    awaits an exact `BSTEventSource::AddEventSink` target is therefore
    misleading. A safe local implementation still requires the exact game
    allocator/array and lock contracts plus teardown ordering.

### VR relay limitations

1. `[verified: Generic/VR*Service.cpp and server VR*RelayService.cpp]` Pose,
   movement, equipment, activation, magic-effect, combat-hit, projectile-intent,
   grab, and HIGGS messages are authenticated and relayed by player ID.
2. `[verified: Docs/SkyrimVR/connection-only-mode.md:74-105]` Most lanes are
   explicitly observation/cache only. They do not perform original entity
   ownership, inventory mutation, object activation, spell casting, projectile
   launch, damage, or remote grab/drop semantics.
3. `[verified: Generic/CharacterService.cpp:107-544]` Remote actor targeting
   validates same-space movement and pose data. Root movement and selected
   equipment draw state can be queued when a normal remote actor exists.
   Direct scene-node/skeleton writes remain disabled, actor movement authority
   is reported as disabled, and FBT body data is cache-only.
4. `[verified: current default compatibility handoff]` The proven run was
   connection-only with all movement/inventory/action relay and remote-avatar
   policies disabled. It does not exercise the staged lanes.

## Gameplay Parity Matrix

The following is the main agent's current assessment. The reviewer must verify,
correct, merge, or split these rows and cite exact source evidence.

| Domain | Original path | Current VR state | Assessment |
|---|---|---|---|
| Session/mod mapping | Transport, ModSystem, StringCache | Proven in one default run | implemented, needs regression evidence |
| Lifecycle/load/reconnect | load hooks and normal services | lifecycle polling/menus; native load sink disabled | partial; reconnect/save-load semantics unproven |
| Cell/grid discovery | DiscoveryService actor/reference scan | cell/grid observation; default actor enumeration disabled | partial; blocks entities |
| Remote player entities | CharacterService spawn/ownership | normal service constructed only in avatar/gameplay; relay caches need existing actor | high-risk partial |
| Movement | movement events and entity components | player-ID snapshot relay; guarded root target | partial mutation, no proven authority/interpolation |
| Animation | animation hooks/actions/graph vars | normal hook batch disabled | absent at runtime |
| VRIK pose | not in desktop original | pose protocol and validation exist | cache/target partial; no proven remote IK embodiment |
| HIGGS | not in desktop original | callbacks/state relay | observation only; no physical remote semantics |
| PLANCK | not in desktop original | bridge/build telemetry | observation only |
| FBT | not in desktop original | pelvis/leg schema and cache | no remote skeleton application |
| Inventory/equipment | InventoryService, equip/inventory hooks | equipment snapshot and weapon-draw assist | partial; full inventory/equipment mutation unproven |
| Objects | ObjectService assignment/activate/lock/inventory | activation telemetry relay | original semantics absent at runtime |
| Magic | MagicService cast/target/effect hooks | effect telemetry only | original semantics absent at runtime |
| Combat/health/death | Combat + ActorValue services and hooks | hit telemetry only | original semantics absent at runtime |
| Projectiles | launch hook/replay | bow/spell intent telemetry | launch/replay absent |
| Quests | quest event sinks and remote mutation | normal service constructed | event ABI and mutation unverified |
| Actor/NPC ownership | CharacterService discovery/spawn/transfer | normal service constructed | event producers and engine writes unverified |
| Party/dialogue/mounts | Party/Character/Player services | normal services constructed | runtime semantics unverified |
| Calendar/weather | Calendar/Weather services | normal services constructed | engine globals/calls unverified |
| Map/waypoint | Map service and player hooks | normal service constructed | hooks disabled; unverified |
| Overlay/control | CEF/DirectInput overlay | desktop companion/file handoff | functional external control; no in-headset parity |
| Voice | optional Vivox | inherited build option | unknown/not acceptance-tested |
| Server persistence/config | original server plus relay services | running ARM64 container | protocol revision/deployment upgrade required per tranche |

## Current Architectural Lean

Adopt a capability-gated VR gameplay architecture rather than enabling the
monolithic original initializer batch:

1. Preserve the proven `Main::Draw` owner, lifecycle gate, transport, and file
   control substrate.
2. Build a machine-readable gameplay capability manifest. Every original
   service must declare its required event producers, reads, mutations,
   addresses, ABI/layout evidence, mod-interaction boundary, and runtime test.
3. Split initializer installation by capability/domain. A gameplay package must
   not acquire unrelated renderer/UI/script patches merely to obtain actor or
   inventory events.
4. Establish actor/reference discovery and remote entity lifecycle first.
   Player-ID pose relays cannot produce visible multiplayer without a reliable
   server-ID-to-live-actor mapping and spawn/despawn/ownership state.
5. Reuse original protocol/services where VR ABI and semantics match. Use
   VR-specific messages only for VR-only data (head/hands/VRIK/HIGGS/FBT) or
   when a deliberate compatibility/version boundary is necessary.
6. Add remote mutation one domain at a time behind independently buildable and
   auditable flags: avatar movement/pose, equipment, actor values/death,
   objects, combat/projectiles/magic, quests/world state, then physical
   interactions.
7. Treat HIGGS/PLANCK integration as explicit authority arbitration. Do not
   replay desktop activation/damage/physics and HIGGS/PLANCK callbacks for the
   same action without deduplication and ownership rules.
8. Require two-client semantic evidence for every capability before enabling it
   in the ordinary gameplay package.

Rejected current alternative: set `TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=1` and
run the existing gameplay executable. This couples 38 initializer translation
units, candidate-only addresses, unresolved inline patches, and HIGGS/PLANCK
interaction into one crash surface and still would not prove VR controller,
pose, or physical-interaction semantics.

## Open Decisions for Sol-max

### D1: Keystone architecture

- **Lean:** capability-gated original-service reuse with VR-native event
  producers and VR-only pose/physics extensions.
- **Alternative:** continue parallel player-ID relay services until they fully
  replace original gameplay services.
- **Concern:** parallel relays may create a second incompatible game model;
  direct original reuse may retain too much flat-runtime ABI coupling.

### D2: First implementation tranche

- **Lean:** actor/reference discovery plus remote player entity lifecycle and
  same-space spawn/despawn mapping, with no combat/inventory mutations yet.
- **Alternative:** prove the already-written pose/movement relay first using two
  clients.
- **Concern:** pose testing needs a remote actor; entity lifecycle may itself
  depend on several disabled event producers.

### D3: Hook decomposition

- **Lean:** replace global `Initializer::RunAll()` for VR with named domain
  registries/capabilities and exact per-hook validation metadata.
- **Alternative:** add compile-time guards around every existing initializer.
- **Concern:** a broad refactor could complicate upstream rebases; scattered
  guards are easier to miss and harder to audit.

### D4: Address/CommonLib strategy

- **Lean:** retain `VersionDb` translation for upstream compatibility but use
  CommonLibSSE-NG VR layouts/events/relocations as the semantic oracle and wrap
  each validated function in a typed contract.
- **Alternative:** migrate the whole client to CommonLibSSE-NG now.
- **Concern:** wholesale migration is large; candidate ID translation alone is
  insufficient for hook ABI and field-layout proof.

### D5: Remote avatar embodiment

- **Lean:** actor-root movement remains authoritative through CharacterService;
  a post-VRIK actor-specific application barrier drives head/hands/body IK. Do
  not write arbitrary scene-node world transforms from `Main::Draw`.
- **Alternative:** direct node writes already present behind the skeleton flag.
- **Concern:** VRIK/HIGGS/PLANCK update order and remote-vs-local actor API
  ownership are not proven.

### D6: Physical interaction authority

- **Lean:** synchronize intent and authoritative object/actor state, deduplicate
  vanilla activation/hit events against HIGGS/PLANCK callbacks, and let each
  client simulate local physics presentation.
- **Alternative:** network raw hand transforms/collision impulses.
- **Concern:** deterministic Havok/PLANCK replication is unrealistic and could
  create feedback loops or divergent damage.

### D7: Protocol/versioning

- **Lean:** introduce a VR gameplay capability handshake and protocol revision
  before enabling mutating VR lanes; reject mismatched clients/servers.
- **Alternative:** rely on the matched-build convention already documented.
- **Concern:** nested body format validation is not negotiation, and server
  relays currently exist unconditionally.

### D8: UI scope

- **Lean:** keep the external companion as the reliable control plane while
  gameplay is ported; later add a VR-native menu via Scaleform/SKSEVR without
  flat DirectInput/D3D hooks.
- **Alternative:** port the original overlay early.
- **Concern:** UI work should not block gameplay, but an installable public mod
  ultimately needs discoverable in-headset connection/status controls.

### D9: Upstream maintainability

- **Lean:** isolate VR additions behind small adapters/capabilities and keep the
  original branch as a comparison/upstream-import base.
- **Alternative:** continue invasive `#if TP_SKYRIM_VR` changes inside original
  services and layouts.
- **Concern:** current CharacterService and many layout headers already have
  large VR conditionals that will make future upstream merges expensive.

Suspected overlap: D1, D3, D4, and D9 may be one architecture decision. D2 and
D5 may be one vertical slice. Merge them if that yields a clearer plan.

## Required Review Output

1. Correct the parity matrix with exact `file:line` evidence.
2. Identify every missing or unproven capability required for full gameplay,
   grouped by dependency rather than source-directory order.
3. Rank P0/P1/P2 work and define the minimum coherent vertical slice that
   produces visible two-client gameplay beyond connection-only mode.
4. Specify the hook/address/ABI validation method and whether the current
   address audit is sufficient for any mutating service.
5. Define protocol/authority/lifecycle invariants, especially across load,
   disconnect, cell transition, HIGGS/PLANCK events, and stale pose data.
6. Produce a delegation map with disjoint file/module ownership. Mark which
   slices are safe for Terra implementation subagents and which require main
   agent integration or another Sol-max review.
7. Define build and two-client runtime evidence gates for each tranche.
8. Surface likely crash, corruption, desync, feedback-loop, and future-upstream
   failure modes that this brief missed.
9. Separate genuine user priority decisions from technical decisions the agent
   should make directly.

## Depth Budget and Not-list

- Spend depth on architecture, dependency ordering, ABI/event/mutation safety,
  multiplayer semantics, and a concrete implementation/delegation program.
- Do not re-review handoff ZIP contents, GitHub release mechanics, server Docker
  basics, character-creation keyboard automation, or the already-proven
  connection-only startup chronology unless they materially constrain full
  gameplay.
- Do not run Skyrim or compile the project. This is a read-only review.
- Do not expose server passwords, raw session logs, or unrelated machine data.
- Do not treat generated audits as authoritative without checking what they
  actually cover.
