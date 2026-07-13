# Papyrus Rebuild Notes

The VR package includes generated PEX bytecode for the VR-specific script edits:

- `scripts/SkyrimTogetherVerifyLaunchScript.pex`
- `scripts/SkyrimTogetherPlayerAliasScript.pex`
- `scripts/SkyrimTogetherVRTickBridge.pex`
- `scripts/SkyrimTogetherUtils.pex`
- `scripts/SkyrimTogetherVRConnectionMenu.pex`
- `scripts/SkyrimTogetherVRConnectionSpellEffect.pex`

Run `Tools/SkyrimVR/audit_gamefiles.py` after rebuilding to confirm stale launch strings and missing native declarations stay at zero.

The default packaged build now recompiles these PEX files automatically. `SkyrimTogetherVRTickBridge.pex` is required for the supported task-backed connection scheduler. The older `SkyrimTogetherUtils` and connection-menu scripts remain staged future UI source; the default VR package does not register their flat-client natives.

The Windows package build can do this as part of the final handoff:

```bat
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites -- -PapyrusCompiler "C:\Tools\Caprica\Caprica.exe"
```

If `CAPRICA` points at `Caprica.exe`, no additional flag is needed. The package manifest records whether Papyrus was regenerated, and `Tools/SkyrimVR/audit_built_package.py` rejects a package missing the tick bridge PEX or required staged source bytecode.

## Local Toolchain

The checked Skyrim VR and Skyrim SE installs do not include a Skyrim Papyrus compiler or source/header tree.

For this workspace, the PEX files were generated with the prebuilt Caprica v0.3.0 executable:

```sh
Tools/SkyrimVR/compile_papyrus.py --caprica /home/obesecatlord/Documents/SkyrimModding/_refs/Caprica-release/extracted/Caprica.exe
```

The wrapper uses:

- `Tools/SkyrimVR/TESV_Papyrus_Flags.flg`
- `Tools/SkyrimVR/PapyrusImports/*.psc` compile-only stubs
- a temporary source directory containing only the VR scripts that need rebuilding

The compile-only import stubs are not package files and should not be copied into `GameFiles/SkyrimVR/scripts`.

## Source Notes

Caprica requires scripts containing native function declarations to be marked `Native`. Only the scripts that actually declare native functions use that marker:

- `SkyrimTogetherUtils.psc`: `Scriptname SkyrimTogetherUtils Native Hidden`
- `SkyrimTogetherVRTickBridge.psc`: `ScriptName SkyrimTogetherVRTickBridge Native Hidden`

`SkyrimTogetherVerifyLaunchScript.psc` deliberately remains a normal quest
script so its `OnInit` and `OnUpdate` timer lifecycle is represented as regular
Papyrus bytecode.

`SkyrimTogetherVRConnectionMenu.psc` avoids `state` as a local variable name because Caprica treats it as a reserved Papyrus keyword.

The compile-only `Quest`, `ReferenceAlias`, and `ActiveMagicEffect` stubs include the inherited timer/event signatures needed for the VR tick timer, player-load rearm path, and spell-effect entry point.

## Official Compiler Option

The Skyrim Special Edition Creation Kit compiler should also work once installed. The expected command shape is:

```cmd
PapyrusCompiler.exe SkyrimTogetherVerifyLaunchScript.psc -f=TESV_Papyrus_Flags.flg -i="<repo>\GameFiles\SkyrimVR\scripts\source;<SkyrimSE>\Data\Scripts\Source" -o="<repo>\GameFiles\SkyrimVR\scripts"
PapyrusCompiler.exe SkyrimTogetherPlayerAliasScript.psc -f=TESV_Papyrus_Flags.flg -i="<repo>\GameFiles\SkyrimVR\scripts\source;<SkyrimSE>\Data\Scripts\Source" -o="<repo>\GameFiles\SkyrimVR\scripts"
PapyrusCompiler.exe SkyrimTogetherVRTickBridge.psc -f=TESV_Papyrus_Flags.flg -i="<repo>\GameFiles\SkyrimVR\scripts\source;<SkyrimSE>\Data\Scripts\Source" -o="<repo>\GameFiles\SkyrimVR\scripts"
PapyrusCompiler.exe SkyrimTogetherUtils.psc -f=TESV_Papyrus_Flags.flg -i="<repo>\GameFiles\SkyrimVR\scripts\source;<SkyrimSE>\Data\Scripts\Source" -o="<repo>\GameFiles\SkyrimVR\scripts"
PapyrusCompiler.exe SkyrimTogetherVRConnectionMenu.psc -f=TESV_Papyrus_Flags.flg -i="<repo>\GameFiles\SkyrimVR\scripts\source;<SkyrimSE>\Data\Scripts\Source" -o="<repo>\GameFiles\SkyrimVR\scripts"
PapyrusCompiler.exe SkyrimTogetherVRConnectionSpellEffect.psc -f=TESV_Papyrus_Flags.flg -i="<repo>\GameFiles\SkyrimVR\scripts\source;<SkyrimSE>\Data\Scripts\Source" -o="<repo>\GameFiles\SkyrimVR\scripts"
```

Depending on the Creation Kit install, the Skyrim header path may be `Data\Scripts\Source`, `Data\Source\Scripts`, or an extracted `Scripts.zip` directory.
