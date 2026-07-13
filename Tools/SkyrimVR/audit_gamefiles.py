#!/usr/bin/env python3
import argparse
import hashlib
import pathlib
import re
from collections import Counter

import vr_paths

EXPECTED_BEHAVIOR_SUFFIXES = (
    "__sig.txt",
    "__hash.txt",
    "__float.txt",
    "__int.txt",
    "__bool.txt",
)

STALE_PEX_TOKENS = (
    b"Skyrim Together is not running!",
    b"SkyrimTogether.exe",
    b"Skyrim Special Edition",
)

REQUIRED_VERIFY_SOURCE_TOKENS = (
    "StartTickBridge",
    "RegisterForSingleUpdate(0.05)",
    "Event OnUpdate()",
    "SkyrimTogetherVRTickBridge.ArmOnInit()",
    "SkyrimTogetherVRTickBridge.Tick()",
)

FORBIDDEN_VERIFY_SOURCE_TOKENS = (
    "DidLaunchSkyrimTogether",
    "ScriptName SkyrimTogetherVerifyLaunchScript extends Quest Native",
    "Skyrim Together VR is not running!",
)

REQUIRED_TICK_BRIDGE_SOURCE_TOKENS = (
    "ScriptName SkyrimTogetherVRTickBridge Native Hidden",
    "Bool Function Tick() Global Native",
    "Bool Function ArmOnInit() Global Native",
    "Bool Function ArmOnPlayerLoadGame() Global Native",
)

REQUIRED_QUEST_IMPORT_TOKENS = (
    "Scriptname Quest Native Hidden",
    "Function RegisterForSingleUpdate(float afInterval) Native",
)

REQUIRED_TICK_BRIDGE_PEX_TOKENS = (
    "SkyrimTogetherVRTickBridge",
    "Tick",
    "ArmOnInit",
    "ArmOnPlayerLoadGame",
)

REQUIRED_UTILS_NATIVE_TOKENS = (
    "ConnectToSkyrimTogether",
    "DisconnectFromSkyrimTogether",
    "IsSkyrimTogetherConnected",
    "SetSkyrimTogetherConnectionConfig",
    "GetSkyrimTogetherConnectionState",
    "GetSkyrimTogetherConfiguredEndpoint",
    "GetSkyrimTogetherConfiguredPassword",
    "GetSkyrimTogetherStatusSummary",
    "GetSkyrimTogetherTelemetryReadout",
)

REQUIRED_VR_MENU_SOURCE_TOKENS = (
    "Scriptname SkyrimTogetherVRConnectionMenu",
    "ShowStatus",
    "GetSkyrimTogetherStatusSummary",
    "GetSkyrimTogetherTelemetryReadout",
    "SetSkyrimTogetherConnectionConfig",
    "Configure",
    "ConfigureAndConnect",
    "ConnectLocalhost",
    "ConnectConfigured",
    "Disconnect",
    "ToggleLocalhost",
    "ToggleConfigured",
    "ShowTelemetry",
    "ShowStatusAndTelemetry",
    "GetSkyrimTogetherConfiguredEndpoint",
    "Debug.MessageBox",
)

REQUIRED_PLAYER_ALIAS_SOURCE_TOKENS = (
    "OnPlayerLoadGame",
    "ArmOnPlayerLoadGame",
    "VerifyLaunch",
)

REQUIRED_PLAYER_ALIAS_PEX_TOKENS = REQUIRED_PLAYER_ALIAS_SOURCE_TOKENS

REQUIRED_VR_MENU_PEX_TOKENS = (
    "SkyrimTogetherVRConnectionMenu",
    "ConnectToSkyrimTogether",
    "DisconnectFromSkyrimTogether",
    "SetSkyrimTogetherConnectionConfig",
    "GetSkyrimTogetherConnectionState",
    "GetSkyrimTogetherStatusSummary",
    "GetSkyrimTogetherTelemetryReadout",
    "GetSkyrimTogetherConfiguredEndpoint",
    "GetSkyrimTogetherConfiguredPassword",
    "Configure",
    "ConfigureAndConnect",
    "ConnectConfigured",
    "ToggleLocalhost",
    "ToggleConfigured",
    "ShowTelemetry",
)

REQUIRED_VR_EFFECT_SOURCE_TOKENS = (
    "Scriptname SkyrimTogetherVRConnectionSpellEffect",
    "ActiveMagicEffect",
    "OnEffectStart",
    "ToggleConfigured",
)

REQUIRED_VR_EFFECT_PEX_TOKENS = (
    "SkyrimTogetherVRConnectionSpellEffect",
    "OnEffectStart",
    "ToggleConfigured",
)

REQUIRED_VR_MENU_ESP_TOKENS = (
    b"SkyrimTogetherVRConnectionSpellEffect",
    b"Skyrim Together VR",
    b"Connect to or disconnect from the local Skyrim Together server.",
    b"Opens the Skyrim Together VR connection menu.",
)


def read_u16(data, offset):
    return int.from_bytes(data[offset : offset + 2], "little")


def read_u32(data, offset):
    return int.from_bytes(data[offset : offset + 4], "little")


def parse_tes4_masters(path):
    data = path.read_bytes()
    if len(data) < 24 or data[:4] != b"TES4":
        raise ValueError(f"{path} is not a TES4 plugin")

    record_size = read_u32(data, 4)
    payload = data[24 : 24 + record_size]
    masters = []
    cursor = 0
    extended_size = None

    while cursor + 6 <= len(payload):
        subrecord_type = payload[cursor : cursor + 4]
        subrecord_size = read_u16(payload, cursor + 4)
        cursor += 6

        if subrecord_type == b"XXXX" and subrecord_size == 4 and cursor + 4 <= len(payload):
            extended_size = read_u32(payload, cursor)
            cursor += 4
            continue

        data_size = extended_size if extended_size is not None else subrecord_size
        extended_size = None
        subrecord = payload[cursor : cursor + data_size]
        cursor += data_size

        if subrecord_type == b"MAST":
            masters.append(subrecord.split(b"\x00", 1)[0].decode("utf-8", errors="replace"))

    return masters


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def relative_files(root):
    if not root.exists():
        return []
    return sorted(path.relative_to(root) for path in root.rglob("*") if path.is_file())


def extract_ascii_strings(path):
    data = path.read_bytes()
    return [match.group(0).decode("ascii", errors="replace") for match in re.finditer(rb"[\x20-\x7e]{4,}", data)]


def audit_plugin(package, skyrim_vr):
    plugin = package / "SkyrimTogether.esp"
    masters = parse_tes4_masters(plugin) if plugin.exists() else []
    vr_data = skyrim_vr / "Data"
    checked_masters = vr_data.exists()
    missing = [master for master in masters if not (vr_data / master).exists()] if checked_masters else []
    stale_tokens = []
    missing_vr_menu_tokens = []
    if plugin.exists():
        data = plugin.read_bytes()
        stale_tokens = [token.decode("ascii") for token in STALE_PEX_TOKENS if token in data]
        missing_vr_menu_tokens = [
            token.decode("ascii") for token in REQUIRED_VR_MENU_ESP_TOKENS if token not in data
        ]
    else:
        missing_vr_menu_tokens = [token.decode("ascii") for token in REQUIRED_VR_MENU_ESP_TOKENS]

    return {
        "path": plugin,
        "exists": plugin.exists(),
        "masters": masters,
        "checked_masters": checked_masters,
        "vr_data": vr_data,
        "missing_masters": missing,
        "stale_tokens": stale_tokens,
        "missing_vr_menu_tokens": missing_vr_menu_tokens,
    }


def audit_papyrus(package):
    scripts = package / "scripts"
    source_dir = scripts / "source"
    source_files = sorted(source_dir.glob("*.psc")) if source_dir.exists() else []
    pex_files = sorted(scripts.glob("*.pex")) if scripts.exists() else []

    source_by_lower = {path.stem.lower(): path for path in source_files}
    pex_by_lower = {path.stem.lower(): path for path in pex_files}
    source_keys = set(source_by_lower)
    pex_keys = set(pex_by_lower)

    exact_case_mismatches = []
    for key in sorted(source_keys & pex_keys):
        if source_by_lower[key].stem != pex_by_lower[key].stem:
            exact_case_mismatches.append((source_by_lower[key], pex_by_lower[key]))

    stale_pex = []
    for path in pex_files:
        data = path.read_bytes()
        hits = [token.decode("ascii") for token in STALE_PEX_TOKENS if token in data]
        if hits:
            stale_pex.append((path, hits, extract_ascii_strings(path)))

    verify_source = source_dir / "SkyrimTogetherVerifyLaunchScript.psc"
    missing_verify_tokens = []
    if verify_source.exists():
        text = verify_source.read_text(encoding="utf-8", errors="replace")
        missing_verify_tokens = [token for token in REQUIRED_VERIFY_SOURCE_TOKENS if token not in text]
        missing_verify_tokens.extend(f"forbidden:{token}" for token in FORBIDDEN_VERIFY_SOURCE_TOKENS if token in text)
    else:
        missing_verify_tokens = list(REQUIRED_VERIFY_SOURCE_TOKENS)

    tick_bridge_source = source_dir / "SkyrimTogetherVRTickBridge.psc"
    missing_tick_bridge_source_tokens = []
    if tick_bridge_source.exists():
        text = tick_bridge_source.read_text(encoding="utf-8", errors="replace")
        missing_tick_bridge_source_tokens = [token for token in REQUIRED_TICK_BRIDGE_SOURCE_TOKENS if token not in text]
    else:
        missing_tick_bridge_source_tokens = list(REQUIRED_TICK_BRIDGE_SOURCE_TOKENS)

    tick_bridge_pex = scripts / "SkyrimTogetherVRTickBridge.pex"
    missing_tick_bridge_pex_tokens = []
    if tick_bridge_pex.exists():
        data = tick_bridge_pex.read_bytes()
        missing_tick_bridge_pex_tokens = [token for token in REQUIRED_TICK_BRIDGE_PEX_TOKENS if token.encode("ascii") not in data]
    else:
        missing_tick_bridge_pex_tokens = list(REQUIRED_TICK_BRIDGE_PEX_TOKENS)

    quest_import = root / "Tools" / "SkyrimVR" / "PapyrusImports" / "Quest.psc"
    missing_quest_import_tokens = []
    if quest_import.exists():
        text = quest_import.read_text(encoding="utf-8", errors="replace")
        missing_quest_import_tokens = [token for token in REQUIRED_QUEST_IMPORT_TOKENS if token not in text]
    else:
        missing_quest_import_tokens = list(REQUIRED_QUEST_IMPORT_TOKENS)

    utils_source = source_dir / "SkyrimTogetherUtils.psc"
    missing_utils_source_tokens = []
    if utils_source.exists():
        text = utils_source.read_text(encoding="utf-8", errors="replace")
        missing_utils_source_tokens = [token for token in REQUIRED_UTILS_NATIVE_TOKENS if token not in text]
    else:
        missing_utils_source_tokens = list(REQUIRED_UTILS_NATIVE_TOKENS)

    utils_pex = scripts / "SkyrimTogetherUtils.pex"
    missing_utils_pex_tokens = []
    if utils_pex.exists():
        data = utils_pex.read_bytes()
        missing_utils_pex_tokens = [token for token in REQUIRED_UTILS_NATIVE_TOKENS if token.encode("ascii") not in data]
    else:
        missing_utils_pex_tokens = list(REQUIRED_UTILS_NATIVE_TOKENS)

    vr_menu_source = source_dir / "SkyrimTogetherVRConnectionMenu.psc"
    missing_vr_menu_source_tokens = []
    if vr_menu_source.exists():
        text = vr_menu_source.read_text(encoding="utf-8", errors="replace")
        missing_vr_menu_source_tokens = [token for token in REQUIRED_VR_MENU_SOURCE_TOKENS if token not in text]
    else:
        missing_vr_menu_source_tokens = list(REQUIRED_VR_MENU_SOURCE_TOKENS)

    vr_menu_pex = scripts / "SkyrimTogetherVRConnectionMenu.pex"
    missing_vr_menu_pex_tokens = []
    if vr_menu_pex.exists():
        data = vr_menu_pex.read_bytes()
        missing_vr_menu_pex_tokens = [token for token in REQUIRED_VR_MENU_PEX_TOKENS if token.encode("ascii") not in data]
    else:
        missing_vr_menu_pex_tokens = list(REQUIRED_VR_MENU_PEX_TOKENS)

    player_alias_source = source_dir / "SkyrimTogetherPlayerAliasScript.psc"
    missing_player_alias_source_tokens = []
    if player_alias_source.exists():
        text = player_alias_source.read_text(encoding="utf-8", errors="replace")
        missing_player_alias_source_tokens = [
            token for token in REQUIRED_PLAYER_ALIAS_SOURCE_TOKENS if token not in text
        ]
    else:
        missing_player_alias_source_tokens = list(REQUIRED_PLAYER_ALIAS_SOURCE_TOKENS)

    player_alias_pex = scripts / "SkyrimTogetherPlayerAliasScript.pex"
    missing_player_alias_pex_tokens = []
    if player_alias_pex.exists():
        data = player_alias_pex.read_bytes()
        missing_player_alias_pex_tokens = [
            token for token in REQUIRED_PLAYER_ALIAS_PEX_TOKENS if token.encode("ascii") not in data
        ]
    else:
        missing_player_alias_pex_tokens = list(REQUIRED_PLAYER_ALIAS_PEX_TOKENS)

    vr_effect_source = source_dir / "SkyrimTogetherVRConnectionSpellEffect.psc"
    missing_vr_effect_source_tokens = []
    if vr_effect_source.exists():
        text = vr_effect_source.read_text(encoding="utf-8", errors="replace")
        missing_vr_effect_source_tokens = [token for token in REQUIRED_VR_EFFECT_SOURCE_TOKENS if token not in text]
    else:
        missing_vr_effect_source_tokens = list(REQUIRED_VR_EFFECT_SOURCE_TOKENS)

    vr_effect_pex = scripts / "SkyrimTogetherVRConnectionSpellEffect.pex"
    missing_vr_effect_pex_tokens = []
    if vr_effect_pex.exists():
        data = vr_effect_pex.read_bytes()
        missing_vr_effect_pex_tokens = [
            token for token in REQUIRED_VR_EFFECT_PEX_TOKENS if token.encode("ascii") not in data
        ]
    else:
        missing_vr_effect_pex_tokens = list(REQUIRED_VR_EFFECT_PEX_TOKENS)

    return {
        "source_files": source_files,
        "pex_files": pex_files,
        "missing_pex": [source_by_lower[key] for key in sorted(source_keys - pex_keys)],
        "missing_source": [pex_by_lower[key] for key in sorted(pex_keys - source_keys)],
        "exact_case_mismatches": exact_case_mismatches,
        "stale_pex": stale_pex,
        "missing_verify_tokens": missing_verify_tokens,
        "missing_tick_bridge_source_tokens": missing_tick_bridge_source_tokens,
        "missing_tick_bridge_pex_tokens": missing_tick_bridge_pex_tokens,
        "missing_quest_import_tokens": missing_quest_import_tokens,
        "missing_utils_source_tokens": missing_utils_source_tokens,
        "missing_utils_pex_tokens": missing_utils_pex_tokens,
        "missing_vr_menu_source_tokens": missing_vr_menu_source_tokens,
        "missing_vr_menu_pex_tokens": missing_vr_menu_pex_tokens,
        "missing_player_alias_source_tokens": missing_player_alias_source_tokens,
        "missing_player_alias_pex_tokens": missing_player_alias_pex_tokens,
        "missing_vr_effect_source_tokens": missing_vr_effect_source_tokens,
        "missing_vr_effect_pex_tokens": missing_vr_effect_pex_tokens,
    }


def audit_behaviors(repo, package, compare_se=False):
    se_root = repo / "GameFiles" / "Skyrim" / "SkyrimTogetherRebornBehaviors"
    vr_root = package / "SkyrimTogetherRebornBehaviors"
    vr_files = relative_files(vr_root)
    se_files = relative_files(se_root) if compare_se else []

    suffix_counts = Counter()
    suffix_case_issues = []
    unrecognized = []
    missing_required_by_dir = []

    for rel_path in vr_files:
        name = rel_path.name
        matched = False
        for suffix in EXPECTED_BEHAVIOR_SUFFIXES:
            if name.endswith(suffix):
                suffix_counts[suffix] += 1
                matched = True
                break
            if name.lower().endswith(suffix):
                suffix_counts[suffix] += 1
                suffix_case_issues.append(rel_path)
                matched = True
                break
        if "__" in name and not matched:
            unrecognized.append(rel_path)

    for directory in sorted({path.parent for path in vr_files}):
        names = {path.name for path in vr_files if path.parent == directory}
        missing = []
        if not any(name.endswith("__sig.txt") for name in names):
            missing.append("__sig.txt")
        if not any(name.endswith("__hash.txt") for name in names):
            missing.append("__hash.txt")
        if missing:
            missing_required_by_dir.append((directory, missing))

    se_set = {str(path) for path in se_files} if compare_se else set()
    vr_set = {str(path) for path in vr_files}
    se_lower_set = {str(path).lower() for path in se_files} if compare_se else set()
    vr_lower_set = {str(path).lower() for path in vr_files}

    common_exact = sorted(se_set & vr_set) if compare_se else []
    content_mismatches = []
    for rel_text in common_exact:
        rel_path = pathlib.Path(rel_text)
        if sha256(se_root / rel_path) != sha256(vr_root / rel_path):
            content_mismatches.append(rel_path)

    return {
        "se_root": se_root,
        "vr_root": vr_root,
        "compare_se": compare_se,
        "se_files": se_files,
        "vr_files": vr_files,
        "suffix_counts": suffix_counts,
        "suffix_case_issues": suffix_case_issues,
        "unrecognized": unrecognized,
        "missing_required_by_dir": missing_required_by_dir,
        "exact_only_se": sorted(se_set - vr_set) if compare_se else [],
        "exact_only_vr": sorted(vr_set - se_set) if compare_se else [],
        "normalized_only_se": sorted(se_lower_set - vr_lower_set) if compare_se else [],
        "normalized_only_vr": sorted(vr_lower_set - se_lower_set) if compare_se else [],
        "content_mismatches": content_mismatches,
    }


def fmt_paths(paths, root=None, limit=20):
    if not paths:
        return "None.\n"

    lines = []
    for item in paths[:limit]:
        path = item
        if isinstance(item, pathlib.Path) and root is not None:
            try:
                path = item.relative_to(root)
            except ValueError:
                path = item
        lines.append(f"- `{path}`\n")
    if len(paths) > limit:
        lines.append(f"- ... {len(paths) - limit} more\n")
    return "".join(lines)


def write_report(path, package, skyrim_vr, plugin, papyrus, behaviors):
    path.parent.mkdir(parents=True, exist_ok=True)

    stale_pex_count = sum(len(hits) for _, hits, _ in papyrus["stale_pex"])
    missing_utils_pex_count = len(papyrus["missing_utils_pex_tokens"])
    compare_se = behaviors["compare_se"]
    exact_behavior_match = compare_se and not behaviors["exact_only_se"] and not behaviors["exact_only_vr"]
    normalized_behavior_match = compare_se and not behaviors["normalized_only_se"] and not behaviors["normalized_only_vr"]

    with path.open("w", encoding="utf-8") as handle:
        handle.write("# SkyrimTogetherVR GameFiles Audit\n\n")
        handle.write("Generated by `Tools/SkyrimVR/audit_gamefiles.py`.\n\n")
        handle.write("## Summary\n\n")
        handle.write(f"- Package path: `{package}`\n")
        handle.write(f"- Skyrim VR path: `{skyrim_vr}`\n")
        handle.write(f"- ESP masters: {', '.join(plugin['masters']) if plugin['masters'] else 'none found'}\n")
        if plugin["checked_masters"]:
            handle.write(f"- Missing ESP masters in VR Data: {len(plugin['missing_masters'])}\n")
        else:
            handle.write("- Missing ESP masters in VR Data: not checked; Skyrim VR `Data` path was not found\n")
        handle.write(f"- Papyrus source files: {len(papyrus['source_files'])}\n")
        handle.write(f"- Papyrus PEX files: {len(papyrus['pex_files'])}\n")
        handle.write(f"- Source files without matching PEX: {len(papyrus['missing_pex'])}\n")
        handle.write(f"- PEX files without matching source: {len(papyrus['missing_source'])}\n")
        handle.write(f"- Stale SE Papyrus PEX token hits: {stale_pex_count}\n")
        handle.write(f"- Missing VR connection spell ESP tokens: {len(plugin['missing_vr_menu_tokens'])}\n")
        handle.write(f"- Missing VR connection native declarations in `SkyrimTogetherUtils.pex`: {missing_utils_pex_count}\n")
        handle.write(f"- Missing VR connection menu source tokens: {len(papyrus['missing_vr_menu_source_tokens'])}\n")
        handle.write(f"- Missing VR connection menu PEX tokens: {len(papyrus['missing_vr_menu_pex_tokens'])}\n")
        handle.write(f"- Missing VR connection grant source tokens: {len(papyrus['missing_player_alias_source_tokens'])}\n")
        handle.write(f"- Missing VR connection grant PEX tokens: {len(papyrus['missing_player_alias_pex_tokens'])}\n")
        handle.write(f"- Missing VR connection spell-effect source tokens: {len(papyrus['missing_vr_effect_source_tokens'])}\n")
        handle.write(f"- Missing VR connection spell-effect PEX tokens: {len(papyrus['missing_vr_effect_pex_tokens'])}\n")
        handle.write(f"- Behavior files: {len(behaviors['vr_files'])}\n")
        handle.write(f"- Behavior suffix case issues: {len(behaviors['suffix_case_issues'])}\n")
        handle.write(f"- Behavior SE comparison: {'checked' if compare_se else 'skipped; pass `--compare-se` to compare against `GameFiles/Skyrim`'}\n")
        if compare_se:
            handle.write(f"- Behavior exact SE parity: {'yes' if exact_behavior_match else 'no'}\n")
            handle.write(f"- Behavior normalized SE parity: {'yes' if normalized_behavior_match else 'no'}\n")
            handle.write(f"- Behavior content mismatches on exact common files: {len(behaviors['content_mismatches'])}\n")
        handle.write("\n")

        handle.write("## ESP\n\n")
        handle.write(f"- Plugin exists: {'yes' if plugin['exists'] else 'no'}\n")
        handle.write(f"- Installed VR Data path checked: {'yes' if plugin['checked_masters'] else 'no'}")
        if not plugin["checked_masters"]:
            handle.write(f" (`{plugin['vr_data']}` was not found)")
        handle.write("\n")
        handle.write("- Masters:\n")
        handle.write(fmt_paths([pathlib.Path(master) for master in plugin["masters"]]))
        if plugin["checked_masters"]:
            handle.write("- Missing masters in installed Skyrim VR Data:\n")
            handle.write(fmt_paths([pathlib.Path(master) for master in plugin["missing_masters"]]))
        else:
            handle.write("- Missing masters in installed Skyrim VR Data: not checked because the Data path was not found.\n")
        handle.write("- Stale SE launch/path tokens in plugin bytes:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in plugin["stale_tokens"]]))
        handle.write("- Missing VR connection menu tokens in plugin bytes:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in plugin["missing_vr_menu_tokens"]]))

        handle.write("\n## Papyrus\n\n")
        handle.write("- Source files without matching PEX:\n")
        handle.write(fmt_paths(papyrus["missing_pex"], package))
        handle.write("- PEX files without matching source:\n")
        handle.write(fmt_paths(papyrus["missing_source"], package))
        handle.write("- Source/PEX exact-case mismatches:\n")
        if papyrus["exact_case_mismatches"]:
            for source, pex in papyrus["exact_case_mismatches"]:
                handle.write(f"- `{source.relative_to(package)}` -> `{pex.relative_to(package)}`\n")
        else:
            handle.write("None.\n")
        handle.write("- Missing VR tokens in `SkyrimTogetherVerifyLaunchScript.psc`:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in papyrus["missing_verify_tokens"]]))
        handle.write("- Missing SKSEVR tick bridge source tokens:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in papyrus["missing_tick_bridge_source_tokens"]]))
        handle.write("- Missing SKSEVR tick bridge PEX tokens:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in papyrus["missing_tick_bridge_pex_tokens"]]))
        handle.write("- Missing VR connection native declarations in `SkyrimTogetherUtils.psc`:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in papyrus["missing_utils_source_tokens"]]))
        handle.write("- Missing VR connection native declarations in `SkyrimTogetherUtils.pex`:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in papyrus["missing_utils_pex_tokens"]]))
        handle.write("- Missing VR connection menu source tokens in `SkyrimTogetherVRConnectionMenu.psc`:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in papyrus["missing_vr_menu_source_tokens"]]))
        handle.write("- Missing VR connection menu tokens in `SkyrimTogetherVRConnectionMenu.pex`:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in papyrus["missing_vr_menu_pex_tokens"]]))
        handle.write("- Missing VR connection grant tokens in `SkyrimTogetherPlayerAliasScript.psc`:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in papyrus["missing_player_alias_source_tokens"]]))
        handle.write("- Missing VR connection grant tokens in `SkyrimTogetherPlayerAliasScript.pex`:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in papyrus["missing_player_alias_pex_tokens"]]))
        handle.write("- Missing VR connection spell-effect tokens in `SkyrimTogetherVRConnectionSpellEffect.psc`:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in papyrus["missing_vr_effect_source_tokens"]]))
        handle.write("- Missing VR connection spell-effect tokens in `SkyrimTogetherVRConnectionSpellEffect.pex`:\n")
        handle.write(fmt_paths([pathlib.Path(token) for token in papyrus["missing_vr_effect_pex_tokens"]]))
        handle.write("- PEX files with stale SE launch/path strings:\n")
        if papyrus["stale_pex"]:
            for pex, hits, strings in papyrus["stale_pex"]:
                handle.write(f"- `{pex.relative_to(package)}`: {', '.join(f'`{hit}`' for hit in hits)}\n")
                relevant = [
                    value
                    for value in strings
                    if "Skyrim" in value or "Together" in value or "launch" in value or "Data" in value
                ]
                for value in relevant[:8]:
                    handle.write(f"  - `{value}`\n")
        else:
            handle.write("None.\n")
        needs_papyrus_rebuild = (
            papyrus["missing_pex"]
            or papyrus["stale_pex"]
            or papyrus["missing_utils_pex_tokens"]
            or papyrus["missing_tick_bridge_pex_tokens"]
            or papyrus["missing_vr_menu_pex_tokens"]
            or papyrus["missing_player_alias_pex_tokens"]
            or papyrus["missing_vr_effect_pex_tokens"]
        )
        if needs_papyrus_rebuild:
            handle.write(
                "\nThe VR-specific Papyrus PEX files must be regenerated after the VR source edits. "
                "Use `Tools/SkyrimVR/compile_papyrus.py` with a Caprica executable or a Skyrim Papyrus compiler.\n"
            )
        else:
            handle.write(
                "\nThe VR-specific Papyrus PEX files are regenerated. "
                "Use `Tools/SkyrimVR/compile_papyrus.py` if these sources change again.\n"
            )

        handle.write("\n## Behaviors\n\n")
        handle.write("### Suffix Counts\n\n")
        for suffix in EXPECTED_BEHAVIOR_SUFFIXES:
            handle.write(f"- `{suffix}`: {behaviors['suffix_counts'].get(suffix, 0)}\n")
        handle.write("\n### Case-Sensitive Suffix Issues\n\n")
        handle.write(fmt_paths(behaviors["suffix_case_issues"], behaviors["vr_root"]))
        handle.write("\n### Unrecognized Behavior Variable Files\n\n")
        handle.write(fmt_paths(behaviors["unrecognized"], behaviors["vr_root"]))
        handle.write("\n### Directories Missing Required Sig/Hash Files\n\n")
        if behaviors["missing_required_by_dir"]:
            for directory, missing in behaviors["missing_required_by_dir"]:
                handle.write(f"- `{directory}`: {', '.join(missing)}\n")
        else:
            handle.write("None.\n")
        handle.write("\n### SE/VR Behavior File List Differences\n\n")
        if compare_se:
            handle.write("- Present only in SE package by exact path:\n")
            handle.write(fmt_paths([pathlib.Path(path) for path in behaviors["exact_only_se"]]))
            handle.write("- Present only in VR package by exact path:\n")
            handle.write(fmt_paths([pathlib.Path(path) for path in behaviors["exact_only_vr"]]))
            handle.write("- Present only in SE package after lowercase normalization:\n")
            handle.write(fmt_paths([pathlib.Path(path) for path in behaviors["normalized_only_se"]]))
            handle.write("- Present only in VR package after lowercase normalization:\n")
            handle.write(fmt_paths([pathlib.Path(path) for path in behaviors["normalized_only_vr"]]))
            handle.write("- Content mismatches on exact common behavior files:\n")
            handle.write(fmt_paths(behaviors["content_mismatches"]))
        else:
            handle.write("Skipped for the default VR-only audit. Pass `--compare-se` to compare against `GameFiles/Skyrim`.\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=pathlib.Path, default=pathlib.Path.cwd())
    parser.add_argument(
        "--skyrim-vr",
        type=pathlib.Path,
        default=vr_paths.default_skyrim_vr_path(),
        help=vr_paths.skyrim_vr_help(),
    )
    parser.add_argument("--package", type=pathlib.Path, default=pathlib.Path("GameFiles/SkyrimVR"))
    parser.add_argument("--report", type=pathlib.Path, default=pathlib.Path("Docs/SkyrimVR/gamefiles-audit.md"))
    parser.add_argument(
        "--compare-se",
        action="store_true",
        help="also compare VR behavior files against the legacy GameFiles/Skyrim behavior tree",
    )
    args = parser.parse_args()

    repo = args.repo.resolve()
    package = (repo / args.package).resolve() if not args.package.is_absolute() else args.package.resolve()
    report = (repo / args.report).resolve() if not args.report.is_absolute() else args.report.resolve()

    plugin = audit_plugin(package, args.skyrim_vr)
    papyrus = audit_papyrus(package)
    behaviors = audit_behaviors(repo, package, compare_se=args.compare_se)
    write_report(report, package, args.skyrim_vr, plugin, papyrus, behaviors)

    stale_pex_count = sum(len(hits) for _, hits, _ in papyrus["stale_pex"])
    print(f"ESP masters: {', '.join(plugin['masters']) if plugin['masters'] else 'none found'}")
    if plugin["checked_masters"]:
        print(f"Missing ESP masters in VR Data: {len(plugin['missing_masters'])}")
    else:
        print(f"Missing ESP masters in VR Data: not checked; {plugin['vr_data']} was not found")
    print(f"Stale Papyrus PEX token hits: {stale_pex_count}")
    print(f"Missing SKSEVR tick bridge PEX tokens: {len(papyrus['missing_tick_bridge_pex_tokens'])}")
    print(f"Missing VR connection spell ESP tokens: {len(plugin['missing_vr_menu_tokens'])}")
    print(f"Papyrus source files without matching PEX: {len(papyrus['missing_pex'])}")
    print(f"Missing SkyrimTogetherUtils.pex VR native declarations: {len(papyrus['missing_utils_pex_tokens'])}")
    print(f"Missing SkyrimTogetherVRConnectionMenu.psc source tokens: {len(papyrus['missing_vr_menu_source_tokens'])}")
    print(f"Missing SkyrimTogetherVRConnectionMenu.pex tokens: {len(papyrus['missing_vr_menu_pex_tokens'])}")
    print(f"Missing SkyrimTogetherPlayerAliasScript.pex grant tokens: {len(papyrus['missing_player_alias_pex_tokens'])}")
    print(f"Missing SkyrimTogetherVRConnectionSpellEffect.pex tokens: {len(papyrus['missing_vr_effect_pex_tokens'])}")
    print(f"Behavior suffix case issues: {len(behaviors['suffix_case_issues'])}")
    print(f"Behavior SE comparison: {'checked' if behaviors['compare_se'] else 'skipped'}")


if __name__ == "__main__":
    main()
