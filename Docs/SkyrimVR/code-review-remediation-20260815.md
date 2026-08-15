# Code Review Remediation Disposition (2026-08-15)

## Current Authority

The release is a **connection/bootstrap alpha** only. No claim of
gameplay-ready, gameplay-alpha, beta, parity, loader-readiness, or multi-client
success is authorized for the current source tree. The current worktree is
unbuilt; historical build and single-client connection evidence apply only to
their recorded revisions.

Source, build, and runtime are separate states:

- **Source** records reviewed code present in the worktree or an explicit
  fail-closed policy.
- **Build** requires a passing WinBoat candidate and clean committed build with
  package/evidence audits for that exact revision.
- **Runtime** requires retained matching deployment evidence; gameplay requires
  two Skyrim VR clients and the matching server.

## Disposition

| Item | Current source disposition | Build | Runtime |
| --- | --- | --- | --- |
| CommonLib | Current unbuilt source is alandtse `CommonLibVR` `ng` 6.3.1 `108836139ee612651f6c6c4dc4c41e673dcde623`, merged by project commit `e74c63b8dd9cebb84a3dc1386cfaf40059ec3d65`. Former 6.1.1 / `612394bda3e2674da585831702308d571cf991b6` provenance is historical. | Pending | Pending |
| Quest synchronization | Intentionally disabled and fail-closed. `QuestMutation` exists but is unadvertised; quest commands return `Unsupported`. Dialogue and party remain separate source domains. | Pending | Pending; not quest-parity proof |
| `SetCombatTarget` | Unsupported and fail-closed. It is not complete combat synchronization. | Pending | Pending; excluded from complete claims |
| VR set-time and teleport producers | Command-file producers for set-time, teleport-to-player, and admin teleport are implemented with validation and stable-transport checks. | Pending | Pending |
| Startup CRT bypass | Integrated source makes VR CRT hooks forwarding-only and gives active outermost `Main::Draw` sole startup ownership. | Pending final build | Pending |
| Startup readiness | The complete 30-gate source-readiness suite passes locally. CI runs it from temporary report paths, executes every focused Python test, and rejects authoritative report drift or checkout side effects. This is source/audit evidence, not package or runtime evidence. | Pending final build | Pending |
| PE loader | Integrated source validates exact size/SHA-256 and bounded PE structures before mapping; focused fixtures exist. | Pending final build | Pending |
| `Movement` `std::bit_cast` | Integrated source uses `std::bit_cast` and preserves the serialized float bit pattern in a unit test. | Pending final build | Pending |
| Projectile regression | Integrated source preserves cast/projectile ownership and adds a focused no-echo source regression. | Pending final build | Pending |
| Server command authorization | Set-time derives identity from the sender connection, ordinary teleport requires distinct players in the same nonempty party, and admin teleport checks the sender before target lookup. Focused authorization tests pass. | Pending final build | Pending |
| Two-client diagnostics | Authentication milestones correlate player, connection, protocol, capabilities, nonces, and generation without passwords, addresses, or mod-list dumps. Bridge health is emitted once per 30 seconds while ready and once on final disconnect/shutdown. | Pending final build | Pending two-client capture |
| Linux/Windows handoff | Dry-run-by-default installers validate the exact legal executable and the complete handoff/package/evidence identity. Windows PowerShell 5.1 and Linux self-tests pass; no installed game was modified by those tests. | Pending clean build and generated archive | Pending target-machine install |
| Multi-client gameplay | No current source assertion substitutes for this gate. | Requires final package | Pending two-client matrix |

## Exact Next Gates

1. Complete the final senior review of the integrated source and resolve every
   accepted blocker.
2. Run the WinBoat candidate build for the
   exact worktree revision.
3. Commit and push only after that candidate passes.
4. Run the clean build of the committed revision with handoff generation;
   retain the package, build evidence, package audit, loader result, readiness
   result, and audited handoff archive.
5. Deploy that exact client package with the matching dedicated-server revision.
6. Capture a fresh single-client connection/bootstrap result. This confirms only
   the alpha scope; it does not prove gameplay.
7. Run and retain the Windows/Linux two-client gameplay matrix, including remote
   presence and the declared gameplay lanes. Only successful matching evidence
   can remove the connection/bootstrap-alpha restriction.

Historical evidence remains in its dated records and must not be rewritten as
proof for this source tree.

## Final Senior Review Disposition

| Sol finding | Disposition |
| --- | --- |
| Publish CommonLib gitlink | Adopted as a pre-parent-push gate. The candidate helper securely bundles the local commit, so the project fork is pushed only after the candidate passes and before the parent commit is published. |
| Restore default Monado socket mount | Adopted. Both launchers restore the existing non-symlink socket RW mount when no isolated runtime is selected; focused tests cover default and isolated paths. |
| Linux installer was write-by-default | Adopted. Validation is now the default and `--install`/`--apply` is required for writes. |
| Linux installer verified only nested build artifacts | Adopted. It rejects links and non-regular payloads, requires an exact record set, and verifies every recorded file before helper import or target writes. |
| Invalid set-time reported success | Adopted. Calendar rejection returns the additive `kInvalidInput` result; encoding and compile coverage are present. |
| Command archive claimed processing | Adopted. Successful local dispatch is archived as `.sent`; documentation explicitly denies server-application semantics. |
| Readiness PR checkout used branch head | Adopted. Pull requests use GitHub's tested merge revision; pushes retain their exact revision. |
| Authorization tests were source-only | Adapted. Encoding tests and the server build run natively, and focused source contracts cover authorization branches. Full behavior tests require linking the server/world/global-service graph and remain a test gap; runtime claims remain pending. |

The final senior pass found no P0. Its P1 source blockers are resolved; the
Windows candidate, publication, clean build, install, and runtime gates below
remain mandatory.
