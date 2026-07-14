# Post-character first-tick crash senior disposition

| Recommendation | Disposition | Implementation |
| --- | --- | --- |
| Use direct VR TES singleton ID `516923`. | Adopted | `TES::Get()` selects `516923` only under `TP_SKYRIM_VR`; desktop keeps `400441` and pointer indirection is unchanged. |
| Use direct VR TESDataHandler ID `514141`. | Adopted | Local `ModManager` is the TESDataHandler shim; its VR branch now uses `514141`, while the verified D60 file-list and TESFile layouts remain unchanged. |
| Use direct VR ProcessLists ID `514167`. | Adopted | Removes dependence on the generated `514167 -> 400315` alias in the live discovery path. |
| Use VR/SE `Actor::GetLevel` ID `36344`. | Adopted | Authentication and online level updates no longer call the unrelated VR function at legacy ID `37334`. |
| Use direct VR Calendar ID `514287`. | Adopted | `TimeData::Get()` selects the CommonLib Calendar singleton in VR; its verified 0x40 layout is unchanged. |
| Correct INISettingCollection while touching the same singleton class. | Adapted | It is not reached by connection-only `DiscoveryService::OnConnected`, but the proven CommonLib ID `524557` is now selected in VR to prevent the same fault class in gameplay mode. |
| Change TES offsets B0/B4 or B8/BC based on CommonLib field names. | Rejected | The original client intentionally uses B0/B4 as loaded-grid center and B8/BC as queued/current player coordinates. The crash dump proves an invalid TES base, not an offset overrun. |
| Gate unproved actor-handle discovery in the admission build. | Adopted | Default connection-only builds publish the local player as the sole discovery actor and avoid `ProcessLists`/`GetByHandle`; avatar-sync and gameplay builds retain full actor discovery. |
| Gate every observation service. | Adapted | Static layout/accessor audits already cover their player-facing update paths. They remain observation-only and are not claimed runtime-validated until the clean run passes; no unrelated service was removed. |
| Require fresh, two-sided connection proof. | Adopted | Automation removes stale command/status/player-cell files before launch and rejects proof files older than the run. Success still requires local online state/nonzero ID and remote server admission. |
| Assert corrected selections in source audits. | Adopted | `audit_vr_services.py` requires each exact VR/desktop conditional and the default actor-handle gate. |

The Sol Max review additionally resolved the crash dump registers: the faulting
TES result was `0x3002320600020601`, and the subsequent legacy data-handler
result was also invalid. These values make the singleton corrections mandatory
rather than defensive guesses.
