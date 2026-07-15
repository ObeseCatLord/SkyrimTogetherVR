# Skyrim Together VR 0.1.0 Alpha 1

This is the first installable connection-test prerelease of the Skyrim VR port.
It is intended for technical testers on Windows or Linux/Monado, not normal
playthroughs.

## Verified Scope

- Windows-built client package and native test suite: 300 assertions across 22
  test cases passed.
- Skyrim VR 1.4.15 startup through the project launcher.
- Linux launch through Proton GE 10-34 and Monado/XRizer.
- Valid character finalization through XRizer's overlay keyboard path.
- Automatic UDP connection to the shared test server.
- Fresh online state with a nonzero local player ID and current-cell request.
- VRIK, HIGGS, and PLANCK bridge binaries are included; compatibility paths are
  implemented but full multiplayer behavior is not certified by this release.

The packaged binaries were built from `2f006674f47ae1436805a80ea17ed90d12d6cb0f`.
The release tag also contains subsequent documentation, launcher, packaging,
and audit changes. The binary build manifest is included in both archives.

## Known Limits

- This is a connection-only alpha. Remote avatars, complete gameplay state,
  combat, inventory, quest, HIGGS/PLANCK interaction, VRIK pose, and FBT
  replication are not release-qualified.
- A clean normal exit with the newest crash handler still needs fresh runtime
  verification.
- Linux character-creation automation requires the included XRizer and Monado
  patches. Manual SteamVR input does not use that automation path.
- The shared server runs source revision `99d1e1d1`; it is wire-compatible with
  this package because shared protocol/server code did not change.

## Assets

- `*-linux-monado-runtime.zip`: extract into a legal Skyrim VR installation.
- `*-review-handoff.zip`: source snapshot, branch bundle, runtime archive,
  manifests, build evidence, and review/test instructions.
- `SHA256SUMS.txt`: hashes for both archives.

Read `Docs/SkyrimVR/linux-monado-prerelease-guide.md` before installation and
`Docs/SkyrimVR/server-deployment.md` before operating a server. This release
does not include SkyrimVR.exe, SKSEVR, VR Address Library, FUS, VRIK, HIGGS,
PLANCK, or other third-party mod packages.
