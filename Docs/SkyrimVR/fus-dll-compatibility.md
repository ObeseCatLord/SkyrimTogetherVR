# FUS DLL Compatibility

This pass audits the native DLL mods under:

```text
/home/obesecatlord/Games/FUS/mods
```

It also scans the deployed Skyrim VR root when it can infer one, normally:

```text
/home/obesecatlord/Games/FUS/SkyrimVR
```

That second scan catches active root DLL proxies and already-deployed `Data/SKSE/Plugins` DLLs that are not visible from the selected MO2 profile alone.

The selected MO2 profile in `ModOrganizer.ini` is:

```text
FUS RO DAH (Basic + Appearance + Gameplay)
```

`/home/obesecatlord/Games/FUS` is a wrapper folder in the current install. The audit resolves it to the real MO2 root:

```text
/home/obesecatlord/LargeGames/FUS - MO2 2.5.1
```

Run the audit without launching Skyrim:

```sh
Tools/SkyrimVR/audit_fus_dll_compat.py
```

The default FUS root can also be overridden without changing the command line:

```sh
STVR_FUS_ROOT="/path/to/FUS" Tools/SkyrimVR/audit_fus_dll_compat.py
FUS_ROOT="/path/to/FUS" Tools/SkyrimVR/audit_fus_dll_compat.py
```

Useful variants:

```sh
Tools/SkyrimVR/audit_fus_dll_compat.py --profile "FUS (Basic)"
Tools/SkyrimVR/audit_fus_dll_compat.py --all-mods --list
Tools/SkyrimVR/audit_fus_dll_compat.py --installed-root "/path/to/SkyrimVR"
Tools/SkyrimVR/audit_fus_dll_compat.py --skip-installed-root
Tools/SkyrimVR/audit_fus_dll_compat.py --fail-on-warnings
Tools/SkyrimVR/audit_fus_dll_compat.py --json-report artifacts/SkyrimTogetherVR/fus-dll-compatibility.json
```

The normal audit also validates the SkyrimTogetherVR launcher policy that makes the static findings actionable. It checks that `DllBlocklist.cpp` still blocks the known SkyrimTogether-incompatible DLL names, greylists `EngineFixesVR.dll` with the required `MemoryManager = false` and `MaxStdio = 8192` rewrite, detects HIGGS as either `higgs_vr.dll` or `higgs.dll`, detects PLANCK, writes `SkyrimTogetherVR.compatibility`, and keeps the unvalidated gameplay hook batch disabled when HIGGS or PLANCK is installed.

## Current Result Shape

Current result shape:

- selected-profile DLL records
- installed-root DLL records when a deployed SkyrimVR root is found
- combined SKSE plugin DLL count
- combined game-root/proxy DLL count
- selected-profile duplicate deployed-path warnings
- expected VR stack status
- launcher policy failures
- compatibility warnings and failures

The optional JSON report uses schema `skyrim_together_vr_fus_dll_compat_v1` and records every active selected-profile native DLL plus every scanned installed-root DLL with its source, deployed path, category, warnings, failures, duplicate-provider groups, expected VR stack status, installed root path, and launcher-policy validation results. This gives the Windows/VR handoff a stable artifact to compare if the FUS profile or deployed root changes.

The active selected profile enables the important Skyrim VR native stack:

- `VRIK.dll`
- `higgs_vr.dll`
- `activeragdoll.dll`
- `skyrimvrtools.dll`
- `SkyUI-VR.dll`
- `skyrimvresl.dll`
- `EngineFixesVR.dll`

The selected profile does not enable the FUS OpenComposite root `openvr_api.dll`, DLSS/DLAA upscaler root stack, or VR Performance Toolkit `d3d11.dll` root proxy. It does enable the FUS Boot Files root DLLs and one ReShade root `dxgi.dll`.

The active root/proxy DLLs are:

- `dxgi.dll` from `ReShade - Low cost - The Sharper Eye`
- `x3daudio1_7.dll` from `Skyrim SE 3D Sound (X3DAudio HRTF)`
- `d3dx9_42.dll` from `FUS Boot Files`
- `DINPUT8.dll` from `FUS Boot Files`

The audit treats these as hard failures if active:

- `EngineFixes.dll`
- `crashhandler64.dll`
- `fraps64.dll`
- `SpecialK64.dll`
- `ReShade64_SpecialK64.dll`
- `NvCamera64.dll`

`EngineFixesVR.dll` is not the blocked SE `EngineFixes.dll`. The launcher now has a VR-specific greylist entry for `EngineFixesVR.dll` and can prompt to set:

```ini
MemoryManager = false
MaxStdio = 8192
```

in `Data/SKSE/Plugins/EngineFixesVR.ini`.

The active FUS profile currently has `MemoryManager=true`, so this remains a runtime-launcher warning rather than a hard source-port failure.
The audit looks for `EngineFixesVR.ini` next to `EngineFixesVR.dll` first, then falls back to common MO2-relative `SKSE/Plugins` paths.

## Duplicate DLL Paths

FUS intentionally contains several VR/SE pair mods and universal support shims that deploy the same DLL path. The audit reports duplicate deployed paths as warnings, not hard failures, because MO2 priority decides the winner. Review those warnings if a runtime crash points at one of the duplicate DLLs.

Common duplicate examples in the selected profile include:

- `BehaviorDataInjector.dll`
- `BladeAndBlunt.dll`
- `CombatPathingRevolution.dll`
- `EnchantmentEffectExtender.dll`
- `HandToHand.dll`
- `LoadingScreenTruce.dll`
- `po3_PapyrusExtender.dll`
- `po3_Tweaks.dll`
- `Subtitles.dll`

## Boundaries

This is a static compatibility gate. It proves that the active FUS native DLL set does not include known SkyrimTogether hard-blocked DLLs and that required VR physics/IK dependencies are present. It does not prove that every third-party native hook behaves correctly at runtime; that still requires the user's in-game VR smoke test.
