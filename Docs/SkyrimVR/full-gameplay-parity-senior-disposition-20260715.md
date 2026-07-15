# Full Gameplay Parity Senior Review Disposition

Date: 2026-07-15

## Decision

The existing connection and lifecycle substrate remains useful, but the staged
VR gameplay implementation is not a safe foundation for full parity. The next
phase will replace its actor ownership and engine-integration assumptions with
a conventional CommonLibSSE-NG SKSEVR gameplay adapter and one canonical
network entity model.

The target remains VR-enhanced gameplay parity: original Skyrim Together
semantics plus VRIK, HIGGS, PLANCK, and FBT data where VR needs additional
state. This is not a telemetry-only target.

## Disposition

| Review recommendation | Disposition | Implementation consequence |
|---|---|---|
| Stop appending `ActorExtension` storage to Skyrim VR actors | Adopted | VR online state moves to canonical entities and a native-handle table; no cast from a game actor to `ExActor`/`ExPlayerCharacter` is permitted. |
| Do not enable the original initializer batch wholesale | Adopted | VR engine integration is capability-scoped and independently enabled. Candidate addresses never enable a capability by themselves. |
| Put VR engine ownership behind a CommonLibSSE-NG SKSEVR plugin | Adopted | A bidirectional `VRGameplayBridge` plugin owns event sinks, actor handles, validated reads, actor creation/destruction, and mutations. |
| Keep transport and canonical replicated state in the mapped client | Adopted | The mapped Skyrim Together client remains responsible for protocol state, interpolation, ownership, and queues; it does not retain native actor pointers. |
| Use fixed-width, versioned POD at the module boundary | Adopted | No STL objects, C++ callbacks, game types, or allocator ownership crosses the gameplay bridge ABI. |
| Engine callbacks only enqueue records | Adopted | Event callbacks never call `World` or networking code. Game mutations execute through commands on a validated game-owner thread. |
| Use process-lifetime event sinks | Adopted | Static sinks register once. Endpoint/lifecycle retirement makes them inert; service destruction cannot leave dangling sink vtables in Skyrim arrays. |
| Add session, lifecycle, entity-generation, and action identities | Adopted | Protocol and bridge records carry enough identity to reject stale work after reconnect, load, cell transition, entity reuse, or retransmission. |
| Add capability negotiation | Adopted | Client and server negotiate protocol revision and gameplay capabilities before enabling each replication lane. |
| Establish explicit server authority and deduplication | Adopted | The server owns entity identity, ownership leases, interest routing, and acceptance/deduplication of actions, including HIGGS/PLANCK overlap. |
| Refactor a minimal avatar slice before broad service reuse | Adopted | First slice is one same-cell remote humanoid with spawn, root transform, stop/turn, disconnect/despawn, and epoch invalidation only. |
| Treat the current player-ID VR relays as the long-term game model | Rejected | Useful parsing/validation may be retained, but parallel relay state will not remain a second source of entity truth. |
| Migrate every client subsystem to CommonLib immediately | Rejected | CommonLib is mandatory at the VR engine boundary. Unrelated transport/server code remains unchanged unless a demonstrated dependency requires refactoring. |

## Verified Corrections

The review's architectural findings stand, with these evidence corrections:

1. The original CharmedBaryon checkout has exact Git metadata at
   `b93280e832f263dbef44e44cbe2936622a02f91a`, but that upstream has not moved
   since 2024. The project now pins the maintained alandtse CommonLibSSE-NG
   continuation at tagged commit
   `4071906acbe5255325a00f71283d995748f6cda5` (`v4.25.1`), matching the
   DevBench dependency already exercised in this Skyrim VR environment.
2. Local `Actor` asserts a flat-runtime size of `0x2B8`, while CommonLib's VR
   layout is `0x2B0`. Local `PlayerCharacter` has no VR size assertion and
   CommonLib's VR size is `0x12D8`. These mismatches make appended extension
   storage unsafe on real VR objects.
3. VR gameplay actor polling is enabled in the staged gameplay target, but
   reference discovery remains blocked because the local VR
   `TESObjectCELL::GetReferenceData()` path returns null.
4. Native event registration is absent: the existing VR dispatcher methods are
   no-ops. CommonLib event-source operations are inline layout operations, not
   a missing relocated `AddEventSink` function.
5. `Actor::New()` is not currently a valid VR factory path. It depends on
   allocator and constructor hooks initialized by the initializer batch that
   all VR targets skip.

## Architecture

### Module ownership

`SkyrimTogether.exe` / mapped client:

- transport, authentication, mod mapping, canonical entities, ownership,
  interpolation, protocol capability state, and stale-message rejection;
- consumes immutable bridge events and emits explicit bridge commands;
- never stores a native `Actor*`, scene node, or engine-owned container.

`SkyrimTogetherVRGameplay.dll` / CommonLib SKSEVR plugin:

- process-lifetime native event sinks and lifecycle observation;
- native actor/reference handles and generation validation;
- validated actor creation, lookup, transform, and destruction operations;
- bounded event production and bounded command consumption;
- no direct network or `World` calls.

Server:

- canonical server instance/session/entity identities;
- entity generation and ownership leases;
- interest management and action deduplication;
- capability-dependent routing and rejection.

### Required identity

Every state-changing path must be bindable to:

`server instance nonce + connection generation + lifecycle epoch + entity ID + entity generation`

Discrete actions additionally carry a monotonic action ID. Ordered streaming
records additionally carry a sequence number. Any mismatch is rejected before
touching game state.

### Capability rule

A capability is enabled only when all of these exist:

1. exact runtime/layout dependency pin;
2. typed event/read/mutation contract;
3. lifecycle and thread-owner contract;
4. stale-handle and queue-overflow behavior;
5. mod-interaction policy where applicable;
6. server/client protocol support;
7. two-client semantic acceptance evidence.

## Dependency-Ordered Work

### P0: Safe avatar foundation

1. Pin SkyrimVR `1.4.15`, SKSEVR, CommonLibSSE-NG, Address Library input, and
   bridge ABI versions in machine-readable build metadata.
2. Add a POD-only gameplay bridge ABI with bounded queues, capability bits,
   lifecycle epochs, entity generations, and explicit overflow counters.
3. Add the CommonLib SKSEVR gameplay adapter with static sinks, endpoint
   retirement, owner-thread command execution, and no gameplay mutations yet.
4. Add mapped-client bridge discovery, negotiation, event draining, command
   submission, and lifecycle retirement.
5. Remove VR dependence on appended `ActorExtension` storage and build the
   adapter-owned native handle table.
6. Extend authentication/protocol state with revision, capabilities, server
   nonce, connection generation, entity generation, action ID, and sequence.
7. Implement server entity authority and interest routing for the first remote
   avatar.
8. Validate exactly one VR actor factory/despawn path and root-transform path.
9. Demonstrate the minimum avatar slice across two clients and ten
   reconnect/load cycles before enabling another mutation lane.

### P1: Original gameplay semantics

Add domains in dependency order: animation graph/action events, equipment and
inventory, actor values/death/respawn, object assignment/activation/locks,
combat/projectiles/magic, quests/party/dialogue/mounts, and world state. Each
domain is independently capability-gated and requires semantic evidence.

### P2: VR embodiment and physical interaction

Add VRIK/FBT remote pose application behind a documented update-order barrier.
Then add HIGGS/PLANCK interaction authority, action IDs, ownership transfer,
and vanilla-event deduplication. Raw local physics simulation is not treated as
network authority.

### P3: Product parity

Validate UI/control, voice, install/update, Windows launch, and
Linux/Proton/Monado launch against the same compatibility and release matrix.

## First Acceptance Slice

The first build that is worth an in-game gameplay test must do only this:

- negotiate the avatar capability;
- discover the local player and bind it to a canonical server entity;
- create one visible same-cell remote humanoid through a validated VR factory;
- apply root position/heading/stop state through the gameplay adapter;
- retire handles and despawn on disconnect, load, epoch change, or entity reuse;
- keep inventory, actor values, combat, magic, equipment, death, animation,
  skeleton writes, HIGGS, and PLANCK mutations disabled.

Acceptance requires two clients to observe unique spawn, movement, turn, stop,
leave, and despawn over ten reconnect/load cycles with no stale actor, duplicate
entity, use-after-free, allocator fault, or queue corruption.

## Refactor Policy

Large refactors are permitted where they remove unsafe flat-runtime
assumptions or duplicate entity models. Existing code is retained only when it
has a clear owner, validated ABI, and compatible multiplayer semantics. The
original branch remains the behavioral reference and comparison base, not a
requirement to preserve its internal architecture inside Skyrim VR.
