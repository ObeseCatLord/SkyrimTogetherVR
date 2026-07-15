# Movement And Animation Graph Stage Review Brief

Date: 2026-07-15

## Goal

Review the first Phase 1 gameplay slice before it is compiled: authoritative
remote root movement across exterior/interior cells, monotonic movement
ordering, reliable visibility-spawn recovery, and bidirectional humanoid
animation graph snapshots. This is a solo-maintained compatibility port, but a
bad ABI or owner-thread decision can corrupt Skyrim state, so fail-closed
behavior is required.

Discrete `ActorMediator` action capture/replay is explicitly outside this
slice. It remains required for full parity, but it must not be enabled from
address correspondence alone.

## Verified Evidence

| Claim | Status and evidence |
|---|---|
| The original wire protocol already carries cell/worldspace, transform, animation variables, direction, and action events. | `[verified: original branch and current encoding]` `Movement`, `ReferenceUpdate`, and `Client/ServerReferencesMoveRequest` remain wire-compatible. |
| The server retains original ownership/interest routing. | `[verified: code trace]` `PlayerService` emits cell/grid visibility and `CharacterService` forwards changed movement at 50 Hz. |
| The prior VR path discarded graph variables/actions, rejected different-cell movement, ignored server ticks, and dropped early spawns. | `[verified: pre-change main]` `VRAvatarService` only staged same-cell root transforms and required a ready local snapshot at spawn receipt. |
| CommonLib exposes named graph get/set APIs and a VR-aware `CreateReferenceAtLocation`. | `[verified: vendored alandtse CommonLib v4.37.0]` adapter code uses only CommonLib types on its owner thread. |
| CommonLib's internal move-to wrapper uses `RELOCATION_ID(56227, 56626)`; VR selects SE ID 56227. | `[verified: CommonLib source and RelocationID implementation]` the pinned VR address database contains ID 56227. Runtime semantics still require acceptance evidence. |
| Exact ActorMediator action replay is not yet proven for VR. | `[verified: focused senior trace]` IDs 38949/38953/39004 and globals 403566/403567 lack semantic/calling-convention proof despite address mappings; only `TESActionData` layout/vtable is strongly corroborated. |

## Implemented Contract Under Review

1. Gameplay bridge ABI/capability revision 2 keeps 64-byte POD payloads and
   adds spatial transfer plus local/remote graph snapshot capabilities.
2. Root commands now include local cell and worldspace form IDs. Creation uses
   CommonLib `CreateReferenceAtLocation`; a cell change uses the same relocation
   contract as CommonLib's private `MoveTo_Impl` wrapper.
3. Client and server reject duplicate/stale movement ticks while accepting an
   initial tick of zero. Server invalid-entity logging no longer dereferences an
   end iterator.
4. A bounded pending-spawn map retains visibility messages received before the
   local CommonLib snapshot/capability becomes ready.
5. Humanoid graph snapshots use the original Master Behavior descriptor order:
   60 booleans, 13 floats, and 14 integers. Five fixed-size chunks represent a
   full snapshot. Partial or superseded snapshots are never applied.
6. The adapter captures local graph variables through CommonLib named accessors
   at 10 Hz. The mapped client assembles complete snapshots into the existing
   network `Movement::Variables` fields.
7. Remote graph chunks are assembled per canonical avatar and applied on the
   command-pump owner thread. Root and animation sequence domains are tracked
   separately; snapshot IDs are idempotent.
8. Animation failure quarantines only that avatar's animation lane. Root/lifecycle
   remain independently active.

## Open Decisions

| Decision | Current lean | Rejected alternative |
|---|---|---|
| Cross-cell mutation API | Keep the exact CommonLib relocation contract locally until an upstream public wrapper exists. | Recreating the actor on every exterior cell boundary breaks identity and action state. |
| Snapshot failure behavior | Assemble fully before apply; retry partial queue submissions; quarantine animation on engine rejection. | Applying chunks immediately exposes half-updated graphs and makes queue loss visible. |
| Variable-name failure | Treat any remote named-set failure as engine rejection. | Silently accepting missing variables would advertise parity while locomotion state is incomplete. |
| Discrete actions | Keep capability absent until ActorMediator ABI is semantically verified; document graph-event fallback as non-parity only. | Calling mapped desktop hooks or claiming `NotifyAnimationGraph` is full replay is unsafe and behaviorally false. |
| Capability promotion | Advertise source-complete graph/spatial lanes in the experimental build, but retain runtime checklist gates. | Keeping implemented endpoints unnegotiated prevents meaningful two-client acceptance evidence. |

## Review Questions

1. Can any malformed, stale, partial, reordered, or retried chunk cause an
   out-of-bounds write, partial graph commit, feedback loop, or wrong-avatar
   mutation?
2. Is the relocation-based cell transfer signature and handle usage consistent
   with pinned CommonLib on Skyrim VR, or should source remain disabled pending
   a dedicated runtime probe?
3. Does movement tick rejection interact incorrectly with reconnect, assignment,
   world-clock reset, multi-entity packets, or server interest transitions?
4. Are capability negotiation and failure quarantine truly independent between
   lifecycle, root/spatial, and graph mutation?
5. What tests or audits are missing before the one clean WinBoat compile?

## Not In Scope

- appearance/equipment/inventory;
- actor values, death, combat, magic, quests, or world state;
- VRIK/FBT skeleton mutation, HIGGS, or PLANCK authority;
- exact action replay implementation before its binary ABI proof;
- runtime launch during this review.
