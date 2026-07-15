# Runtime Crash-Surface Senior Review Disposition

The Sol-max review audited revision `b4556de6` after the repeated Skyrim VR
access violation at RVA `0x131AAB`. The fault is inside the engine's Havok-world
lookup and is consistent with a one-byte-misaligned `ExtraHavok*`; the available
minimal dumps do not identify the writer. The port therefore removes credible
corruption sources and uses a staged runtime matrix instead of patching the
faulting engine instruction.

| Recommendation | Disposition | Implementation |
| --- | --- | --- |
| Defer client startup until Skyrim is initialized. | Adopted | WinMain only forwards and owns shutdown. Active startup occurs once, after the original outermost `Main::Draw` returns. |
| Keep observer mode free of client services. | Adopted | Observer mode activates only the process-local tick endpoint; it never constructs `World`. |
| Disable the invalid VR event registration target. | Adopted | Default lifecycle/discovery registration is disabled and VR `InternalRegisterSink`/`InternalUnRegisterSink` fail closed. |
| Remove VM-update and renderer-init observers. | Adopted | The default hook set is exactly WinMain plus `Main::Draw`. |
| Disable Discord's detached callback thread. | Adopted | Connection-only mode keeps a zero-state service for authentication schema compatibility but does not call `DiscordService::Init()`. |
| Fix mapped-loader API contract bugs. | Adopted | Module matching is exact by basename; `UNICODE_STRING` byte lengths and `GetModuleFileNameA/W` capacities and return values are bounded. |
| Make MinHook installation transactional. | Adopted | Any install failure rolls back owned hooks; normal teardown removes hooks and restores IAT entries in reverse order. |
| Make startup allocation failure nonfatal to Skyrim. | Adopted | `World::Create()` catches construction failure and deferred startup retires the bridge while leaving the game running. |
| Prevent override/alias collisions. | Adopted | Runtime loading rejects aliases that conflict with explicit project overrides while retaining required VR-ID collision translations such as `19362 -> 19789`. |
| Patch `ExtraDataList::GetHavokWorld`. | Rejected | No writer or ownership violation is proven; masking a malformed pointer would hide corruption and risk Havok state damage. |
| Immediately rewrite TickBridge sequencing. | Deferred | Current SKSE task execution is serialized. Change to monotonic-max semantics only if runtime evidence proves callback reordering. |
| Restore load-event sinks now. | Deferred | Requires exact decrypted VR semantic proof for `BSTEventSource::AddEventSink` and owner-thread cache invalidation. |

## Required Runtime Matrix

1. Baseline Skyrim VR/SKSEVR without the STVR mapper, first with VRIK, then HIGGS and PLANCK.
2. STVR mapper with client update mode `off`.
3. Minimal observer mode: WinMain plus `Main::Draw`, no `World`.
4. Active deferred startup without autoconnect.
5. Active deferred startup with autoconnect and server authentication.

Use the normal RaceSex confirmation path. If RVA `0x131AAB` recurs, capture a
full-memory dump containing heap and TLS pages; another minimal dump cannot
identify whether the malformed pointer came from TLS caching, the extra-data
list, or an earlier writer.
