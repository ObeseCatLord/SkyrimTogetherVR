# Windows Gameplay Build Result - 2026-07-15

## Provenance

- Source commit: `dda9cad855ad2784f70dc3d8c2fd5c48172fab0f`
- Branch: `main`
- Build host: WinBoat Windows VM through the private `winboat-ssh` channel
- Build entry point: `Tools/SkyrimVR/build_winboat_gameplay.sh`
- Windows wrapper: `BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay`
- CommonLibSSE-NG: alandtse `CommonLibVR` `ng` commit
  `d36bc08cae5d445804d4c22ccd49fec4e9dfbdc4` (`v4.37.0`)
- CommonLib nested OpenVR: `60eb187801956ad277f1cae6680e3a410ee0873b`
- TiltedUI: `3335041dc95b6776b13c2f4b86b6eb2cd823e32d`

The Linux source worktree was clean, and both `github/main` and `origin/main`
resolved to the source commit before the detached Windows build was created.

## Result

The clean gameplay build completed successfully. It regenerated eight Papyrus
scripts, built and ran `TPTests`, built the gameplay client and launcher, built
all VR compatibility bridges, built the CommonLib SKSEVR gameplay adapter, and
built the auxiliary process target.

- Tests: 491 assertions in 26 test cases passed.
- Build evidence commands: 5 succeeded, 0 failed.
- Evidence audit: 0 warnings, 0 failures.
- Package audit: 0 failures.
- Package files recorded by the evidence manifest: 503.

Required gameplay artifacts were produced:

- `SkyrimTogetherVRGameplay.exe`
- `SkyrimTogetherVRGameplayBridge.dll`
- `SkyrimTogetherVRVrikBridge.dll`
- `SkyrimTogetherVRHiggsBridge.dll`
- `SkyrimTogetherVRPlanckBridge.dll`
- `SkyrimTogetherVRTickBridge.dll`
- `EarlyLoad.dll`
- `TPProcess.exe`

This is the first clean build after switching the gameplay adapter to the
maintained alandtse CommonLib branch. CommonLib compiled from source and the
adapter linked against it successfully.

## Preserved Artifacts

The generated artifacts are intentionally ignored by Git. The local retained
copies are:

- `artifacts/SkyrimTogetherVR/packages/SkyrimTogetherVR-gameplay-dda9cad8-20260715.zip`
  - unpacked payload: 917,692,560 bytes
  - compressed size: 262,749,055 bytes
  - SHA-256: `21eeac8284573a0753165917ce19a00bb96bf4f3885936016b2676ebaec7b260`
- `artifacts/SkyrimTogetherVR/build-evidence/SkyrimTogetherVR-build-evidence-gameplay-20260715-190334Z.zip`
  - SHA-256: `706bb9ca518533f61ea4b9c17fda68e43dff122dc37f0925f6477573bb0548ec`

The package ZIP passed `unzip -t`. The copied evidence archive independently
passed:

```bash
python3 Tools/SkyrimVR/audit_build_evidence_zip.py \
  artifacts/SkyrimTogetherVR/build-evidence/SkyrimTogetherVR-build-evidence-gameplay-20260715-190334Z.zip \
  --require-package --require-gameplay
```

## Remaining Verification

This build does not prove in-game behavior. Skyrim was not launched by the
build or by this verification pass. Before a gameplay test, unpack the package,
run the strict target-prerequisite/readiness audit against the intended Skyrim
VR 1.4.15 installation, perform the install dry run, and then install. The
package audit correctly reported that SKSEVR, the VR Address Library, VRIK,
HIGGS, and PLANCK were not present inside the standalone package itself; those
are target-install prerequisites rather than bundled outputs.

Runtime acceptance still requires the current-process lifecycle, server
admission, avatar, gameplay-relay, HIGGS, PLANCK, save/load, and clean-shutdown
evidence described in `Docs/SkyrimVR/final-handoff-checklist.md`. A successful
compile must not be reported as full gameplay parity.

## Storage Hygiene

`skyrim-together-vr-build-cleanup.timer` is enabled as a persistent user timer
and runs daily. It retains recent artifacts for two days and removes only
the generated Skyrim Together build paths defined by the checked-in cleanup
script. The WinBoat build helper also removes previous generated detached
worktrees before starting a new build. After preserving the package and build
evidence from this run, its generated detached Windows worktree can be removed
with:

```bash
Tools/SkyrimVR/cleanup_build_storage.sh --max-age-days 0 --skip-local-artifacts
```
