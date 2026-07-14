# Skyrim Together VR pre-authentication crash: senior review brief

## Goal and scope

Make the default Skyrim Together VR connection-only client authenticate and send its first cell/grid messages without a crash. This is a focused correctness/ABI decision for a solo-maintained port; do not re-review the entire port or propose enabling gameplay synchronization.

## Verified evidence

- [verified: autonomous DevBench run, 2026-07-14 12:33 local] A fresh process selected New Game, completed RaceSex, entered Realm of Lorkhan, finalized a Nord player, and confirmed `SkyrimTogether.esp` active.
- [verified: `tp_client.log`] Tick dispatch is stable before connect. The command logs `queueing connection to incidentalstoat.xyz:10578`, then the process crashes about 650 ms later.
- [verified: handoff files] The process-local session is `34536078031916`; status reaches `state=connecting`, `online=0`, `connectionGeneration=0`. No cell/grid request is sent.
- [verified: remote Docker log] The server sees no authentication or player admission for this attempt.
- [verified: minidump exception stream] Exception `0xc0000005`, thread 464, address `0x140131aab`, invalid read. The address is in the mapped Skyrim VR `.game` image. The dump is `crash_UTC_2026-07-14_17-34-39.dmp` in the installed game root.
- [verified: code/log ordering] The last client log is emitted by `VRConnectionService::QueueConnect`. Its runner callback calls `TransportService::Connect`; the next game-facing stage is `TransportService::OnConnected`, which constructs and sends `AuthenticationRequest`.
- [verified: source] `TransportService::OnConnected` calls Discord context, `PlayerCharacter::Get`, base form/NPC full name, `ModManager::Get` plus every loaded mod, world-space and cell form translation, player level, and `TimeData` calendar globals before `Send(request)`.
- [verified: source] The default VR target emplaces `DiscordService`, `StringCacheService`, `DiscoveryService`, `PlayerService`, and `VRConnectionService`; optional observation/pose/proxy services are disabled.
- [verified: runtime] `DiscoveryService` already reads the local player, parent cell, and location successfully before connect after the prior direct-VR `GetCurrentLocation` fix.
- [verified: server source] Authentication requires matching build version and receives username/mod list/world/cell/level/time, but the review must verify which fields are actually required for acceptance and immediate post-auth behavior.

## Environment facts

| Item | Value |
|---|---|
| Repository | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR` |
| Default target | `SkyrimTogetherVRClient`, connection-only, discovery + player-cell network lane |
| Runtime | Skyrim VR 1.4.15 under Proton/Monado |
| Server | One Docker server at `incidentalstoat.xyz:10578` |
| Main source | `Code/client/Services/Generic/TransportService.cpp` |
| Queue source | `Code/client/Services/Generic/VRConnectionService.cpp` |
| World composition | `Code/client/World.cpp`, `Code/client/xmake.lua` |
| Crash address | mapped game RVA `0x131aab`; static executable bytes are Steam-encrypted and the small dump does not contain that code page |
| Rebuild path | WinBoat Windows VM; each native rebuild/deploy is costly, so recommend a single defensible patch plus diagnostics |

## Current interpretation

The transport reaches its network-connected callback, then a desktop-era game accessor used only to decorate the authentication request faults. The precise accessor is not proven because `OnConnected` has no stage logs and the dump cannot unwind through the Steam-protected game image. Connection-only VR should assemble authentication from the smallest validated snapshot surface and make optional metadata best-effort, rather than invoking every desktop accessor on the network callback.

## Open decisions

1. **Authentication data boundary**
   - Lean: add a VR-specific authentication builder that avoids unsafe game calls in `OnConnected`; capture only validated player/cell data on the game tick, and use conservative defaults for optional name/time fields.
   - Alternative: retain all calls and add one log before each to discover the offender. Rejected as the final approach because it likely consumes another build merely for localization and leaves other unvalidated calls in a critical callback.

2. **Mod list source**
   - Lean: verify whether direct `ModManager` iteration is ABI-safe and required. If not proven, build a validated mod snapshot earlier on the game tick or omit only when the server permits it.
   - Alternative: send an empty list. Rejected unless server semantics prove it cannot create a mismatch or unsafe later mapping.

3. **World/cell/level/time fields**
   - Lean: reuse player-cell/discovery snapshot values for world/cell/level, or defer them to the post-auth cell requests. Default calendar values if the server permits.
   - Alternative: continue direct access in the callback. Rejected because one of these calls may be the current fault and the callback thread is not established as a safe game-access context.

4. **Diagnostics and failure containment**
   - Lean: stage-log the builder, validate pointers/translation results, refuse or degrade individual optional fields, and ensure status reports a pre-auth build/send failure rather than crashing.
   - Alternative: SEH-wrap the whole callback. Rejected as primary design because continuing after an access violation can hide ABI corruption; a narrow guard may be acceptable only as a final containment layer.

## Suspected overlap

Decisions 1 and 3 may be one architectural issue: game-state reads belong on the game tick, while the network callback should consume immutable snapshots. Merge them if source verification supports it.

## Reviewer assignment

Verify the actual callback/thread ordering, server authentication requirements, and all game-facing calls reachable from connect through accepted authentication and first cell/grid sends. Return a prioritized critique with file/line evidence, the safest one-build patch scope, tests/audits needed, and any later failure that the proposed patch would merely expose. Do not edit files. Do not broaden into avatar/gameplay sync, UI overlay, server deployment, or controller bindings.
