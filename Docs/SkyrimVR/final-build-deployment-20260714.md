# Final Build And Deployment - July 14, 2026

## Revision Contract

The client package and server image were built from commit
`99d1e1d10f85347f0df37d4199ca90c671cb9bcc`. This revision extends the fixed
VR pose wire schema with validated body-pose data, so it must not be paired
with an older client or server.

## Source Verification

- All 27 no-launch Skyrim VR readiness gates passed.
- The installed SkyrimVR-FBT profile audit passed with zero warnings and zero
  failures against the installed VRIK binary.
- Companion self-tests passed.
- `TPTests` built and passed all 136 assertions in 5 test cases on Linux.
- `git diff --check` passed before the implementation commit.

Requested Sol Max and fallback Spark final reviews were attempted, but both
review services exhausted their quotas before returning findings. The review
attempts and the main-agent disposition are recorded in
`fbt-final-integration-senior-disposition-20260714.md`; no unavailable review
is represented as successful.

## Windows Gameplay Build

A clean checkout of the exact revision was built through the WinBoat Windows
VM using:

```bat
BuildSkyrimTogetherVR-Gameplay-Windows.bat -Mode releasedbg -PapyrusCompiler C:\Tools\Caprica\Caprica.exe
```

The actual Caprica path was supplied explicitly by the build environment. The
build produced the gameplay launcher, VRIK/HIGGS/PLANCK/tick bridges,
EarlyLoad, TPProcess, the CEF runtime, and the required compiled Papyrus
scripts. The package manifest reports:

- schema: `skyrim_together_vr_build_package_v2`
- platform/architecture: Windows x64
- mode/flavor: `releasedbg` / `gameplay`
- generated: `2026-07-14T20:15:06.3742385Z`
- source revision: `99d1e1d10f85347f0df37d4199ca90c671cb9bcc`
- source dirty: false
- Papyrus compiled: true

Selected SHA-256 hashes:

| Artifact | SHA-256 |
|---|---|
| `SkyrimTogetherVRGameplay.exe` | `63b1a01e7623d725df0678b0cc705ae274310c413066fd3f01b601f61a015303` |
| `SkyrimTogetherVRHiggsBridge.dll` | `6793bc29f0e1fee1245ac07c4be8b697c58c4e22846b97b8b5a023074b176773` |
| `SkyrimTogetherVRTickBridge.dll` | `a6035152eaf7253b7f3cf62864e3b360bce84c2c56d4e05f1a7f8bca83cdd515` |
| `SkyrimTogetherVRVrikBridge.dll` | `7ef8844783335163b320b1621669293dce8bdca105a354a788eafddf3aae1abb` |
| `SkyrimTogetherVRPlanckBridge.dll` | `ef9f26fb38d70b606e5c59569ac903aab9c3639b433dd25ece89701a9e2de1f9` |

The package-only audit passed. It was installed into the local Skyrim VR
directory, after which the strict installed-prerequisite audit passed with
SKSEVR, VR Address Library, VRIK, HIGGS, PLANCK, and SkyrimVR-FBT present.

## Server Deployment

The remote ARM64 server was built from a clean checkout of the same revision.
The Docker build completed successfully, including `SkyrimTogetherServer`,
`libSTServer.so`, and `TPTests`, and produced:

- image: `skyrim-together-vr-server:99d1e1d1`
- image digest: `sha256:71d77428f3a5c262291e126dfce3c612d88eceef0cb96fbc0c16c3d98f7807b8`
- platform: Linux ARM64

Only the Skyrim Together Compose service was recreated. Post-deployment
verification found exactly one `skyrim-together-vr` container, running the new
image. The server log reports a successful startup on UDP port `26099`, and a
host socket check confirms that port is listening. Existing PM2 and unrelated
services were not touched.

The retained server configuration reports a non-public server and no
`loadorder.txt`; these are configuration warnings, not process startup
failures. The image's display version is `HEAD@none` because the Docker build
context does not include Git metadata. Revision identity is therefore tracked
by the immutable source checkout, image tag, and image digest above.

## Runtime Status

No Skyrim VR process was launched during this final cycle. Build, package,
install, and server startup are verified; in-game connection and avatar
behavior remain runtime acceptance tasks for the user.

For FBT specifically, the current implementation captures, validates, relays,
caches, and reports pelvis/leg state. It intentionally does not apply remote
pelvis/leg transforms until an actor-specific post-animation ownership point
is proven. Runtime evidence must show increasing capture success and local and
remote body sequences before that lane is considered operational.
