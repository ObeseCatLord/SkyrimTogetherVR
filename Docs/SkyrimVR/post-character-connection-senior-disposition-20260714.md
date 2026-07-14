# Senior Review Disposition: Post-Character Connection

| Recommendation | Disposition | Implementation |
| --- | --- | --- |
| Replace startup-thread equality with ABI-v2 task provenance. | Adopted | Endpoint ABI is v2. Each `ClientUpdateTask::Run` supplies a monotonic one-shot sequence and its executor TID; client dispatch verifies same-call TID, sequence, epoch, state, and non-reentrancy. Activation TID is telemetry only. |
| Keep the endpoint view read-only and fix state acquisition. | Adopted | Bridge state reads now use a volatile load followed by `MemoryBarrier`; validation reads state before dependent endpoint metadata. The old interlocked pseudo-read is forbidden by static audit. |
| Add task lifecycle evidence. | Adopted | First task entry, callback acceptance/result, and task disposal log sequence and thread data. |
| Keep Papyrus as primary cadence; defer native fallback. | Adopted | No autonomous native cadence was added. Canonical PEX deployment proved migration `OnInit -> Tick -> AddTask`. |
| Do not treat `kHide` as successful RaceSex completion. | Adopted | Automation requires the real MessageBox callback, excludes `cancelIndex`, waits for RaceSex to close, and fails otherwise. |
| Require HEDR 1.70 and canonical loose-script casing. | Adopted | Migration verification normalizes/requires exact HEDR 1.70. Staging/build/audits use `Data/Scripts`; package and target audits reject case-fold collisions and verify installed PEX hashes. |
| Use one connection source and deduplicate in-flight connects. | Adopted | Automation launches with environment autoconnect disabled, removes stale handoff state, then writes one command after player finalization. `VRConnectionService` ignores duplicate in-flight requests. |
| Correlate connection evidence. | Adopted | Acceptance requires successful task dispatch, local online status with nonzero player ID, client transport evidence, and a remote server admission during the same test window. |

## Runtime findings that changed the design

1. Skyrim VR silently omitted the plugin while its TES4 HEDR was 1.71; changing only
   that field to 1.70 made the plugin live.
2. Proton kept `Data/Scripts` and `Data/scripts` as distinct trees. The lowercase-only
   PEX deployment caused every Skyrim Together script bind to fail.
3. Once PEX loading was repaired, Papyrus immediately enqueued the SKSE task. The first
   task crashed in `SkyrimTogetherVRTickBridge+0x21f7`: an interlocked pseudo-read wrote
   to a read-only mapping.
4. Startup activation occurred on TID 308 while SKSE executed the task on TID 600.
   Fixed startup-thread equality could never authorize the proven executor, motivating
   the ABI-v2 one-shot permit.
