# Windows Gameplay Build Result - July 12, 2026

## Package

The Windows/MSVC gameplay package was rebuilt successfully and staged at:

```text
artifacts\SkyrimTogetherVR\packages\gameplay
```

Its build manifest reports:

- schema: `skyrim_together_vr_build_package_v2`
- mode: `releasedbg`
- flavor: `gameplay`
- generated: `2026-07-12T21:19:06.3675620Z`
- source revision: `6eb55313689fdbfa742a65e81e2d99ae02aa5b58`
- source state: intentionally dirty and approved, with tree fingerprint
  `4d0ce92b78057536dc06fe92a485bc40b9f51a47b9316f004560431c8d583350`
- staged payload entries: 487, excluding the self-referential manifest

## Verification And Deployment

`Tools/SkyrimVR/audit_built_package.py --gameplay` passed with strict SKSEVR,
VR Address Library, VRIK, HIGGS, and PLANCK checks against the local Skyrim VR
installation.

The audited package was installed into the local Skyrim VR folder. The install
copied `SkyrimTogetherVRGameplay.exe`, updated six support files, and found 481
files already byte-identical. No game launch is implied by this result.

The Linux launch wrapper now reads `packageFlavor` from the installed build
manifest and chooses `SkyrimTogetherVRGameplay.exe` for this package. An
explicit `STVR_LAUNCHER` environment variable still overrides that selection.

## Packaging Follow-Up

The Windows rebuild included a snapshot-copy reliability improvement in
`BuildSkyrimTogetherVR-Windows.ps1`: it uses `robocopy` and accepts only the
documented successful exit codes 0 through 7. This prevents the earlier
`Copy-Item` CEF locale-directory snapshot failure from being reported as a
successful package handoff.
