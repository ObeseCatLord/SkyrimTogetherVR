# Fadeout, resync, and connection-stability senior-review brief (2026-08-22)

## Review objective

Determine the smallest correct repair that gives the Skyrim VR port desktop Skyrim Together's observable connection behavior across startup and load/fade boundaries: authenticate once, complete bootstrap, remain online, retire stale lifecycle work on a real load boundary, reconnect once if retirement requires it, and never leave the user behind a stale Fader menu. The immediate acceptance target is a new-character run against the exact protocol-21 server that remains healthy for at least two minutes and survives one deliberate fade/load transition without a retry storm, process abort, or server-side `BadConnection`.

This is a solo-operator development environment. Conservative, observable recovery is preferred to broad retries or unconditional UI manipulation.

## Environment

| Component | Verified value |
|---|---|
| Source revision under test | `891f9ebf002df5524a0c5051e3200ef491719382` (`stvr-v0.1.0-alpha.1-121-g891f9ebf`) |
| Protocol | 21; full-capability run negotiated `0x3ff80e0f`, isolation run negotiated `0x3ff00e0f` |
| Client package | `SkyrimTogetherVR-gameplay-891f9ebf-20260822.zip`, SHA-256 `db52bf3ba681466e75feb908d919133aca1f401e2af54c9aba31abadf7595ecf` |
| Server | exact `skyrim-together-vr-server:891f9ebf-arm64`, UDP 26099, no server restarts |
| Game/runtime | Skyrim VR 1.4.15, GE-Proton10-34, XRizer `v0.5-17-g3131956-dirty`, managed Monado simulated-qwerty profile |
| Client install | isolated install and compatdata roots; no save passed to launcher |
| Save state | all prior `.ess`/`.skse` files moved to a recoverable quarantine; active Saves directory verified empty before testing |
| Working tree caveat | pre-existing dirty `Libraries/activeragdoll` submodule is user-owned and out of scope |

## Verified observations

### Connection and lifecycle

- [verified: client log] A fresh character named `STVRTest` reaches Realm of Lorkhan, finishes RaceSex, and has a valid player/base/cell before connect.
- [verified: client log] Authentication succeeds with exact client/server version and protocol 21. In the 15:39 run, auth is accepted at `tp_client.log:9492`, then connection rehydration reaches `authenticated` at line 9506 and avatar bootstrap begins at lines 9509-9511.
- [verified: earlier complete run] A prior fresh run advanced through `authenticated -> bootstrap -> assigned -> domains_active -> ready` in about two seconds. It then logged `VR canonical recovery: quest canonical resync request budget exhausted` roughly five seconds later.
- [verified: code] `VRConnectionService` retains endpoint/password at `Code/client/Services/Generic/VRConnectionService.cpp:759-760`. A genuine lifecycle invalidation closes an authenticated transport before reconnect and retains one pending connect at lines 363-389. A Fader menu by itself is not a lifecycle blocker.
- [verified: server log] The server accepts each exact client, then reports `BadConnection` only after the client process disappears. No server restart or version rejection occurs.

### Quest canonical resync defect

- [verified: code] `VRWorldReplicationService::OnConnected` unconditionally requests a resync for the local player at `Code/client/Services/Generic/VRWorldReplicationService.cpp:2269-2273`.
- [verified: code] `CanAuthorizeQuestOwner` requires a nonzero shared party ID at `Code/encoding/Structs/GameplayCapabilities.h:125-130`.
- [verified: code] The server therefore rejects a local player's initial pre-party request at `Code/server/Services/QuestService.cpp:89-94`; it sends no negative acknowledgement or empty snapshot.
- [verified: code] The client retries at two-second intervals up to three attempts and then erases the request with `quest canonical resync request budget exhausted` at `VRWorldReplicationService.cpp:1719-1742`.
- [verified: protocol contract] `NotifyQuestResync` explicitly supports both party-bound (`HasParty=true`, nonzero `PartyId`) and unbound (`HasParty=false`, zero `PartyId`) snapshots at `Code/encoding/Messages/NotifyQuestResync.h:23-26`.
- [verified: tests] Current policy tests cover same-party/cross-party authorization but not self-owner authorization before party membership (`Code/tests/revisioned_canonical_recovery.cpp:68-84`). Encoding tests cover only a party-bound response.
- [opinion] The likely intended contract is: a requester may always obtain its own canonical snapshot, including an empty snapshot, without a party; another owner's snapshot still requires common nonzero party membership. The server response should faithfully set `HasParty`/`PartyId` from the current optional party. An alternative is to defer all local resync until party join, but that leaves canonical local recovery unavailable to a connected solo client and makes `HasParty=false` largely unused.

### Deterministic client fault; XRizer panic is not established as causal

- [verified: launch log] Two consecutive fresh-character connected runs printed an XRizer FFI panic about 185 ms after auth acceptance:
  - auth at 15:37:03.771; XRizer reports `panic in a function that cannot unwind` at 15:37:03.956;
  - auth at 15:39:27.272; XRizer reports the same panic at 15:39:27.457.
- [verified: later diagnostic run] With `RUST_BACKTRACE=full` and `RUST_LOG=xrizer=trace`, XRizer continued successful Wait/Begin/Submit/present cadence after auth and then stopped abruptly without any XRizer panic or Rust backtrace. Auth completed at 15:52:29.588, connected dispatch completed by 15:52:29.594, rehydration entered bootstrap at 15:52:29.595, and valid frame traces continued through 15:52:29.698 before the process/DevBench endpoint disappeared.
- [verified: process behavior] DevBench then receives connection refusal/reset; the server later observes `BadConnection`. This makes the network disconnect downstream of a local client-process failure, but does not make XRizer the initiating component.
- [verified: repeated Windows fault] Multiple runs also report an access violation at mapped Skyrim image address `0x1402767C6` (RVA `0x2767C6`), read address `0x384`, with `RCX=0x25C`, `RDX=1`; crash thread IDs vary. The packed on-disk executable prevents trustworthy static disassembly of this RVA, and no usable minidump was produced under Proton.
- [verified: Windows SEH backtrace and PDB] A full Wine SEH trace captured two game threads faulting nearly simultaneously through Skyrim RVAs `0x2767C6 -> 0x2B843C -> 0x2B806F -> ... -> 0x2A88A4`. One return address is gameplay bridge VA `0x180012663`; the exact installed PDB resolves it to `ActivationHooks.cpp:178`, immediately after `HookActivateRef` calls the original `TESObjectREFR::ActivateRef` trampoline.
- [verified: hook target/ABI] The detour target RVA `0x2A8300`, 32-byte prologue, and six-argument signature agree with CommonLib SSE NG ID 19796, the repository's curated VR address contract, and an independent VR port. An address or signature typo is therefore not the leading explanation.
- [verified: unsafe breadth] `CapturePreActivation` calls `As<Actor>`, `PlayerCharacter::GetSingleton`, target/base/cell/worldspace accessors, position access, and `BGSOpenCloseForm::GetOpenState` before it proves the activator is the local player. Its policy currently admits every bounded actor reference, not only the local player (`LocalGameplayCapture.cpp:4560-4612`, `LocalGameplayCapture.h:37-55`). Consequently the global engine detour performs bridge reads on arbitrary activation threads, including engine-driven activations unrelated to the player.
- [verified: capability isolation] The trace run negotiated `0x3ff00e0f` after malformed HIGGS readout caused optional HIGGS and PLANCK relays to be omitted, yet the client still failed. Those optional negotiated relay paths are not necessary for this fault.
- [inference] Earlier XRizer `panic in a function that cannot unwind` lines are likely teardown fallout after the Windows-side fault, or a second failure path. The trace run falsifies the stronger claim that the Rust panic is proven to initiate the disconnect.
- [inference] The activation detour is now the first isolation target. The trace proves the fault occurs inside an original `ActivateRef` invocation reached through the bridge detour; it does not yet prove whether MinHook concurrency, pre-call native reads/timing, or an unrelated engine defect in that activation is causal.
- [contradictory evidence needing disposition] An earlier offline old-save run reportedly faulted at the same mapped Skyrim RVA. Server traffic is therefore not proven necessary for the Windows fault.

### Fade presentation

- [verified: code] `FaderRecovery` observes the real UI stack and only considers a hide when the stack is exactly HUD+Fader, player/base/cell context is stable, the transport has a nonzero server identity, no LoadingMenu is active, a three-second transition quiet period has elapsed, and the policy's hard timeout/evidence gates pass (`Code/vr_gameplay_bridge/FaderRecovery.cpp:17-40, 56-123, 151-227`; policy in `FaderRecoveryPolicy.h`).
- [verified: current bridge log] During fresh startup it logs `waiting for a safe HUD/Fader-only stack; blocker=additional_live_menu`; it does not issue an automatic hide before the abort.
- [verified: historical runtime evidence] On earlier exact builds the same policy reached candidate, issued exactly one hide, and verified Fader closure after a loaded save (`Docs/SkyrimVR/runtime-connection-result-20260819-c1ffb57e.md`). A fresh Realm run previously needed exact-state DevBench cleanup (`runtime-connection-result-20260818-c18ca1d8.md`).
- [opinion] Fader presentation should remain separate from lifecycle/network state. Making Fader itself retire the connection would turn a visual artifact into unnecessary reconnect churn. The runtime should eventually own safe stale-Fader closure so the test harness is not required, but unconditional `HideMenu(Fader)` is unsafe during legitimate transitions.

## Timeline model to review

1. Startup menus/RaceSex cause lifecycle epochs 2 and 3 while offline; both settle to `ready`.
2. Launch-bound command queues one connection.
3. Exact server authenticates; transport identity is stable and rehydration begins.
4. Current client immediately emits a local quest-resync request that the pre-party server policy rejects silently.
5. Current fresh runs suffer a local client-process fault inside an original `TESObjectREFR::ActivateRef` invocation reached through the gameplay bridge activation detour, causing the server's later `BadConnection`; XRizer continues valid frame submission after auth in the latest trace and is not established as the initiating fault.
6. When a run survives long enough, the unanswered quest request retries and logs budget exhaustion.
7. Fader recovery is observing startup fade state but has not issued an unsafe hide in the current runs.

## Decisions requested from the senior reviewer

1. Are the quest retry storm, XRizer abort/Windows fault, lifecycle retirement, and Fader presentation correctly modeled as separate state machines/failure families? Identify any verified coupling that this brief misses.
2. For quest recovery, choose and specify one contract:
   - authorize self-owner snapshots without a party and send `HasParty=false/PartyId=0`, while preserving same-party authorization for remote owners; or
   - defer local canonical resync until party establishment and explicitly complete/cancel the pre-party lane.
   Include server/client tests needed for empty snapshots, self-owner pre-party, remote-owner pre-party rejection, party transitions, revisions, and no silent retry budget exhaustion.
3. Given the symbolized `HookActivateRef` backtrace, should the exact activation detour be disabled, constrained before all native reads to a proven game-thread/local-player activation, or replaced by a filtered `TESActivateEvent` sink? Specify the smallest isolation sequence that distinguishes MinHook concurrency from unsafe pre-call reads and a native engine activation defect.
4. Should runtime Fader recovery policy change now? If so, give exact state/evidence rules. If not, define the acceptance evidence that proves it is already correct and the harness workaround can be removed later.
5. Produce a deep implementation specification for the top-ranked repair, including invariants, file/symbol changes, tests, diagnostics, rollback behavior, and runtime acceptance checks.

## Rejected shortcuts

- Increase canonical retry count or retry forever. This hides a contract mismatch and increases churn.
- Treat server `BadConnection` as the initiating fault. Current timing proves it follows local abort.
- Make every Fader appearance a lifecycle boundary or reconnect trigger.
- Hide Fader unconditionally or while LoadingMenu/other menus remain live.
- Disable all gameplay/VR hooks broadly. The now-implicated activation detour may be disabled alone as a bounded, evidence-producing isolation.
- Weaken exact build/protocol/capability admission to keep a connection apparently alive.
- Load an old save for convenience; new-character testing is a hard requirement.

## Suspected overlap / uncertainty flags

- Quest recovery begins synchronously from `ConnectedEvent`, but a normal network send should not directly call XRizer. Any proposed causal link must identify a concrete call path.
- `auth.connected_dispatch.done` proves all synchronous `ConnectedEvent` subscribers returned. Avatar/pose/bootstrap timing is therefore weaker evidence than the PDB-backed activation-hook stack.
- The mapped Skyrim RVA fault and XRizer FFI abort may be two views of one abort path under Wine, or independent failures. Do not collapse them without a backtrace or isolation result.
- The user's observed visual fade may be a stale Fader menu, an XR compositor session loss caused by the abort, or both.

## Not-list (out of review scope)

- WinBoat scheduled-task hardening, transactional package installer, artifact provenance, and server deployment automation.
- Broad protocol-21 parity outside quest recovery and the immediate connected-event crash surface.
- General modlist selection, graphics tuning, or physical-headset validation.
- Re-reviewing the already exact package/server version match.

## Required review output

Verify before critiquing. Return prioritized findings with file/line evidence; label facts, inferences, and remaining unknowns. Explicitly list human decisions. Then provide one deep specification for the highest-priority repair. Do not edit files.
