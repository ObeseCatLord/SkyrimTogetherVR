# Windows Gameplay Discovery/Handshake Build - 2026-07-16

## Provenance

- Source commit: `3cf4aa0e86aa8e846f6d291ae7c69c72e9b649e1`
- Network build version: `stvr-v0.1.0-alpha.1-39-g3cf4aa0e`
- Branch: `main`
- Build host: WinBoat Windows VM through the private `winboat-ssh` channel
- Linux entry point: `Tools/SkyrimVR/build_winboat_gameplay.sh 3cf4aa0e`
- Windows entry point: `BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay`
- CommonLibSSE-NG: alandtse `CommonLibVR` `ng` commit
  `d36bc08cae5d445804d4c22ccd49fec4e9dfbdc4`

## Discovery A/B

Commit `5f7943c2` made VR discovery use the player-only branch. The legacy
`ProcessLists` actor-handle scan is not used by the VR avatar architecture and
was the highest-confidence source of the previous post-RaceSex failure. A fresh
automated run then completed RaceSex correctly, entered Realm of Lorkhan,
reached lifecycle `ready=1` with nonzero player/base/cell IDs, and wrote a fresh
discovery snapshot containing exactly the local player. All staged gameplay
observation files were emitted and no current exception or crash dump appeared.

That run also reached the dedicated server. The server logged a connection and
then rejected authentication because the client supplied version `none` while
the server expected `stvr-v0.1.0-alpha.1-37-g6f9cb845`. This proved the prior
failure was no longer in discovery and isolated the next blocker to build
provenance, not packet serialization or a game-address crash.

## Version-Provenance Correction

The original client writes `BUILD_COMMIT` directly into
`AuthenticationRequest`, and the original server intentionally requires exact
equality with its own `BUILD_COMMIT`. Xmake substitutes `${GIT_TAG}` in
`BuildInfo.h`; the detached WinBoat checkout did not have the release tag, so
the generated value became `none`.

Commit `3cf4aa0e` makes the WinBoat helper fetch tags, makes the Windows build
derive `git describe --tags` before configuration, and fails if generated
`BuildInfo.h` does not contain that exact value. The package manifest now
records `buildVersion`; package and build-evidence audits reject missing or
invalid values and reject a launcher that does not contain the manifest value.
The authentication packet test now round-trips an explicit nonempty version.
The server version gate was not weakened.

## Exact Client Build

The corrected Windows gameplay build completed successfully. It regenerated
eight Papyrus scripts, built and ran `TPTests`, built the gameplay client,
launcher, compatibility bridges, `EarlyLoad.dll`, and `TPProcess.exe`, and
created a 503-file package. The package and evidence audits report zero
failures; evidence records five successful commands and no warnings.

- Package:
  `artifacts/SkyrimTogetherVR/packages/SkyrimTogetherVR-gameplay-3cf4aa0e-20260716.zip`
  - size: 263,389,929 bytes
  - SHA-256: `4316e38975ff6c6cb1c6c34bcb301aba3b652400c31a4831eb4361d30469cdf3`
- Build evidence:
  `artifacts/SkyrimTogetherVR/build-evidence/SkyrimTogetherVR-build-evidence-gameplay-20260716-004323Z.zip`
  - size: 164,357 bytes
  - SHA-256: `1ba7490cb875d56f36e5fa377ec5a7739ed2e62f09af3e8012afda569def067c`
- Gameplay launcher SHA-256:
  `f16ebe701608cf19c7b442cd494953a016ac66e2d95a3ac695742e11f7f00b38`
- CommonLib gameplay bridge SHA-256:
  `618e02a631279a5939ec6e490e96659c0ed8303db1d64176559ed7d8dc09c3f1`

The package was deployed to the direct Skyrim VR installation. The installer
updated 34 files, found 469 files identical, and passed strict installed
SKSEVR, VR Address Library, VRIK, HIGGS, PLANCK, and Papyrus hash checks.

## Matched Runtime Acceptance

The exact ARM64 server image completed and replaced only the existing Skyrim
Together container:

- image: `skyrim-together-vr-server:3cf4aa0e`
- image ID:
  `sha256:3f0967165e0fd39f7da68d253eacbc61696bc9bb130bd51e07610d4206856fff`
- runner SHA-256:
  `3a54131f667b8e7a5f9e9b70f716c51944e2b1a218d18d88846fc1c75c329246`
- core SHA-256:
  `06a6fdbfa1d5512862247fc83b04de9f214fbf3cad7fe2c3f4e85d714a6ceb51`

Startup reports the same network version as the client. The container uses host
networking, has zero restarts, and is the only Skyrim Together container.

A fresh automated no-save run completed Main Menu, RaceSex, the controller
confirmation, Realm of Lorkhan, player finalization, ESP validation, and the
Realm intro. The server then accepted the exact-version client and logged a new
player with the expected ten-mod load order. This proves transport reachability,
packet decoding, build-version equality, and server authentication acceptance.

The client exited in the same second, before its local status advanced from
`connecting` to `online`. A second exact reproduction with Wine SEH tracing
captured `c0000005` at Skyrim VR RVA `0x664759`. The packaged PDB resolves the
caller chain to:

```text
TransportService::HandleAuthenticationResponse
  -> ServerSettings dispatcher
  -> PlayerService::OnServerSettingsReceived
  -> PlayerService::ToggleDeathSystem
  -> Actor::SetPlayerRespawnMode
  -> Actor::SetNoBleedoutRecovery
```

The gameplay package sets `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=0` while keeping
`TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE=1`. `PlayerService` incorrectly used
the former as its network-only predicate, so gameplay packages subscribed the
desktop difficulty/death/respawn mutation callbacks even though World described
the service as network-only. The first successful authentication response
therefore called an unvalidated desktop game routine.

The source correction keys the network-only PlayerService path to
`TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE` for every VR package. It also adds
flushed authentication/listener checkpoints and strengthens the service audit
so safety cannot depend on package flavor. The original server version gate,
ServerSettings dispatch, and ConnectedEvent semantics remain intact.

Two-client gameplay/avatar parity remains a later gate even after this
single-client post-authentication crash is repaired and the local client proves
nonzero player ID, current-cell request, and a sustained connection.
