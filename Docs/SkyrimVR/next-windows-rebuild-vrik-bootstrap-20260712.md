# Next Windows Rebuild: VRIK Bootstrap

## Why this rebuild is required

The prior default package is built from `6e04eb9e` and stalls while SKSEVR is
loading VRIK. The current source moves SKSEVR bootstrapping onto a dedicated,
TLS-prepared helper thread while retaining the pre-game barrier used by the
official loader. The preexisting package binaries must not be deployed for this
test.

## Build

From an x64 Visual Studio Developer Command Prompt in the repository root:

```bat
BuildAndAuditSkyrimTogetherVR-Windows.bat
```

Then install the resulting `artifacts\SkyrimTogetherVR\packages\default`
snapshot using the existing package installer. Do not install avatar-sync or
gameplay packages for this startup test.

## Expected runtime evidence

`Data\SKSE\Plugins\tp_client.log` must show this order:

1. `SKSEVR bootstrap helper started`
2. `applied mapped Skyrim VR TLS`
3. `entering LoadScriptExender`
4. `returned from LoadScriptExender (result 0)`
5. `bootstrap helper completed before mapped game entry`
6. `entering mapped Skyrim VR gameMain`

`sksevr.log` must show `VRIK.dll ... loaded correctly` after the existing
PLANCK, HIGGS, and SkyrimTogetherVR bridge lines. If it instead stops at
`checking plugin ... VRIK.dll`, retain both logs; do not treat a timeout or a
terminated launch as a successful startup.

The helper logs its TLS template and slot sizes if it rejects the mapping. For
Skyrim VR 1.4.15 the template is 10,772 bytes and the launcher reserve is
28,384 bytes, so a size rejection indicates a build or mapper regression.
