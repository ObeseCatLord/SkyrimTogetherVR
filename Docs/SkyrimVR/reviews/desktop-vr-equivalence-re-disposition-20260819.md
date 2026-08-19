# Desktop-to-VR Equivalence Reverse-Engineering Disposition

Date: 2026-08-19

Scope: behavior inherited from desktop Skyrim Together whose Skyrim VR native
equivalent was missing, ambiguous, or structurally different. This is source
and static executable evidence only. It is not build or runtime proof.

The locked input is SkyrimVR.exe 1.4.15.0 with SHA-256
`6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971`.
Opcode evidence was read from a Steam CEG-decrypted analysis image derived from
that exact file. The source hash, runtime, RVA, executable span, and entry
prologue remain the runtime admission boundary.

## Disposition

| Finding | Result |
|---|---|
| Calendar hook might suppress a broader VR function than desktop | Resolved by direct database and opcode evidence. VR ID 35402/RVA `0x5AD8F0` is `Calendar::Update(Calendar*, float GameDaysPassed)`. Its live-frame callers load the Calendar singleton and pass the frame delta. The body performs time/date advancement and rollover notification. Desktop ID 36291's automatic VR correlation at `0x5DB010` is an unrelated form-property thunk and is explicitly rejected. The direct VR target remains pinned. |
| Quest stage mutation used an unverified raw native call | Fixed. RVA `0x3803D0` is a synchronous `bool(TESQuest*, uint16_t)` stage mutator: it checks running state, finds the stage record, updates the highest/current stage, executes the stage path, and marks quest runtime data changed. The plugin now validates exact runtime, text span, page protection, RVA, and 24 entry bytes before advertising readiness or mutating. Completed-stage replay is idempotent, matching desktop `ScriptSetStage`. |
| Remote-root replay might lose a thread-local allowance in deferred work | Static trace found no such deferred re-entry. RVA `0x5E0E20` has one direct caller inside full `Actor::Process` at `0x5E07A0`; task thunks `0x7083D0` and `0x708430` enter the full process before reaching it. `Actor::SetPosition` at `0x5DC380` calls generic SetPosition and controller/Havok synchronization inline. The replay allowance covers the synchronous authoritative call; the atomic managed-actor registry independently protects task-thread classification. Runtime ragdoll/cell-transition proof remains required. |
| VR update ownership differs from the desktop VM inactive predicate | Deferred to instrumented runtime evidence. Main::Draw remains the sole verified startup/update owner. The current bridge lifecycle gate retires epochs before command mutation, but menu, save/load, and VM-transition traces must prove exactly one safe update per active frame. |
| Local health capture coalesces ordinary owner state over 100 ms | Deliberate transport adaptation, not yet literal event-order proof. Exact damage/heal hooks enforce remote authority; the owner-state lane now remains distinct from identity-authorized, replay-resistant attacker-originated NPC damage. This does not prove that a compromised client reported truthful damage. Two-client sub-100-ms damage/heal tests remain required. |
| Direct PLANCK/HIGGS physical behavior has no desktop equivalent | Not desktop-parity behavior and therefore cannot be inferred from Tilted Evolution. Canonical root, damage, inventory, grab intent, and full-body pose remain network-owned. Direct remote PLANCK constraint/ragdoll replay would require separate Skyrim VR/Havok and PLANCK API reverse engineering and must not be advertised by this candidate. |
| Generated actor-AI aliases could stand in for desktop hooks | Rejected. Generated mappings for desktop IDs 37577, 39643, and 42704 resolve to VR RVAs `0x62C830`, `0x6D96F0`, and `0x767A40`. Exact disassembly shows an unrelated wrapper, a three-float global copy, and a twelve-float vector copy respectively. These rows are registration hints only and must never be admitted as `Actor::IsFleeing`, dialogue `ProcessResponse`, or detection-state update targets. Any future native target requires the pinned decrypted-artifact hash, exact RVA, bounded detour bytes, ABI/call-shape evidence, semantic anchors, and caller validation. |
| Remote dialogue can already be queued before AI is disabled | Fixed in source before build. The verified `SpeakSound` and subtitle hooks now suppress native presentation for a managed remote speaker outside an explicit network-replay scope; replay calls the original trampoline once and is never captured as local dialogue. Diagnostics are logarithmic aggregates. A candidate response processor near VR RVA `0x681F80` has a plausible four-argument shape and calls verified `SpeakSound`, but its return ABI and desktop correspondence are not established, so no hook is admitted there. |
| Disabling AI subsumes desktop remote-player detection suppression | Not proven, but the prior fail-open admission is fixed in source. Confirmed `EnableAI(false)` is now a prerequisite for publishing a remote avatar. Refusal rejects and faults the create; restoration failure retains the actor/record behind a closed retirement gate. This is a conservative behavioral guard, not proof that queued detection, combat response, or package work cannot survive. Candidates around `0x743170` and `0x7431E0` remain forbidden until exact desktop-body and VR caller analysis distinguishes their semantics. |
| Desktop forces `IsFleeing` false for player-classified actors | Literal behavior is not currently implemented and is not a pre-build blocker while remote AI admission is fail closed. VR RVA `0x5F5280` is a stronger static candidate than the rejected generated row, but it remains untrusted without an authoritative desktop body, ABI, semantic anchors, and caller proof. Two-client fear/flee testing decides whether this becomes a required hook or obsolete defensive desktop behavior. |

## Completed Static-Source Equivalence

The current unbuilt source has exact, fail-closed VR contracts for activation,
calendar, remote equipment authority, summon authority, temporary remote-save
exclusion, local drop capture, and weather application/release. Each contract
uses the pinned 1.4.15.0 executable fingerprint plus direct RVA (or direct VR
ID 35402 for `Calendar::Update`), text/page range, and entry-prologue checks;
they are not generated CommonLib aliases. `VersionDb` also removes known false
VR IDs 34989, 37577, 39643, 40454, and 42704 after all mapping sources load.

This closes the static source-equivalence work only. It is neither a build
result nor evidence that one or two running clients preserve desktop behavior.

## Remaining Runtime Gates

- Trace update owner, menu state, loading/save lifecycle, command count, and
  lifecycle epoch across new game, load, reconnect, and shutdown.
- Prove calendar authority through midnight/month rollover, wait/sleep, menus,
  loading, disconnect, and reconnect without suppressing unrelated scheduling.
- Exercise running, stopped, current, new, and completed quest stages with
  aliases, objectives, and fragments; require no echo or partial mutation.
- Exercise locomotion, stairs, doors, cells, knockdown/ragdoll, HIGGS contact,
  and PLANCK contact with caller/thread/root diagnostics and no drift or fling.
- Prove that managed remote actors cannot originate native speech, subtitles,
  packages, combat detection, or fleeing behavior outside explicit replay, and
  that local/unmanaged actors remain unchanged.
- Exercise remote-avatar admission when AI disable succeeds, is refused, or
  throws. Require no usable fail-open avatar, one bounded diagnostic, safe
  retirement, and restoration of pre-existing-reference AI state.
- Prove ordered owner health, PvP on/off, NPC melee/projectile/magic damage,
  healing, regeneration, death, and replay rejection with two clients.
- Exercise every queued and immediate final-equipment path, including failed
  pre-mutation retry, terminal post-mutation failure, acknowledgement timeout,
  resync, retirement, and reconnect; prove exactly-once visible equipment.
- Correlate `PlayerCharacter::DropObject` with the synchronous
  `TESContainerChangedEvent` sink under stackable, unique, nested, remote, and
  rejected drops; uncertain event ordering must remain an inventory removal.
- Validate weather force/release and remote-save temporary marking on the target
  executable during create, load, save, disconnect, and teardown. A static RVA
  or post-call flag check does not prove the target's lifecycle behavior.
- Trace remote AI detection and flee behavior. `ProcessResponse`, detection,
  and `IsFleeing` remain deliberately unhooked because their candidate aliases
  are false or insufficiently proven.
- Treat direct PLANCK remote grab/ragdoll replay as an optional extension, not
  desktop parity; still prove canonical damage deduplication and pose/physics
  non-interference with PLANCK and without it.

No claim in this disposition upgrades the release beyond connection/bootstrap
alpha before matching two-client evidence exists.
