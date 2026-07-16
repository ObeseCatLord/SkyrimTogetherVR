# Windows Gameplay Runtime-Gate Build - 2026-07-15

## Provenance

- Source commit: `6f9cb845b07587583b62d4e2c70998f9820ab52c`
- Branch: `main`
- Build host: WinBoat Windows VM through the private `winboat-ssh` channel
- Linux entry point: `Tools/SkyrimVR/build_winboat_gameplay.sh 6f9cb845`
- Windows entry point: `BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay`
- CommonLibSSE-NG: alandtse `CommonLibVR` `ng` commit
  `d36bc08cae5d445804d4c22ccd49fec4e9dfbdc4`
- CommonLib nested OpenVR: `60eb187801956ad277f1cae6680e3a410ee0873b`
- TiltedUI: `3335041dc95b6776b13c2f4b86b6eb2cd823e32d`

The build used a fresh detached Windows worktree. The package manifest records
the source tree as clean and binds all artifacts to the full source revision.

## Result

The gameplay build wrapper completed successfully. It regenerated eight
Papyrus scripts, ran the project test target, built the gameplay client and
launcher, built all VR compatibility bridges including the CommonLib gameplay
bridge, built `EarlyLoad.dll` and `TPProcess.exe`, and produced a 503-file
gameplay package.

The Windows package audit had zero failures. The independently copied build
evidence archive reports five successful evidence commands, zero failed
commands, zero warnings, and zero audit failures. The copied package passed a
Linux ZIP integrity check and an independent Linux package audit with zero
failures.

## Preserved Artifacts

- `artifacts/SkyrimTogetherVR/packages/SkyrimTogetherVR-gameplay-6f9cb845-20260715.zip`
  - size: 263,069,373 bytes
  - SHA-256: `3e950b4832ca55515a9e1becbb84032b3ebea81922cc1ad583c889db62f641df`
- `artifacts/SkyrimTogetherVR/build-evidence/SkyrimTogetherVR-build-evidence-gameplay-20260715-231225Z.zip`
  - size: 163,533 bytes
  - SHA-256: `30a7c3b21dcd3de00629688af443b93480e17a3f75865256ff15e71bfdf0c7f1`

The unpacked exact package is retained at
`artifacts/SkyrimTogetherVR/packages/gameplay-6f9cb845`.

## Local Deployment

The package was installed into the direct Skyrim VR test installation. The
installer updated 34 files and found 469 files already identical. Its strict
post-install audit found SKSEVR, the VR Address Library, VRIK, HIGGS, and PLANCK
and reported zero package, prerequisite, or installed-Papyrus failures.

Installed artifact hashes include:

- `Data/SKSE/Plugins/SkyrimTogetherVRGameplayBridge.dll`:
  `3781bd431dd23e48f9c495d270170a4e802e03cec050c977c4d320ce093c9019`
- `SkyrimTogetherVRGameplay.exe`:
  `3596f326a0cc63fcfbe747fda8e479e2e1dbe3e410acd58a625a8788679450b2`

## Runtime Acceptance

The runtime-gate correction is accepted. In a fresh Skyrim VR process,
`sksevr.log` reported `SkyrimTogetherVRGameplayBridge.dll` loaded correctly and
the bridge log recorded loader runtime `0x010400F1`, SKSE version `0x020000C0`,
and release index 60 before reporting the bridge loaded. The mapped client
completed startup, detected HIGGS and PLANCK, published the tick endpoint, and
reached recurring owner-thread `Main::Draw` dispatch. The former `CommonLib
gameplay endpoint failed its owner-thread bootstrap` failure did not recur.

End-to-end connection acceptance remains open. Two fresh simulated-runtime
runs reached Main Menu, New Game, RaceSex, and the valid controller-driven
finish/name transaction. The process then exited with launcher status 5 before
DevBench could observe the confirmation close and issue the connection command.
The runs produced no new crash dump or current exception-handler entry; one run
briefly reached a ready Realm lifecycle and executed Realm/Helgen quest scripts
before the process disappeared. This is recorded as an unresolved
post-RaceSex/runtime teardown failure, not a successful connection and not a
regression attributed to the corrected loader gate without further evidence.

Connection acceptance still requires a stable ready lifecycle, nonzero online
player ID, a valid current-cell request from the same session and connection
generation, matching admission in the single test-server container, and a
clean controlled shutdown.

Do not treat this successful build and install as proof of gameplay parity. The
remaining ordered work and evidence gates are maintained in
`original-gameplay-parity-checklist.md`.
