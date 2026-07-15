# Full Runtime Safety Senior Review Disposition

Two Sol-max reviews were used before the corrective build. The focused review
traced the pre-address-library failure to version text being parsed only after
manual PE mapping. The broader review independently identified path-qualified
module-name spoofing as a concrete regression and found four additional native
safety gaps. The implementation addresses both immediate hypotheses because
they are independently unsafe and inexpensive to remove.

| Recommendation | Disposition | Implementation |
| --- | --- | --- |
| Preserve the runtime version as fixed-width data before mapping. | Adopted | `VS_FIXEDFILEINFO` is decoded into four integer fields before `ExeLoader::Load`; no heap string crosses the mapping boundary. |
| Spoof only exact bare executable module names. | Adopted | `GetModuleHandleA/W` and `LdrGetDllHandleEx` reject path-qualified names and compare exact case-insensitive `SkyrimVR.exe` requests. |
| Keep corrected module-filename size semantics. | Adopted | The bounded A/W conversions remain; truncation now sets `ERROR_INSUFFICIENT_BUFFER`, and allocation failure sets `ERROR_NOT_ENOUGH_MEMORY`. |
| Treat unwind-table registration failure as fatal. | Adopted | `LoadExceptionTable` is result-bearing and aborts mapping before the game entrypoint. Full in-process rollback is unnecessary because startup exits the mapper process on this failure. |
| Create persistent diagnostics before VersionDb. | Adopted | The logger is constructed before address loading and records runtime/path/failure reason. The address-library dialog includes the structured reason. |
| Require and transactionally apply helper overlays. | Adopted | Missing, empty, malformed, duplicate, truncated, or unresolved helper rows fail closed and clear partial maps. CSV and binary base paths use the same overlay policy. |
| Let explicit project overrides outrank generated aliases. | Adopted | Aliases still overwrite ordinary raw numeric-ID collisions, including `19362 -> 19789`; an explicit target override is retained. Package audit remains responsible for reporting generated overlap. |
| Harden the legacy binary parser. | Adopted | Every read is checked; runtime/module/pointer size/count are validated; delta arithmetic is overflow/underflow checked; binary overlay failures propagate. |
| Validate every default hook target before MinHook. | Adopted | IDs `35545` and `35560` must resolve to exact RVAs, committed executable pages, and their decrypted Skyrim VR 1.4.15 16-byte prologues. |
| Explicitly close and destroy the active client world. | Adopted | Tick production retires first, transport disconnect is delivered while subscribers exist, connection-only and gameplay context services are erased in reverse dependency order, hooks are removed, and the locator is reset idempotently. |
| Fully audit all dormant gameplay hooks now. | Deferred | They remain compile-time disabled in the default client and cannot cause the current startup failure. They require separate ABI/semantic proof before gameplay mode can ship. |
| Patch the engine Havok fault directly. | Rejected | The repeated `0x140131AAB` fault identifies a malformed pointer consumer, not its writer. Observer mode now constructs no `World`; a full-memory dump is still required if the signature recurs. |

## Runtime Gates

1. Default observer mode must open the explicit Skyrim executable version
   resource, then the base and both helper CSVs, validate both hook contracts,
   enter mapped `gameMain`, and construct no `World`.
2. Active mode without autoconnect must defer `World` construction until the
   original outermost `Main::Draw` returns after character readiness.
3. Active autoconnect must authenticate, receive lifecycle/cell state, then
   disconnect and exit with no late callback or stale server connection.
4. VRIK/HIGGS/PLANCK coexistence must leave physics, skeleton, input, renderer,
   RaceSex, and Havok writes disabled in connection-only mode.
5. Missing/stale helper files, wrong prologues, malformed binaries, and unwind
   registration failure must stop before mapped game entry with a specific log.

## Residual Risk

The review did not exhaust disabled gameplay synchronization, every historical
address ID, USVFS permutations, packet-level reconnect races, or all PE-loader
fault injection. Those surfaces are not release-cleared by this disposition.
The next runtime pass is limited to observer and default connection-only mode.
