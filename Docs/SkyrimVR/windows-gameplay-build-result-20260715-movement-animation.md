# Windows Movement And Animation Gameplay Build Result - 2026-07-15

## Provenance

- Source commit: `d201a3f87f50bb1b02e20f168763f57999f73237`
- Branch: `main`
- Build host: WinBoat Windows VM through the private `winboat-ssh` channel
- Build entry point: `Tools/SkyrimVR/build_winboat_gameplay.sh`
- Windows wrapper: `BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay`
- Detached build worktree:
  `C:\Users\obesecatlord\Documents\Codex\SkyrimTogetherVR-build-d201a3f8-20260715213813Z`
- CommonLibSSE-NG: alandtse `CommonLibVR` `ng` commit
  `d36bc08cae5d445804d4c22ccd49fec4e9dfbdc4` (`v4.37.0`)

The Linux source revision was clean, pushed to `github/main`, and checked out
into a fresh detached Windows worktree with all pinned submodules initialized.
The build wrapper omitted synchronous WinBoat volume retrimming; stale build
worktree cleanup remains a separate bounded operation.

## Result

The build completed successfully. It compiled the gameplay client, immersive
launcher, EarlyLoad helper, VRIK/HIGGS/PLANCK/tick bridges, CommonLib gameplay
bridge ABI v2, and auxiliary process target. It regenerated all eight Papyrus
scripts, packaged the result, and audited both package and evidence.

- Windows `TPTests`: 571 assertions in 33 test cases passed.
- Build evidence commands: 5 succeeded, 0 failed.
- Evidence audit: 0 warnings, 0 failures.
- Package files recorded by the evidence manifest: 503.
- Build manifest source was clean and exactly
  `d201a3f87f50bb1b02e20f168763f57999f73237`.

An earlier build of the preceding revision exposed one graph-assembly test
failure: a malformed newer snapshot did not retire a complete older pending
snapshot. Commit `44f43533` corrected supersession before metadata validation
and added the zero-snapshot-ID case. The final clean build above includes that
fix and passed the expanded test suite.

## Preserved Artifacts

These machine-local artifacts are intentionally ignored by Git:

- `artifacts/SkyrimTogetherVR/packages/SkyrimTogetherVR-gameplay-d201a3f8-20260715-214829Z.zip`
  - size: 261,448,272 bytes
  - SHA-256: `1d21e7f46dd013b97596f8b995d154c8d6cf8527d2cbfcdc2f69924b553167c3`
- `artifacts/SkyrimTogetherVR/build-evidence/SkyrimTogetherVR-build-evidence-gameplay-20260715-214829Z.zip`
  - size: 163,460 bytes
  - SHA-256: `9e6532d72ff70ec95f20beb4a48a0d9bc60c291389e1689a4cbd8b64db5bb2c3`
- Canonical build-manifest SHA-256:
  `ef5397e06f5e69dae3af55b5209af265e4ba268b762c6d22aaa58facf7bf4f8c`

The package passed its ZIP CRC test. The local artifact-pair validator checked
every package-file hash, clean source provenance, and exact equality between
the package and evidence build manifests. The package was then installed into
the direct Skyrim VR test folder without launching the game; the strict
post-install audit passed with SKSEVR, VR Address Library, VRIK, HIGGS, and
PLANCK present and all packaged Papyrus hashes matching.

## Runtime Status

This build does not prove in-game behavior. Skyrim was not launched during the
build or deployment stage. The next gameplay gate is the Phase 0 plus
movement/graph two-client matrix in `original-gameplay-parity-checklist.md`.
