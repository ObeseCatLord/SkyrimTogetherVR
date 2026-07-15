# Skyrim Together VR Full Runtime Safety Senior-Review Brief

## Goal and scale

Perform a fresh, broad native-code safety and compatibility audit of the Skyrim
Together VR port before its next Windows build. The goal is a distributable
Skyrim VR 1.4.15 client that starts through the normal Skyrim Together launcher,
coexists with SKSEVR/VRIK/HIGGS/PLANCK, reaches the server, and fails closed
instead of corrupting game state. This is a solo-maintained port: require
specific safety evidence, not enterprise process.

The reviewer is read-only. Verify the repository and runtime artifacts before
critiquing. Compare `main` with `original-skyrim-together` where behavior was
changed or removed. Return prioritized, actionable findings with file/line
evidence and the smallest coherent pre-build patch set.

## Environment facts

| Fact | Evidence |
| --- | --- |
| Repository | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR` |
| Audited branch/revision | `main`, `aa0d7801dc39182f292dcd944fb499caad393321` |
| Original comparison | local branch `original-skyrim-together` |
| Reverse-hook submodule | `Libraries/TiltedReverse` at `569633b5286426db1f6ba77e0fc9d4ee90bad334` |
| Runtime | Skyrim VR 1.4.15 under GE-Proton 10-34, Monado/XRizer |
| Default safety mode | connection-only; gameplay hooks and inline patches compile-time disabled |
| Required coexistence | SKSEVR 2.0.12, VR Address Library, VRIK, HIGGS, PLANCK |
| Server | one Docker server on the remote host, UDP 26099 |
| Build result | revision `aa0d7801` built and passed package/static audits on Windows |

## Verified evidence

1. `[verified: Wine +file trace]` The `aa0d7801` executable shows the VR Address
   Library error before opening `version-1-4-15-0.csv`, either helper CSV, or a
   `versionlib` file. It does successfully open the explicit SkyrimVR executable
   during version-resource lookup.
2. `[verified: A/B launch with identical prefix/game/data]` Package `b4556de6`
   opens all three address CSVs and enters mapped `gameMain`; `aa0d7801` does not.
3. `[verified: source diff]` The relevant pre-entry behavioral change broadens
   `GetModuleHandleA/W` interception from a bare executable-name comparison to a
   case-insensitive basename comparison that also accepts full paths.
4. `[verified: PE metadata inspection]` The installed `SkyrimVR.exe` file and
   product versions are `1.4.15.0`; the address CSV exists and passes package
   audit. The failure is therefore upstream of CSV parsing.
5. `[verified: source ordering]` `CoreStubsInit()` enables loader API hooks before
   `LoadProgram()` calls `QueryFileVersion()`. `TiltedOnlineApp`, which installs
   the file logger, is currently constructed only after `VersionDb::Load()`.
6. `[verified: prior runtime logs/dump]` Earlier builds repeatedly faulted at
   mapped address `0x140131AAB`, inside Skyrim VR's Havok-world lookup, with a
   pointer consistent with a one-byte-misaligned `ExtraHavok*`. The writer is
   not proven by the available minimal dumps.
7. `[verified: prior Sol-max disposition]` Revision `aa0d7801` deferred `World`
   construction until after the original outermost exact `Main::Draw`, made
   observer mode construct no `World`, reduced default hooks to WinMain plus
   Main::Draw, disabled invalid VR event registration and Discord callbacks,
   and made hook installation transactional.
8. `[verified: VersionDb tests/source]` Generated aliases overwrite ordinary
   raw VR numeric-ID collisions. Aliases conflicting with explicit project
   overrides cause CSV loading to fail; the binary-library path currently
   ignores the boolean result of `ApplyVrProjectAddressFiles()`.
9. `[verified: static audits before build]` Exact-hook, address-coverage,
   CommonLib-layout, inline-patch, bridge, service, and VRIK/HIGGS/PLANCK/FBT
   audits passed. Treat these as evidence to challenge, not proof of runtime
   correctness.

## Current architecture

- The immersive launcher maps `SkyrimVR.exe` into its own process, emulates
  loader APIs, applies imports/relocations/TLS, bootstraps SKSEVR, then enters
  mapped game startup.
- `VersionDb` loads the VR Address Library plus project overrides and generated
  AE-to-SE aliases. `VersionDbPtr` lazily caches resolved process addresses.
- Exact VR WinMain owns idempotent shutdown. Exact outermost `Main::Draw` owns
  deferred client startup and, in active mode, activation-thread updates.
- `VRTickBridge` receives SKSE task/Papyrus cadence and coalesces permits for the
  activation-thread owner.
- Default connection-only mode still constructs networking/client services but
  skips unvalidated gameplay hooks, event sinks, Discord callbacks, and the flat
  overlay.

## Open decisions and current lean

### D1: Module-name interception semantics

Current lean: `GetModuleHandleA/W` should spoof only an exact bare
`SkyrimVR.exe` request. A full path must pass through to Windows. Length-aware
basename matching may remain only where `LdrGetDllHandleEx` is documented or
observed to receive path-qualified `UNICODE_STRING` names.

Rejected alternative: keep broad matching and special-case version APIs. That
leaves unrelated callers vulnerable to receiving the mapped image for a path
that names a different loaded module.

### D2: Pre-address-library diagnostics

Current lean: initialize the process-local logger before `VersionDb::Load()` and
record the queried executable version, requested CSV path, and a structured
VersionDb failure reason. Construction must not create `World` or touch game
memory.

### D3: VersionDb parser and overlay failure policy

Current lean: make all binary reads length-checked, reject invalid pointer size,
counts, arithmetic underflow/overflow, and truncated files, and propagate helper
overlay failures on both CSV and binary paths. Explicit project override
conflicts should remain a hard failure with exact IDs/offsets in the diagnostic.

### D4: Loader-emulation correctness

Current lean: audit every hooked Win32/NT loader API against its size units,
null/truncation semantics, case/path behavior, lifetime, recursion, and Wine/MO2
behavior. Prefer the narrowest spoof required by mapped Skyrim/SKSEVR.

### D5: Address and hook proof

Current lean: every default-enabled target must have exact VR provenance, an
executable-section check, expected instruction/signature validation where
possible, ABI proof, transactional install, and teardown proof. Lazy static
address caches must not preserve null/stale resolutions across retries.

### D6: Client/service lifetime

Current lean: no callback, detached thread, task, event sink, transport handler,
or bridge producer may outlive the registry/services it accesses. Startup
failures must unwind producers and hooks in reverse order; shutdown must be
idempotent from partial states.

### D7: Compatibility containment

Current lean: connection-only mode should not write actor physics, skeleton,
input, Havok, RaceSex, renderer, or menu state, allowing HIGGS/PLANCK/VRIK to own
those surfaces. Detecting those mods must not itself depend on unsafe loader
callbacks or path assumptions.

## Required audit matrix

1. PE mapping and loader API emulation: imports, delay imports, relocations,
   sections/protections, TLS, CRT startup, exception/unwind metadata, module
   lookup/path spoofing, version resources, and partial-failure rollback.
2. Address loading: version parsing, CSV and binary parser bounds, ID alias and
   reverse-map consistency, explicit override precedence, missing/malformed
   files, executable-section validation, and lazy cache behavior.
3. Every default native detour/IAT patch: target provenance, ABI, original-call
   ordering, return behavior, recursion/reentrancy, thread contract, install
   transaction, and uninstall safety.
4. Startup/teardown and concurrency: app/World construction, SKSE bootstrap,
   bridge endpoint, task/Papyrus callbacks, transport handlers, threads, atomics,
   exceptions, retries, and shutdown races.
5. Connection flow: character-ready gating, autoconnect, authentication schema,
   mod list, server rejection/error paths, reconnect/disconnect, null services,
   and no-overlay operation.
6. VR coexistence: VRIK/HIGGS/PLANCK/SKSEVR ownership conflicts, input and
   RaceSex interaction, and dormant desktop code accidentally reachable under
   default flags.
7. Compare changed control flow with `original-skyrim-together`; identify any
   required initialization or connection step lost during the VR reduction.

## Deliverable

- Correct any false premise first.
- Findings ordered P0-P3 with exact file/line evidence.
- Label each finding `rebuild-blocking`, `runtime-matrix`, or `future-gameplay`.
- For each finding: failure mechanism, trigger, confidence, smallest fix, and a
  deterministic static/unit/runtime check.
- Produce a decision table for D1-D7: adopt, adapt, or reject with reason.
- End with one consolidated pre-build patch list and one post-build runtime
  sequence. Do not edit files.

## Depth budget and not-list

Spend depth on crash prevention, ABI/API correctness, data corruption, and lost
connection prerequisites. Do not review server performance, UI aesthetics, FBT
pose replication, disabled gameplay synchronization implementation, general
style, or packaging prose unless it creates a native runtime failure. Do not
expand into speculative engine reverse engineering without code/runtime
evidence.
