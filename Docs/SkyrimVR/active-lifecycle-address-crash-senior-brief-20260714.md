# Active Lifecycle Address Crash: Senior Review Brief

## Goal And Scale

Choose the smallest robust fix for the active Skyrim VR client crash that occurs on the first lifecycle menu probe, and audit the same relocation-collision failure class before requesting another Windows rebuild. This is a solo port and test loop: no enterprise ceremony, but another avoidable rebuild is expensive.

## Verified Evidence

| Claim | Status | Evidence |
|---|---|---|
| The active default client crashes on owner thread 320 during the second VM-update callback while the main menu is open. | `[verified: runtime log and minidump]` | Runtime log reaches lifecycle `main_menu`; dump `crash_UTC_2026-07-15_01-00-33.dmp` records `c0000005` at `SkyrimVR.exe+0xF43AD1`. |
| The game fault receives invalid `this=0x3002320600020601`. | `[verified: minidump context and stack unwind]` | RIP `0x140F43AD1` reads `[rcx+0x38]`; RCX/RDI/R14 are the same invalid value. |
| The first non-game frame is `UI::GetMenuOpen`, called by `VRLifecycleService::Update`. | `[verified: PDB symbolization]` | Launcher addresses symbolize to `UI.cpp:32`, `VRLifecycleService.cpp:102`, `World.cpp:279`, and `SkyrimVM64.cpp:105`. |
| `UI::Get()` currently asks `VersionDb` for ID 400327. | `[verified: source]` | `Code/client/Games/Skyrim/Interface/UI.cpp:18-22`. |
| The generated helper resolves 400327 directly to RVA `0x1C39D20`, whose stored value at runtime is the invalid pointer above. | `[verified: generated address report and dump memory]` | `Docs/SkyrimVR/address-audit.json` plus dump memory inspection. |
| CommonLib NG identifies the VR UI singleton as `RELOCATION_ID(514178, 400327)`; the VR release maps 514178 to `0x1F83200`. | `[verified: local CommonLib NG source and VR release CSV]` | `include/RE/Offsets.h` UI singleton and `version-1-4-15-0.csv`. CommonLib's `RelocationID` chooses the first ID for VR. |
| The runtime loader applies generated aliases after the raw VR release and overrides colliding target IDs. | `[verified: source and unit test]` | `VersionDb.h:205-206`, `ApplyIdAliasFile`; `Code/tests/version_db.cpp` proves a colliding raw ID is replaced. |
| The generator lacks this UI pair because its imported `se_ae.csv` is incomplete, then accepts a direct SSE-to-VR match for 400327. | `[verified: source and generated CSV]` | `audit_addresses.py:247-317`; current helper CSV has no `514178,400327` row. |
| Default `SkyrimTogetherVRClient` is connection-only with observation/gameplay services disabled. | `[verified: build definition]` | `Code/client/xmake.lua:6-46,116`; only discovery, player-cell, lifecycle, connection, Discord, and string-cache services are enabled. |
| `World::Update` runs lifecycle first and returns before dispatching service updates until lifecycle is ready. | `[verified: source]` | `Code/client/World.cpp:268-288`. |
| The default pre-ready path's only newly reached game objects are UI and then PlayerCharacter/base/cell identity. | `[verified: scoped call-graph audit]` | `VRLifecycleService.cpp:23-42,76-154`; Spark connection-path trace found no other relocation calls before the readiness gate. |
| A readable-page guard already exists for the player singleton but not the UI singleton. | `[verified: source]` | `Code/client/Games/Skyrim/VR/VRPlayerReadiness.h`; `UI::Get()` returns the raw stored value. |

## Same-Class Relocation Audit

The following desktop ID / CommonLib VR ID pairs are exact semantic matches for singleton/global objects, not speculative function translations. Their VR release offsets are present. Three are already selected explicitly by source branches (`TESDataHandler`, `Calendar`, `INISettingCollection`); the remainder are still vulnerable if reached because the generator currently emits direct translations for the desktop IDs.

| Object | Desktop ID | VR ID | VR RVA | Default pre-online reachability |
|---|---:|---:|---:|---|
| TESDataHandler | 400269 | 514141 | `0x1F82AD8` | Not before ready; already explicit VR branch |
| BGSCreatedObjectManager | 400320 | 514172 | `0x1F831C8` | No |
| UI | 400327 | 514178 | `0x1F83200` | **Yes; confirmed crash** |
| SubtitleManager | 400443 | 514283 | `0x1F850E8` | No |
| Calendar | 400447 | 514287 | `0x1F85108` | Not before ready; already explicit VR branch |
| SkyrimVM | 400475 | 514315 | `0x1F889D8` | Not in the VM-update hook path |
| ActorEquipManager | 400636 | 514494 | `0x2F896D8` | No |
| PlayerCamera | 400802 | 514642 | `0x2F8A888` | No |
| BSInputEnableManager | 400863 | 514705 | `0x2F8AAA0` | No |
| PlayerControls | 400864 | 514706 | `0x2F8AAA8` | No |
| MenuControls | 401263 | 515124 | `0x2FC52E8` | No |
| BGSSaveLoadGame | 403330 | 516851 | `0x2FEB200` | No |
| PlayerCharacter god-mode global | 404238 | 517711 | `0x2FFFDEA` | No |
| INISettingCollection | 411155 | 524557 | `0x3175FE0` | Not before ready; already explicit VR branch |
| Renderer data | 411347 | 524728 | `0x317E790` | Flat overlay path disabled; getter remains compiled |
| D3D11 device | 411348 | 524729 | `0x317E798` | Flat overlay path disabled; getter remains compiled |

Lower-numbered function-ID mismatches are intentionally excluded from blanket conversion. Runtime evidence has already shown that some apparently corresponding CommonLib pairs name different functions in this codebase (for example the validated renderer bring-up hook), so semantic identity must be proven per function.

## Environment Facts

| Fact | Value |
|---|---|
| Repository | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR` |
| Branch / deployed source | `main`; deployed build is commit `8b5b6cf5` |
| Game | Skyrim VR 1.4.15, CEG-encrypted executable; decrypted analysis copy at `/tmp/SkyrimVR.ceg-decrypted.exe` |
| Address inputs | Imported VR database/addrlib/SSE-VR maps plus installed VR release CSV |
| Runtime address load order | VR release CSV, project override CSV, then authoritative project alias CSV |
| Build target for next test | `SkyrimTogetherVRClient` default connection-only target |
| Test constraint | No build or runtime test until code/review work is complete; Windows build will be done through WinBoat after approval of this patch set |

## Open Decisions And Current Lean

1. **Fix location: generator alias vs. direct source branch.**
   - Lean: add a curated, source-controlled CommonLib VR alias map to `audit_addresses.py`, merged over imported `se_ae.csv`, and assert every curated row is present in the generated runtime CSV. This repairs the existing translation-layer contract and protects future upstream merges that reintroduce desktop IDs.
   - Adaptation: also select VR ID 514178 directly in `UI::Get()` only if the reviewer sees material defense-in-depth value; otherwise avoid redundant port-only call-site churn.
   - Rejected: patch only the generated CSV by hand, because the next audit run would erase it.

2. **Scope of curated aliases.**
   - Lean: include all 16 exact singleton/global pairs in the table, including the three already explicit in source. They are mechanically backed by CommonLib semantic names and the VR release, and they are the same failure class.
   - Rejected: blanket-import all 46 CommonLib ID differences, because several function pairs are not semantically interchangeable in this project.

3. **Pre-ready pointer safety rail.**
   - Lean: generalize the existing readable-page helper name and make lifecycle treat an unreadable UI singleton as `ui_unavailable`, logging once. Keep address correctness as the primary fix; the guard only converts obviously bad/uninitialized pointers into suspension.
   - Rejected: broad SEH wrappers around game calls, because they hide ABI/address defects and cannot make a wrong readable object safe.

4. **Regression checks.**
   - Lean: make `audit_addresses.py` fail unless every curated alias row is generated and add/extend focused tests for alias collision precedence. Keep build/runtime testing for the end.
   - Rejected: row-count-only validation, because it passed while the critical UI row was absent.

5. **Connection test sequence after one rebuild.**
   - Lean: deploy default target; run observer startup first; run active startup through documented DevBench input automation; require lifecycle `ready`, connection status `online`, and remote server admission before expanding to avatar/gameplay targets.
   - Rejected: immediately test gameplay target, because it multiplies relocation and layout surface before the connection baseline is proven.

Decisions 1 and 2 overlap intentionally: the reviewer may merge them into one translation-policy recommendation.

## Requested Senior Review

Verify the load order, CommonLib pairs, generator behavior, default runtime call graph, and proposed safety rail before critiquing. Prioritize recommendations that prevent another rebuild-time crash. Identify any other default-target relocation or object-layout access reachable from process startup through server admission that this brief missed. Return a concise ranked review, separating must-fix-before-build items from deferred gameplay-mode work.

## Depth Budget And Not-List

- Spend depth on address translation correctness, default-target startup/readiness/connection reachability, pointer validation, and regression checks.
- Do not re-review controller/character-creation automation; it has a completed unattended path documented in `AGENTS.md`.
- Do not design FBT/avatar/gameplay synchronization in this pass.
- Do not change server configuration or deployment.
- Do not recommend enabling unvalidated hooks or the full gameplay target for this test.
