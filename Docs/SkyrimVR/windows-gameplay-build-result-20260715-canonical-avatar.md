# Windows Canonical Avatar Gameplay Build Result - 2026-07-15

## Provenance

- Source commit: `94544550280598ccd774e15fca3a68450f27e182`
- Branch: `main`
- Build host: WinBoat Windows VM through the private `winboat-ssh` channel
- Build entry point: `Tools/SkyrimVR/build_winboat_gameplay.sh`
- Windows wrapper: `BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay`
- Detached build worktree:
  `C:\Users\obesecatlord\Documents\Codex\SkyrimTogetherVR-build-94544550-20260715194842Z`
- CommonLibSSE-NG: alandtse `CommonLibVR` `ng` commit
  `d36bc08cae5d445804d4c22ccd49fec4e9dfbdc4` (`v4.37.0`)

The Linux source revision was clean and reachable from `github/main`. The
helper created a fresh detached Windows worktree and initialized the pinned
submodules before running the audited build.

## Result

The build completed successfully. It compiled the gameplay client, immersive
launcher, EarlyLoad helper, VRIK/HIGGS/PLANCK/tick bridges, CommonLib gameplay
bridge, and auxiliary process target. It also regenerated Papyrus output,
packaged the result, and audited the package and build evidence.

- Windows `TPTests`: 526 assertions in 30 test cases passed.
- Build evidence commands: 5 succeeded, 0 failed.
- Evidence audit: 0 warnings, 0 failures.
- Package files recorded by the evidence manifest: 503.
- Build manifest source was clean and exactly
  `94544550280598ccd774e15fca3a68450f27e182`.

## Preserved Artifacts

These local artifacts are intentionally ignored by Git:

- `artifacts/SkyrimTogetherVR/packages/SkyrimTogetherVR-gameplay-94544550-20260715-200253Z.zip`
  - size: 262,867,939 bytes
  - SHA-256: `d18dba0a3a3f6353dd0881d4a70c3338a7ce9991728c11ae73adaa7afbb4cb06`
- `artifacts/SkyrimTogetherVR/build-evidence/SkyrimTogetherVR-build-evidence-gameplay-20260715-200253Z.zip`
  - size: 163,471 bytes
  - SHA-256: `50500f7dd919340e73b3ca5535d43b45c9cb29594d7691e83ba18b13afbeab7e`
- Canonical build-manifest SHA-256:
  `265e4e36dc477f83f6ee58667a39557cda66b02063e3b76635c99bb681f11fe7`

The package passed `unzip -t`. The build-evidence auditor passed with
`--require-package --require-gameplay`. The local handoff artifact validator
then checked both ZIP CRCs, every package-file hash, clean source provenance,
and exact equality between the package and evidence build manifests.

## Runtime Status

This build does not prove in-game behavior. Skyrim was not launched in this
stage. The next gate is the Phase 0 two-client runtime matrix in
`original-gameplay-parity-checklist.md`: unique spawn, movement, stop,
visibility leave/re-entry, despawn, reconnect/load/cell transitions, and safe
temporary-reference cleanup. No later mutation domain should be promoted until
that runtime gate passes.
