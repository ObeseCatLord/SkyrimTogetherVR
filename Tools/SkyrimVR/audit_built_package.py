#!/usr/bin/env python3
import argparse
import ast
import csv
import hashlib
import json
import pathlib
import re
import struct
import sys
import tempfile

import audit_gamefiles
import vr_paths
from vr_address_contract import REQUIRED_VR_ADDRESS_ALIAS_ROWS, VALIDATED_VR_ADDRESS_OVERRIDES

BRIDGE_RUNTIME_FILES = (
    "EarlyLoad.dll",
    "TPProcess.exe",
    "Data/SKSE/Plugins/SkyrimTogetherVRVrikBridge.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVRHiggsBridge.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVRPlanckBridge.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVRTickBridge.dll",
)

CEF_RUNTIME_VERSION = "141.0.11"
CEF_RUNTIME_REQUIRED_FILES = (
    "chrome_elf.dll",
    "d3dcompiler_47.dll",
    "dxcompiler.dll",
    "dxil.dll",
    "libcef.dll",
    "libEGL.dll",
    "libGLESv2.dll",
    "v8_context_snapshot.bin",
    "vk_swiftshader.dll",
    "vulkan-1.dll",
    "chrome_100_percent.pak",
    "chrome_200_percent.pak",
    "icudtl.dat",
    "resources.pak",
    "locales/en-US.pak",
)

DLL_ONLY_REQUIRED_RUNTIME_FILES = (
    "EarlyLoad.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVRVrikBridge.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVRHiggsBridge.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVRPlanckBridge.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVRTickBridge.dll",
)

DEFAULT_REQUIRED_RUNTIME_FILES = (
    "SkyrimTogetherVR.exe",
    *BRIDGE_RUNTIME_FILES,
    *CEF_RUNTIME_REQUIRED_FILES,
)

AVATAR_SYNC_REQUIRED_RUNTIME_FILES = (
    "SkyrimTogetherVRAvatarSync.exe",
    *BRIDGE_RUNTIME_FILES,
    *CEF_RUNTIME_REQUIRED_FILES,
)

GAMEPLAY_REQUIRED_RUNTIME_FILES = (
    "SkyrimTogetherVRGameplay.exe",
    *BRIDGE_RUNTIME_FILES,
    *CEF_RUNTIME_REQUIRED_FILES,
)

REQUIRED_STAGED_FILES = (
    "SkyrimTogetherVR_BuildManifest.json",
    "Data/SkyrimTogether.esp",
    "Data/Seq/SkyrimTogether.seq",
    "Data/SKSE/Plugins/SkyrimTogetherVR_AE_to_SE.csv",
    "Data/SKSE/Plugins/SkyrimTogetherVR_AddressOverrides.csv",
    "Data/Scripts/SkyrimTogetherUtils.pex",
    "Data/Scripts/SkyrimTogetherVRTickBridge.pex",
    "Data/Scripts/SkyrimTogetherVerifyLaunchScript.pex",
    "Data/Scripts/SkyrimTogetherPlayerAliasScript.pex",
    "Data/Scripts/SkyrimTogetherVRMigrationXScript.pex",
    "Data/Scripts/SkyrimTogetherVRMigrationScript.pex",
    "Data/Scripts/SkyrimTogetherVRConnectionMenu.pex",
    "Data/Scripts/SkyrimTogetherVRConnectionSpellEffect.pex",
    "LaunchSkyrimTogetherVRCompanion.bat",
    "AuditSkyrimTogetherVRRuntime-Windows.bat",
    "AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat",
    "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat",
    "CollectSkyrimTogetherVREvidence-Windows.bat",
    "CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
    "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat",
    "AuditSkyrimTogetherVREvidence-Windows.bat",
    "AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
    "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat",
    "Tools/SkyrimVR/vr_handoff.py",
    "Tools/SkyrimVR/vr_paths.py",
    "Tools/SkyrimVR/audit_runtime_handoff.py",
    "Tools/SkyrimVR/collect_runtime_evidence.py",
    "Tools/SkyrimVR/audit_runtime_evidence_zip.py",
)

PACKAGED_PYTHON_HELPERS = (
    "vr_handoff.py",
    "vr_paths.py",
    "audit_runtime_handoff.py",
    "collect_runtime_evidence.py",
    "audit_runtime_evidence_zip.py",
)

FORBIDDEN_PACKAGE_FILES = (
    "SkyrimTogether.esp",
    "scripts",
    "Scripts",
    "meshes",
    "SkyrimTogetherRebornBehaviors",
    "SkyrimTogetherVR.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVR.dll",
    "SkyrimTogetherVRVrikBridge.dll",
    "SkyrimTogetherVRHiggsBridge.dll",
    "SkyrimTogetherVRPlanckBridge.dll",
    "SkyrimTogetherVRTickBridge.dll",
)

DEFAULT_FORBIDDEN_RUNTIME_FILES = (
    "SkyrimTogetherVRAvatarSync.exe",
    "SkyrimTogetherVRGameplay.exe",
)

AVATAR_SYNC_FORBIDDEN_RUNTIME_FILES = (
    "SkyrimTogetherVR.exe",
    "SkyrimTogetherVRGameplay.exe",
)

GAMEPLAY_FORBIDDEN_RUNTIME_FILES = (
    "SkyrimTogetherVR.exe",
    "SkyrimTogetherVRAvatarSync.exe",
)

DLL_ONLY_FORBIDDEN_RUNTIME_FILES = (
    "SkyrimTogetherVR.exe",
    "SkyrimTogetherVRAvatarSync.exe",
    "SkyrimTogetherVRGameplay.exe",
    "TPProcess.exe",
    "libcef.dll",
    "resources.pak",
    "locales",
)

VR_PREREQUISITE_FILES = {
    "vrik": (
        "Data/SKSE/Plugins/VRIK.dll",
        "Data/SKSE/Plugins/vrik.ini",
        "Data/SKSE/Plugins/vrikslots.ini",
        "Data/Scripts/VRIK.pex",
        "Data/Scripts/_vrik_qust_system.pex",
        "Data/meshes/vrik/torchoff.nif",
        "Data/vrik.esp",
    ),
    "higgs": (
        "Data/SKSE/Plugins/higgs_vr.dll",
        "Data/SKSE/Plugins/higgs_vr.ini",
        "Data/Scripts/HiggsVR.pex",
        "Data/higgs_vr.esp",
    ),
    "planck": (
        "Data/SKSE/Plugins/activeragdoll.dll",
        "Data/SKSE/Plugins/activeragdoll.ini",
        "Data/Scripts/planck.pex",
    ),
}

X64_MACHINE = 0x8664
MANIFEST_SCHEMA = "skyrim_together_vr_build_package_v2"
IMAGE_DIRECTORY_ENTRY_IMPORT = 1
IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT = 13
IMAGE_DELAYLOAD_ATTRIBUTE_RVA = 1
DEFAULT_EXPECTED_MANIFEST_TARGETS = (
    "SkyrimTogetherVRClient",
    "SkyrimTogetherVRVrikBridge",
    "SkyrimTogetherVRHiggsBridge",
    "SkyrimTogetherVRPlanckBridge",
    "SkyrimTogetherVRTickBridge",
    "SkyrimVRImmersiveLauncher",
    "ImmersiveElf",
    "TPProcess",
)
DLL_ONLY_EXPECTED_MANIFEST_TARGETS = (
    "SkyrimTogetherVRVrikBridge",
    "SkyrimTogetherVRHiggsBridge",
    "SkyrimTogetherVRPlanckBridge",
    "SkyrimTogetherVRTickBridge",
    "ImmersiveElf",
)
AVATAR_SYNC_EXPECTED_MANIFEST_TARGETS = (
    "SkyrimTogetherVRClientAvatarSync",
    "SkyrimVRImmersiveLauncherAvatarSync",
    "SkyrimTogetherVRVrikBridge",
    "SkyrimTogetherVRHiggsBridge",
    "SkyrimTogetherVRPlanckBridge",
    "SkyrimTogetherVRTickBridge",
    "ImmersiveElf",
    "TPProcess",
)
GAMEPLAY_EXPECTED_MANIFEST_TARGETS = (
    "SkyrimTogetherVRGameplayClient",
    "SkyrimVRImmersiveLauncherGameplay",
    "SkyrimTogetherVRVrikBridge",
    "SkyrimTogetherVRHiggsBridge",
    "SkyrimTogetherVRPlanckBridge",
    "SkyrimTogetherVRTickBridge",
    "ImmersiveElf",
    "TPProcess",
)


def repo_root():
    return pathlib.Path(__file__).resolve().parents[2]


def count_csv_rows(path):
    if not path.exists():
        return 0

    with path.open(encoding="utf-8-sig", errors="replace") as handle:
        return sum(1 for line in handle if line.strip())


def audit_vr_address_aliases(path, failures):
    if not path.exists():
        return

    rows = set()
    try:
        with path.open(newline="", encoding="utf-8-sig") as handle:
            for row in csv.DictReader(handle):
                rows.add((int(row.get("sseid", ""), 10), int(row.get("aeid", ""), 10)))
    except (OSError, TypeError, ValueError) as error:
        failures.append(f"invalid SkyrimTogetherVR_AE_to_SE.csv: {error}")
        return

    for vr_id, desktop_id in sorted(REQUIRED_VR_ADDRESS_ALIAS_ROWS - rows):
        failures.append(f"built package is missing required VR address alias {vr_id},{desktop_id}")


def audit_vr_address_overrides(path, failures):
    if not path.exists():
        return

    rows = {}
    try:
        with path.open(newline="", encoding="utf-8-sig") as handle:
            for row in csv.DictReader(handle):
                address_id = int(row.get("id", ""), 10)
                if address_id in rows:
                    failures.append(f"built package has duplicate VR address override {address_id}")
                rows[address_id] = row
    except (OSError, TypeError, ValueError) as error:
        failures.append(f"invalid SkyrimTogetherVR_AddressOverrides.csv: {error}")
        return

    for address_id, expected in sorted(VALIDATED_VR_ADDRESS_OVERRIDES.items()):
        row = rows.get(address_id)
        if row is None:
            failures.append(f"built package is missing required VR address override {address_id}")
            continue
        try:
            offset = int(row.get("offset", ""), 16)
        except ValueError:
            offset = -1
        actual = (offset, row.get("source", ""), row.get("status", ""), row.get("name", ""))
        wanted = (expected["offset"], expected["source"], expected["status"], expected["name"])
        if actual != wanted:
            failures.append(f"built package VR address override {address_id} is {actual!r}, expected {wanted!r}")


def pe_machine(path):
    try:
        with path.open("rb") as handle:
            mz = handle.read(0x40)
            if len(mz) < 0x40 or mz[:2] != b"MZ":
                return None

            pe_offset = int.from_bytes(mz[0x3C:0x40], "little")
            if pe_offset <= 0:
                return None

            handle.seek(pe_offset)
            header = handle.read(6)
            if len(header) < 6 or header[:4] != b"PE\0\0":
                return None

            return int.from_bytes(header[4:6], "little")
    except OSError:
        return None


def pe_imported_libraries(path, *, delay_imports=False):
    """Return the lower-case PE import names, or None when the PE cannot be parsed."""
    try:
        data = path.read_bytes()
    except OSError:
        return None

    if len(data) < 0x40 or data[:2] != b"MZ":
        return None
    nt_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if nt_offset + 24 > len(data) or data[nt_offset : nt_offset + 4] != b"PE\0\0":
        return None

    coff_offset = nt_offset + 4
    section_count = struct.unpack_from("<H", data, coff_offset + 2)[0]
    optional_size = struct.unpack_from("<H", data, coff_offset + 16)[0]
    optional_offset = coff_offset + 20
    if optional_offset + optional_size > len(data) or optional_size < 112:
        return None

    optional_magic = struct.unpack_from("<H", data, optional_offset)[0]
    if optional_magic == 0x20B:
        number_of_rvas_offset = optional_offset + 108
        data_directories_offset = optional_offset + 112
        image_base = struct.unpack_from("<Q", data, optional_offset + 24)[0]
    elif optional_magic == 0x10B:
        number_of_rvas_offset = optional_offset + 92
        data_directories_offset = optional_offset + 96
        image_base = struct.unpack_from("<I", data, optional_offset + 28)[0]
    else:
        return None

    if number_of_rvas_offset + 4 > len(data):
        return None
    number_of_rvas = struct.unpack_from("<I", data, number_of_rvas_offset)[0]
    directory_index = IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT if delay_imports else IMAGE_DIRECTORY_ENTRY_IMPORT
    directory_offset = data_directories_offset + directory_index * 8
    if number_of_rvas <= directory_index or directory_offset + 8 > optional_offset + optional_size:
        return set()

    directory_rva, directory_size = struct.unpack_from("<II", data, directory_offset)
    if directory_rva == 0 or directory_size == 0:
        return set()

    sections_offset = optional_offset + optional_size
    if sections_offset + section_count * 40 > len(data):
        return None

    sections = []
    for index in range(section_count):
        offset = sections_offset + index * 40
        virtual_size = struct.unpack_from("<I", data, offset + 8)[0]
        virtual_address = struct.unpack_from("<I", data, offset + 12)[0]
        raw_size = struct.unpack_from("<I", data, offset + 16)[0]
        raw_offset = struct.unpack_from("<I", data, offset + 20)[0]
        sections.append((virtual_address, max(virtual_size, raw_size), raw_offset))

    def rva_to_offset(rva):
        for virtual_address, size, raw_offset in sections:
            if virtual_address <= rva < virtual_address + size:
                offset = raw_offset + rva - virtual_address
                return offset if offset < len(data) else None
        return None

    descriptors_offset = rva_to_offset(directory_rva)
    descriptor_size = 32 if delay_imports else 20
    if descriptors_offset is None:
        return None

    libraries = set()
    max_descriptors = directory_size // descriptor_size
    for index in range(max_descriptors):
        offset = descriptors_offset + index * descriptor_size
        if offset + descriptor_size > len(data):
            return None
        descriptor = data[offset : offset + descriptor_size]
        if not any(descriptor):
            break

        name_rva = struct.unpack_from("<I", descriptor, 4 if delay_imports else 12)[0]
        if delay_imports and not (struct.unpack_from("<I", descriptor, 0)[0] & IMAGE_DELAYLOAD_ATTRIBUTE_RVA):
            if name_rva < image_base:
                return None
            name_rva -= image_base
        name_offset = rva_to_offset(name_rva)
        if name_offset is None:
            return None
        end = data.find(b"\0", name_offset)
        if end < 0:
            return None
        libraries.add(data[name_offset:end].decode("ascii", errors="replace").lower())

    return libraries


def launcher_runtime_files(avatar_sync=False, dll_only=False, gameplay=False):
    if dll_only:
        return ()
    return tuple(
        relative_file
        for relative_file in required_runtime_files(avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay)
        if pathlib.PurePath(relative_file).suffix.lower() == ".exe"
        and pathlib.PurePath(relative_file).name.lower() != "tpprocess.exe"
    )


def audit_cef_delay_import(package, failures, avatar_sync=False, dll_only=False, gameplay=False):
    for relative_file in launcher_runtime_files(
        avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay
    ):
        path = package / relative_file
        normal_imports = pe_imported_libraries(path, delay_imports=False)
        delay_imports = pe_imported_libraries(path, delay_imports=True)
        if normal_imports is None or delay_imports is None:
            failures.append(f"could not inspect launcher imports: {relative_file}")
            continue
        if "libcef.dll" in normal_imports:
            failures.append(f"launcher package imports libcef.dll normally: {relative_file}")
        if "libcef.dll" not in delay_imports:
            failures.append(f"launcher package does not delay-import libcef.dll: {relative_file}")


def exists_case_insensitive(directory, names):
    if not directory.exists():
        return False

    wanted = {name.lower() for name in names}
    return any(child.name.lower() in wanted for child in directory.iterdir())


def exists_relative_case_insensitive(root, relative_path):
    current = root
    for part in pathlib.PurePath(relative_path).parts:
        if not current.exists() or not current.is_dir():
            return False
        wanted = part.lower()
        match = next((child for child in current.iterdir() if child.name.lower() == wanted), None)
        if match is None:
            return False
        current = match
    return current.exists()


def missing_relative_files_case_insensitive(root, relative_paths):
    return [
        relative_path
        for relative_path in relative_paths
        if not exists_relative_case_insensitive(root, relative_path)
    ]


def local_import_modules(path):
    try:
        tree = ast.parse(path.read_text(encoding="utf-8", errors="replace"), filename=str(path))
    except SyntaxError as exc:
        return set(), [f"{path}: Python syntax error while auditing imports: {exc}"]
    except OSError as exc:
        return set(), [f"{path}: could not read Python helper while auditing imports: {exc}"]

    modules = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                modules.add(alias.name.split(".", 1)[0])
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            modules.add(node.module.split(".", 1)[0])

    return modules, []


def packaged_python_import_closure(package):
    tool_dir = package / "Tools" / "SkyrimVR"
    failures = []
    required = set(PACKAGED_PYTHON_HELPERS)
    queue = list(PACKAGED_PYTHON_HELPERS)
    visited = set()

    while queue:
        helper = queue.pop(0)
        if helper in visited:
            continue
        visited.add(helper)
        path = tool_dir / helper
        if not path.exists():
            failures.append(f"missing package Python helper: Tools/SkyrimVR/{helper}")
            continue

        modules, import_failures = local_import_modules(path)
        failures.extend(import_failures)
        for module in sorted(modules):
            candidate = f"{module}.py"
            if not (tool_dir / candidate).exists():
                continue
            if candidate not in required:
                required.add(candidate)
                queue.append(candidate)

    return tuple(sorted(required)), failures


def pex_tokens_for_package_file(relative_path):
    name = pathlib.PurePath(relative_path).name.lower()
    if name == "skyrimtogetherutils.pex":
        return audit_gamefiles.REQUIRED_UTILS_NATIVE_TOKENS
    if name == "skyrimtogethervrtickbridge.pex":
        return audit_gamefiles.REQUIRED_TICK_BRIDGE_PEX_TOKENS
    if name == "skyrimtogethervrconnectionmenu.pex":
        return audit_gamefiles.REQUIRED_VR_MENU_PEX_TOKENS
    if name == "skyrimtogetherplayeraliasscript.pex":
        return audit_gamefiles.REQUIRED_PLAYER_ALIAS_PEX_TOKENS
    if name == "skyrimtogethervrmigrationxscript.pex":
        return audit_gamefiles.REQUIRED_MIGRATION_PEX_TOKENS
    if name == "skyrimtogethervrmigrationscript.pex":
        return audit_gamefiles.REQUIRED_MIGRATION_ALIAS_PEX_TOKENS
    if name == "skyrimtogethervrconnectionspelleffect.pex":
        return audit_gamefiles.REQUIRED_VR_EFFECT_PEX_TOKENS
    return ()


def audit_packaged_papyrus(package, failures):
    data_dir = package / "Data"
    script_variants = sorted(
        entry.name
        for entry in data_dir.iterdir()
        if entry.is_dir() and entry.name.casefold() == "scripts"
    ) if data_dir.is_dir() else []
    if script_variants != ["Scripts"]:
        failures.append(
            "package must contain exactly one canonical Data/Scripts directory; found: "
            + (", ".join(script_variants) if script_variants else "none")
        )

    scripts_dir = package / "Data" / "Scripts"
    if not scripts_dir.exists() or not scripts_dir.is_dir():
        failures.append("missing package Papyrus directory: Data/Scripts")
        return

    for pex_path in sorted(scripts_dir.glob("*.pex")):
        data = pex_path.read_bytes()
        relative_path = pex_path.relative_to(package).as_posix()
        stale_hits = [
            token.decode("ascii")
            for token in audit_gamefiles.STALE_PEX_TOKENS
            if token in data
        ]
        if stale_hits:
            failures.append(
                f"packaged Papyrus bytecode has stale SE token(s): {relative_path}: "
                + ", ".join(stale_hits)
            )

    for relative_path in REQUIRED_STAGED_FILES:
        if not relative_path.lower().endswith(".pex"):
            continue
        tokens = pex_tokens_for_package_file(relative_path)
        if not tokens:
            continue
        pex_path = package / relative_path
        if not pex_path.exists() or not pex_path.is_file():
            continue
        data = pex_path.read_bytes()
        missing_tokens = [
            token
            for token in tokens
            if token.encode("ascii") not in data
        ]
        if missing_tokens:
            failures.append(
                f"packaged Papyrus bytecode is missing VR token(s): {relative_path}: "
                + ", ".join(missing_tokens)
            )


def audit_installed_papyrus(package, skyrim_vr, failures):
    installed_data = skyrim_vr / "Data"
    variants = sorted(
        entry.name
        for entry in installed_data.iterdir()
        if entry.is_dir() and entry.name.casefold() == "scripts"
    ) if installed_data.is_dir() else []
    if variants != ["Scripts"]:
        failures.append(
            "target must contain exactly one canonical Data/Scripts directory; found: "
            + (", ".join(variants) if variants else "none")
        )
        return

    for relative_path in REQUIRED_STAGED_FILES:
        if not relative_path.startswith("Data/Scripts/") or not relative_path.endswith(".pex"):
            continue
        packaged = package / relative_path
        installed = skyrim_vr / relative_path
        if not installed.is_file():
            failures.append(f"target is missing packaged Papyrus file: {relative_path}")
        elif packaged.is_file() and sha256(installed) != sha256(packaged):
            failures.append(f"target Papyrus hash mismatch: {relative_path}")


def audit_startup_quest_assets(package, skyrim_vr, failures):
    data_dir = package / "Data"
    plugin_path = data_dir / "SkyrimTogether.esp"
    sequence_path = data_dir / "Seq" / "SkyrimTogether.seq"
    if not plugin_path.exists() or not sequence_path.exists():
        return

    try:
        plugin = audit_gamefiles.audit_plugin(data_dir, skyrim_vr)
    except (OSError, ValueError) as exc:
        failures.append(f"could not audit packaged startup quest assets: {exc}")
        return

    if plugin["sequence_error"]:
        failures.append(f"packaged startup SEQ generation failed: {plugin['sequence_error']}")
    if plugin["startup_migration_error"]:
        failures.append(f"packaged startup migration quest verification failed: {plugin['startup_migration_error']}")
    if plugin["missing_expected_start_quests"]:
        failures.append(
            "packaged startup migration is missing expected quest FormID(s): "
            + ", ".join(f"0x{form_id:08X}" for form_id in plugin["missing_expected_start_quests"])
        )
    if not plugin["sequence_matches"]:
        failures.append("packaged startup SEQ does not exactly match the staged SkyrimTogether.esp")


def check_file(package, relative_path, failures, require_pe=False):
    path = package / relative_path
    if not path.exists():
        failures.append(f"missing package file: {relative_path}")
        return

    if not path.is_file():
        failures.append(f"package path is not a file: {relative_path}")
        return

    size = path.stat().st_size
    if size == 0:
        failures.append(f"package file is empty: {relative_path}")

    if require_pe:
        machine = pe_machine(path)
        if machine is None:
            failures.append(f"package runtime is not a PE image: {relative_path}")
        elif machine != X64_MACHINE:
            failures.append(
                f"package runtime is not x64 PE: {relative_path} machine=0x{machine:04x}"
            )


def load_build_manifest(package, failures):
    path = package / "SkyrimTogetherVR_BuildManifest.json"
    if not path.exists() or not path.is_file():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as exc:
        failures.append(f"build manifest is not valid JSON: {exc}")
    except OSError as exc:
        failures.append(f"could not read build manifest: {exc}")
    return {}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def package_file_hashes(package):
    return {
        path.relative_to(package).as_posix(): sha256(path)
        for path in package.rglob("*")
        if path.is_file() and path.relative_to(package).as_posix() != "SkyrimTogetherVR_BuildManifest.json"
    }


def normalize_manifest_relative_path(value):
    if not isinstance(value, str) or not value:
        return None
    path = pathlib.PurePosixPath(value.replace("\\", "/"))
    if path.is_absolute() or ".." in path.parts or path.name == "SkyrimTogetherVR_BuildManifest.json":
        return None
    return path.as_posix()


def manifest_artifact_path(package, artifact):
    if artifact.startswith("SkyrimTogetherVR") and "Bridge." in artifact:
        return package / "Data" / "SKSE" / "Plugins" / artifact
    return package / artifact


def required_runtime_files(avatar_sync=False, dll_only=False, gameplay=False):
    if dll_only:
        return DLL_ONLY_REQUIRED_RUNTIME_FILES
    if gameplay:
        return GAMEPLAY_REQUIRED_RUNTIME_FILES
    return AVATAR_SYNC_REQUIRED_RUNTIME_FILES if avatar_sync else DEFAULT_REQUIRED_RUNTIME_FILES


def manifest_runtime_files(avatar_sync=False, dll_only=False, gameplay=False):
    runtime_files = required_runtime_files(avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay)
    return tuple(relative_file for relative_file in runtime_files if relative_file not in CEF_RUNTIME_REQUIRED_FILES)


def expected_manifest_targets(avatar_sync=False, dll_only=False, gameplay=False):
    if dll_only:
        return DLL_ONLY_EXPECTED_MANIFEST_TARGETS
    if gameplay:
        return GAMEPLAY_EXPECTED_MANIFEST_TARGETS
    return AVATAR_SYNC_EXPECTED_MANIFEST_TARGETS if avatar_sync else DEFAULT_EXPECTED_MANIFEST_TARGETS


def package_mode_name(avatar_sync=False, dll_only=False, gameplay=False):
    if dll_only:
        return "dll-only"
    if gameplay:
        return "gameplay"
    return "avatar-sync" if avatar_sync else "default"


def audit_build_manifest(package, failures, avatar_sync, dll_only=False, gameplay=False):
    manifest = load_build_manifest(package, failures)
    if not manifest:
        return

    schema = manifest.get("schema")
    if schema != MANIFEST_SCHEMA:
        failures.append(f"build manifest has unexpected schema: {schema!r}")

    if manifest.get("platform") != "windows":
        failures.append(f"build manifest platform is not windows: {manifest.get('platform')!r}")
    if manifest.get("arch") != "x64":
        failures.append(f"build manifest arch is not x64: {manifest.get('arch')!r}")
    if bool(manifest.get("avatarSync")) != bool(avatar_sync):
        failures.append(
            "build manifest avatarSync does not match audit mode: "
            f"{manifest.get('avatarSync')!r}"
        )
    if bool(manifest.get("gameplay")) != bool(gameplay):
        failures.append(
            "build manifest gameplay does not match audit mode: "
            f"{manifest.get('gameplay')!r}"
        )
    expected_flavor = package_mode_name(avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay)
    if manifest.get("packageFlavor") != expected_flavor:
        failures.append(
            "build manifest packageFlavor does not match audit mode: "
            f"{manifest.get('packageFlavor')!r}"
        )
    if manifest.get("stagedGameFiles") is not True:
        failures.append("build manifest says staged game files were not packaged")
    if manifest.get("companionPanel") is not True:
        failures.append("build manifest says companion panel helpers were not packaged")

    targets = manifest.get("targets")
    if not isinstance(targets, list):
        failures.append("build manifest targets field is not a list")
        target_set = set()
    else:
        target_set = {str(target) for target in targets}
    required_targets = expected_manifest_targets(avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay)
    missing_targets = [target for target in required_targets if target not in target_set]
    if missing_targets:
        failures.append("build manifest is missing target(s): " + ", ".join(missing_targets))
    unexpected_targets = [target for target in sorted(target_set) if target not in required_targets]
    if dll_only and unexpected_targets:
        failures.append("dll-only build manifest has unexpected target(s): " + ", ".join(unexpected_targets))

    copied_artifacts = manifest.get("copiedArtifacts")
    if not isinstance(copied_artifacts, list):
        failures.append("build manifest copiedArtifacts field is not a list")
        copied_set = set()
    else:
        copied_set = {str(artifact) for artifact in copied_artifacts}
    runtime_files = manifest_runtime_files(avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay)
    missing_runtime_artifacts = [
        pathlib.PurePath(relative_file).name
        for relative_file in runtime_files
        if pathlib.PurePath(relative_file).name not in copied_set
    ]
    if missing_runtime_artifacts:
        failures.append(
            "build manifest copiedArtifacts missing runtime artifact(s): "
            + ", ".join(missing_runtime_artifacts)
        )
    unexpected_runtime_artifacts = [
        artifact
        for artifact in sorted(copied_set)
        if dll_only and artifact in {"SkyrimTogetherVR.exe", "SkyrimTogetherVRAvatarSync.exe", "SkyrimTogetherVRGameplay.exe", "TPProcess.exe"}
    ]
    if unexpected_runtime_artifacts:
        failures.append(
            "dll-only build manifest copiedArtifacts has unexpected runtime artifact(s): "
            + ", ".join(unexpected_runtime_artifacts)
        )

    source_revision = manifest.get("sourceRevision")
    source_provenance = manifest.get("sourceProvenance")
    if not isinstance(source_provenance, dict):
        failures.append("build manifest sourceProvenance field is not an object")
    else:
        revision = source_provenance.get("revision")
        source_tree_sha256 = source_provenance.get("sourceTreeSha256")
        dirty = source_provenance.get("dirty")
        dirty_approved = source_provenance.get("dirtyApproved")
        if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-fA-F]{40}", revision):
            failures.append("build manifest sourceProvenance revision is missing or invalid")
        if not isinstance(source_tree_sha256, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", source_tree_sha256):
            failures.append("build manifest sourceProvenance sourceTreeSha256 is missing or invalid")
        if not isinstance(dirty, bool):
            failures.append("build manifest sourceProvenance dirty flag is missing or invalid")
        if not isinstance(dirty_approved, bool):
            failures.append("build manifest sourceProvenance dirtyApproved flag is missing or invalid")
        if isinstance(revision, str) and isinstance(source_tree_sha256, str) and isinstance(dirty, bool):
            expected_source_revision = f"{revision}-dirty-{source_tree_sha256}" if dirty else revision
            if source_revision != expected_source_revision:
                failures.append("build manifest sourceRevision does not match sourceProvenance")
        if dirty is True and dirty_approved is not True:
            failures.append("build manifest sourceProvenance has an unapproved dirty source tree")
        if dirty is False and dirty_approved is True:
            failures.append("build manifest sourceProvenance marks a clean source tree as dirty-approved")

    package_hashes = manifest.get("packageFileSha256")
    if not isinstance(package_hashes, dict):
        failures.append("build manifest packageFileSha256 field is not an object")
    else:
        normalized_hashes = {}
        for relative_path, expected_hash in package_hashes.items():
            normalized_path = normalize_manifest_relative_path(relative_path)
            if normalized_path is None:
                failures.append(f"build manifest packageFileSha256 has invalid path: {relative_path!r}")
                continue
            if not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected_hash):
                failures.append(f"build manifest packageFileSha256 has invalid SHA-256: {normalized_path}")
                continue
            normalized_hashes[normalized_path] = expected_hash.lower()

        actual_hashes = package_file_hashes(package)
        missing_payload_hashes = sorted(set(actual_hashes) - set(normalized_hashes))
        unexpected_payload_hashes = sorted(set(normalized_hashes) - set(actual_hashes))
        if missing_payload_hashes:
            failures.append("build manifest packageFileSha256 missing package file(s): " + ", ".join(missing_payload_hashes))
        if unexpected_payload_hashes:
            failures.append("build manifest packageFileSha256 has unexpected package file(s): " + ", ".join(unexpected_payload_hashes))
        for relative_path in sorted(set(actual_hashes) & set(normalized_hashes)):
            if actual_hashes[relative_path].lower() != normalized_hashes[relative_path]:
                failures.append(f"build manifest package SHA-256 mismatch: {relative_path}")

    artifact_hashes = manifest.get("artifactSha256")
    if not isinstance(artifact_hashes, dict):
        failures.append("build manifest artifactSha256 field is not an object")
    else:
        missing_hashes = [artifact for artifact in sorted(copied_set) if not isinstance(artifact_hashes.get(artifact), str)]
        if missing_hashes:
            failures.append("build manifest artifactSha256 missing copied artifact(s): " + ", ".join(missing_hashes))
        for artifact in sorted(copied_set):
            expected_hash = artifact_hashes.get(artifact)
            if not isinstance(expected_hash, str):
                continue
            path = manifest_artifact_path(package, artifact)
            if not path.is_file():
                failures.append(f"build manifest copied artifact is missing from package: {artifact}")
            elif sha256(path).lower() != expected_hash.lower():
                failures.append(f"build manifest artifact SHA-256 mismatch: {artifact}")

    cef_runtime = manifest.get("cefRuntime")
    if dll_only:
        if cef_runtime is not None:
            failures.append("dll-only build manifest unexpectedly includes a CEF runtime")
    elif not isinstance(cef_runtime, dict):
        failures.append("launcher build manifest is missing CEF runtime metadata")
    else:
        if cef_runtime.get("version") != CEF_RUNTIME_VERSION:
            failures.append(
                "launcher build manifest has unexpected CEF runtime version: "
                f"{cef_runtime.get('version')!r}"
            )
        files = cef_runtime.get("files")
        if not isinstance(files, list):
            failures.append("launcher build manifest CEF runtime files field is not a list")
        else:
            normalized_files = {str(path).replace("\\", "/") for path in files}
            missing_cef_metadata = [
                relative_file
                for relative_file in CEF_RUNTIME_REQUIRED_FILES
                if relative_file not in normalized_files
            ]
            if missing_cef_metadata:
                failures.append(
                    "launcher build manifest CEF runtime files missing: "
                    + ", ".join(missing_cef_metadata)
                )


def audit_package(
    package,
    skyrim_vr,
    avatar_sync=False,
    dll_only=False,
    gameplay=False,
    require_installed_prerequisites=False,
    verify_installed_papyrus=True,
    require_vrik=False,
    require_higgs=False,
    require_planck=False,
):
    failures = []

    if not package.exists():
        failures.append(f"package root does not exist: {package}")
    elif not package.is_dir():
        failures.append(f"package root is not a directory: {package}")

    runtime_files = required_runtime_files(avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay)
    for relative_file in runtime_files:
        require_pe = pathlib.PurePath(relative_file).suffix.lower() in {".dll", ".exe"}
        check_file(package, relative_file, failures, require_pe=require_pe)

    for relative_file in REQUIRED_STAGED_FILES:
        check_file(package, relative_file, failures)
    audit_packaged_papyrus(package, failures)
    audit_startup_quest_assets(package, skyrim_vr, failures)
    helper_closure, helper_failures = packaged_python_import_closure(package)
    failures.extend(helper_failures)
    for helper in helper_closure:
        relative = f"Tools/SkyrimVR/{helper}"
        if relative not in REQUIRED_STAGED_FILES:
            failures.append(f"packaged Python helper import closure is not required by package audit: {relative}")
    audit_build_manifest(package, failures, avatar_sync, dll_only=dll_only, gameplay=gameplay)
    audit_cef_delay_import(package, failures, avatar_sync, dll_only=dll_only, gameplay=gameplay)

    ae_to_se_rows = count_csv_rows(package / "Data" / "SKSE" / "Plugins" / "SkyrimTogetherVR_AE_to_SE.csv")
    overrides_rows = count_csv_rows(
        package / "Data" / "SKSE" / "Plugins" / "SkyrimTogetherVR_AddressOverrides.csv"
    )
    if ae_to_se_rows <= 1:
        failures.append("SkyrimTogetherVR_AE_to_SE.csv has no data rows in the built package")
    if overrides_rows <= 1:
        failures.append("SkyrimTogetherVR_AddressOverrides.csv has no data rows in the built package")
    audit_vr_address_aliases(
        package / "Data" / "SKSE" / "Plugins" / "SkyrimTogetherVR_AE_to_SE.csv", failures
    )
    audit_vr_address_overrides(
        package / "Data" / "SKSE" / "Plugins" / "SkyrimTogetherVR_AddressOverrides.csv", failures
    )

    if dll_only:
        mode_forbidden_files = DLL_ONLY_FORBIDDEN_RUNTIME_FILES
    elif gameplay:
        mode_forbidden_files = GAMEPLAY_FORBIDDEN_RUNTIME_FILES
    else:
        mode_forbidden_files = AVATAR_SYNC_FORBIDDEN_RUNTIME_FILES if avatar_sync else DEFAULT_FORBIDDEN_RUNTIME_FILES
    for relative_file in (*FORBIDDEN_PACKAGE_FILES, *mode_forbidden_files):
        if (package / relative_file).exists():
            failures.append(f"forbidden package file present: {relative_file}")

    prereq = installed_prerequisites(skyrim_vr)
    if require_installed_prerequisites:
        if verify_installed_papyrus:
            audit_installed_papyrus(package, skyrim_vr, failures)
        if not prereq["sksevr"]:
            failures.append("target Skyrim VR install is missing sksevr_1_4_15.dll")
        if not prereq["sksevr_steam_loader"]:
            failures.append("target Skyrim VR install is missing sksevr_steam_loader.dll")
        if not prereq["address_library"]:
            failures.append(
                "target Skyrim VR install is missing Data/SKSE/Plugins/version-1-4-15-0.csv"
            )

    if require_vrik and prereq["missing_vrik_files"]:
        failures.append(
            "target Skyrim VR install is missing VRIK file(s): "
            + ", ".join(prereq["missing_vrik_files"])
        )
    if require_higgs and prereq["missing_higgs_files"]:
        failures.append(
            "target Skyrim VR install is missing HIGGS file(s): "
            + ", ".join(prereq["missing_higgs_files"])
        )
    if require_planck and prereq["missing_planck_files"]:
        failures.append(
            "target Skyrim VR install is missing PLANCK file(s): "
            + ", ".join(prereq["missing_planck_files"])
        )

    return failures, prereq, ae_to_se_rows, overrides_rows


def installed_prerequisites(skyrim_vr):
    plugin_dir = skyrim_vr / "Data" / "SKSE" / "Plugins"
    missing_vrik_files = missing_relative_files_case_insensitive(skyrim_vr, VR_PREREQUISITE_FILES["vrik"])
    missing_higgs_files = missing_relative_files_case_insensitive(skyrim_vr, VR_PREREQUISITE_FILES["higgs"])
    missing_planck_files = missing_relative_files_case_insensitive(skyrim_vr, VR_PREREQUISITE_FILES["planck"])
    higgs_dll_installed = exists_case_insensitive(plugin_dir, ("higgs_vr.dll", "higgs.dll"))
    if higgs_dll_installed:
        missing_higgs_files = [
            path for path in missing_higgs_files
            if pathlib.PurePath(path).name.lower() != "higgs_vr.dll"
        ]
    return {
        "sksevr": (skyrim_vr / "sksevr_1_4_15.dll").exists(),
        "sksevr_steam_loader": (skyrim_vr / "sksevr_steam_loader.dll").exists(),
        "address_library": (plugin_dir / "version-1-4-15-0.csv").exists(),
        "vrik": exists_case_insensitive(plugin_dir, ("vrik.dll",)),
        "higgs": higgs_dll_installed,
        "planck": exists_case_insensitive(plugin_dir, ("activeragdoll.dll",)),
        "missing_vrik_files": missing_vrik_files,
        "missing_higgs_files": missing_higgs_files,
        "missing_planck_files": missing_planck_files,
    }


def write_file(path, data=b"package-test"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def python_helper_fixture(path):
    imports = {
        "audit_runtime_evidence_zip.py": "import collect_runtime_evidence\n",
        "collect_runtime_evidence.py": "import audit_runtime_handoff\nimport vr_handoff\nimport vr_paths\n",
        "audit_runtime_handoff.py": "import vr_handoff\nimport vr_paths\n",
        "vr_handoff.py": "import vr_paths\n",
        "vr_paths.py": "",
    }
    return f"from __future__ import annotations\n{imports.get(path.name, '')}\n"


def write_x64_pe(path, normal_imports=(), delay_imports=()):
    data = bytearray(0xA00)
    data[0:2] = b"MZ"
    data[0x3C:0x40] = (0x80).to_bytes(4, "little")
    data[0x80:0x84] = b"PE\0\0"
    data[0x84:0x86] = X64_MACHINE.to_bytes(2, "little")
    struct.pack_into("<H", data, 0x86, 1)
    struct.pack_into("<H", data, 0x94, 0xF0)

    optional_offset = 0x98
    struct.pack_into("<H", data, optional_offset, 0x20B)
    struct.pack_into("<Q", data, optional_offset + 24, 0x140000000)
    struct.pack_into("<I", data, optional_offset + 32, 0x1000)
    struct.pack_into("<I", data, optional_offset + 36, 0x200)
    struct.pack_into("<I", data, optional_offset + 56, 0x2000)
    struct.pack_into("<I", data, optional_offset + 60, 0x200)
    struct.pack_into("<I", data, optional_offset + 108, 16)

    section_offset = optional_offset + 0xF0
    data[section_offset : section_offset + 8] = b".rdata\0\0"
    struct.pack_into("<I", data, section_offset + 8, 0x800)
    struct.pack_into("<I", data, section_offset + 12, 0x1000)
    struct.pack_into("<I", data, section_offset + 16, 0x800)
    struct.pack_into("<I", data, section_offset + 20, 0x200)

    def rva_to_offset(rva):
        return 0x200 + rva - 0x1000

    def write_import_table(rva, libraries, descriptor_size, name_offset, delay=False):
        for index, library in enumerate(libraries):
            descriptor_offset = rva_to_offset(rva) + index * descriptor_size
            name_rva = 0x1400 + name_offset
            if delay:
                struct.pack_into("<I", data, descriptor_offset, IMAGE_DELAYLOAD_ATTRIBUTE_RVA)
                struct.pack_into("<I", data, descriptor_offset + 4, name_rva)
            else:
                struct.pack_into("<I", data, descriptor_offset + 12, name_rva)
            encoded = library.encode("ascii") + b"\0"
            string_offset = rva_to_offset(name_rva)
            data[string_offset : string_offset + len(encoded)] = encoded
            name_offset += len(encoded)
        return name_offset

    normal_imports = tuple(normal_imports)
    delay_imports = tuple(delay_imports)
    if normal_imports:
        struct.pack_into("<II", data, optional_offset + 112 + IMAGE_DIRECTORY_ENTRY_IMPORT * 8, 0x1000, (len(normal_imports) + 1) * 20)
    next_name_offset = write_import_table(0x1000, normal_imports, 20, 0, delay=False)
    if delay_imports:
        struct.pack_into("<II", data, optional_offset + 112 + IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT * 8, 0x1200, (len(delay_imports) + 1) * 32)
    write_import_table(0x1200, delay_imports, 32, next_name_offset, delay=True)
    write_file(path, bytes(data))


def write_csv(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.name == "SkyrimTogetherVR_AE_to_SE.csv":
        rows = ["sseid,aeid"]
        rows.extend(f"{vr_id},{desktop_id}" for vr_id, desktop_id in sorted(REQUIRED_VR_ADDRESS_ALIAS_ROWS))
        path.write_text("\n".join(rows) + "\n", encoding="utf-8")
    elif path.name == "SkyrimTogetherVR_AddressOverrides.csv":
        rows = ["id,offset,source,status,name"]
        rows.extend(
            f"{address_id},{metadata['offset']:x},{metadata['source']},{metadata['status']},{metadata['name']}"
            for address_id, metadata in sorted(VALIDATED_VR_ADDRESS_OVERRIDES.items())
        )
        path.write_text("\n".join(rows) + "\n", encoding="utf-8")
    else:
        path.write_text("id,address\n1,2\n", encoding="utf-8")


def papyrus_pex_fixture():
    tokens = []
    for token_group in (
        audit_gamefiles.REQUIRED_UTILS_NATIVE_TOKENS,
        audit_gamefiles.REQUIRED_TICK_BRIDGE_PEX_TOKENS,
        audit_gamefiles.REQUIRED_VR_MENU_PEX_TOKENS,
        audit_gamefiles.REQUIRED_PLAYER_ALIAS_PEX_TOKENS,
        audit_gamefiles.REQUIRED_MIGRATION_PEX_TOKENS,
        audit_gamefiles.REQUIRED_MIGRATION_ALIAS_PEX_TOKENS,
        audit_gamefiles.REQUIRED_VR_EFFECT_PEX_TOKENS,
    ):
        tokens.extend(token_group)
    return "\n".join(sorted(set(tokens))).encode("ascii")


def write_build_manifest(package, avatar_sync=False, dll_only=False, gameplay=False):
    runtime_files = manifest_runtime_files(avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay)
    targets = expected_manifest_targets(avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay)
    copied_artifacts = [pathlib.PurePath(relative_file).name for relative_file in runtime_files]
    manifest = {
        "schema": MANIFEST_SCHEMA,
        "mode": "releasedbg",
        "platform": "windows",
        "arch": "x64",
        "avatarSync": avatar_sync,
        "gameplay": gameplay,
        "packageFlavor": package_mode_name(avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay),
        "targets": list(targets),
        "copiedArtifacts": copied_artifacts,
        "expectedArtifacts": copied_artifacts,
        "artifactSha256": {
            artifact: sha256(manifest_artifact_path(package, artifact)) for artifact in copied_artifacts
        },
        "sourceRevision": "0" * 40,
        "sourceProvenance": {
            "revision": "0" * 40,
            "sourceTreeSha256": "1" * 64,
            "dirty": False,
            "dirtyApproved": False,
        },
        "packageRoot": str(package),
        "packageSnapshotRoot": str(package),
        "stagedGameFiles": True,
        "papyrusCompiled": True,
        "papyrusCompiler": str(package / "Caprica.exe"),
        "companionPanel": True,
        "cefRuntime": None if dll_only else {
            "version": CEF_RUNTIME_VERSION,
            "files": list(CEF_RUNTIME_REQUIRED_FILES),
        },
        "generatedAtUtc": "2026-01-01T00:00:00.0000000Z",
    }
    path = package / "SkyrimTogetherVR_BuildManifest.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    manifest["packageFileSha256"] = package_file_hashes(package)
    path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def populate_test_package(package, avatar_sync=False, dll_only=False, gameplay=False):
    runtime_files = required_runtime_files(avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay)
    for relative_file in runtime_files:
        is_launcher = relative_file in launcher_runtime_files(
            avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay
        )
        write_x64_pe(package / relative_file, delay_imports=("libcef.dll",) if is_launcher else ())

    for relative_file in REQUIRED_STAGED_FILES:
        path = package / relative_file
        if path.name == "SkyrimTogetherVR_BuildManifest.json":
            continue
        if relative_file == "Data/SkyrimTogether.esp":
            source = pathlib.Path(__file__).resolve().parents[2] / "GameFiles" / "SkyrimVR" / "SkyrimTogether.esp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(source.read_bytes())
            continue
        elif relative_file == "Data/Seq/SkyrimTogether.seq":
            source = pathlib.Path(__file__).resolve().parents[2] / "GameFiles" / "SkyrimVR" / "Seq" / "SkyrimTogether.seq"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(source.read_bytes())
            continue
        if path.suffix.lower() == ".csv":
            write_csv(path)
        elif path.suffix.lower() == ".pex":
            write_file(path, papyrus_pex_fixture())
        elif relative_file.startswith("Tools/SkyrimVR/") and path.suffix.lower() == ".py":
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(python_helper_fixture(path), encoding="utf-8")
        else:
            write_file(path)
    write_build_manifest(package, avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay)


def run_self_test():
    with tempfile.TemporaryDirectory(prefix="stvr-built-package-audit-") as tmp:
        root = pathlib.Path(tmp)
        skyrim_vr = root / "SkyrimVR"

        default_package = root / "default"
        populate_test_package(default_package)
        failures, *_ = audit_package(default_package, skyrim_vr)
        if failures:
            print("Default package self-test unexpectedly failed:")
            for failure in failures:
                print(f"- {failure}")
            return 1

        write_file(skyrim_vr / "sksevr_1_4_15.dll")
        write_file(skyrim_vr / "Data" / "SKSE" / "Plugins" / "version-1-4-15-0.csv")
        for relative_path in REQUIRED_STAGED_FILES:
            if relative_path.startswith("Data/Scripts/") and relative_path.endswith(".pex"):
                source = default_package / relative_path
                destination = skyrim_vr / relative_path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(source.read_bytes())
        failures, *_ = audit_package(default_package, skyrim_vr, require_installed_prerequisites=True)
        if "target Skyrim VR install is missing sksevr_steam_loader.dll" not in failures:
            print("Package self-test did not require sksevr_steam_loader.dll when strict prerequisites were requested.")
            return 1

        write_file(skyrim_vr / "sksevr_steam_loader.dll")
        failures, *_ = audit_package(default_package, skyrim_vr, require_installed_prerequisites=True)
        if failures:
            print("Package self-test unexpectedly failed after installing strict SKSEVR prerequisites:")
            for failure in failures:
                print(f"- {failure}")
            return 1

        lowercase_scripts = skyrim_vr / "Data" / "scripts"
        lowercase_scripts.mkdir()
        failures, *_ = audit_package(default_package, skyrim_vr, require_installed_prerequisites=True)
        if not any("exactly one canonical Data/Scripts" in failure for failure in failures):
            print("Package self-test did not reject a target Data/Scripts case-fold collision.")
            return 1
        lowercase_scripts.rmdir()

        hash_fixture = skyrim_vr / "Data" / "Scripts" / "SkyrimTogetherUtils.pex"
        expected_hash_fixture = hash_fixture.read_bytes()
        hash_fixture.write_bytes(b"tampered")
        failures, *_ = audit_package(default_package, skyrim_vr, require_installed_prerequisites=True)
        if "target Papyrus hash mismatch: Data/Scripts/SkyrimTogetherUtils.pex" not in failures:
            print("Package self-test did not reject a target Papyrus hash mismatch.")
            return 1
        hash_fixture.write_bytes(expected_hash_fixture)

        eager_cef_package = root / "eager-cef"
        populate_test_package(eager_cef_package)
        write_x64_pe(eager_cef_package / "SkyrimTogetherVR.exe", normal_imports=("libcef.dll",))
        failures, *_ = audit_package(eager_cef_package, skyrim_vr)
        if "launcher package imports libcef.dll normally: SkyrimTogetherVR.exe" not in failures:
            print("Package self-test did not reject a normal CEF launcher import.")
            return 1

        missing_delay_cef_package = root / "missing-delay-cef"
        populate_test_package(missing_delay_cef_package)
        write_x64_pe(missing_delay_cef_package / "SkyrimTogetherVR.exe")
        failures, *_ = audit_package(missing_delay_cef_package, skyrim_vr)
        if "launcher package does not delay-import libcef.dll: SkyrimTogetherVR.exe" not in failures:
            print("Package self-test did not reject a missing CEF delay import.")
            return 1

        avatar_package = root / "avatar"
        populate_test_package(avatar_package, avatar_sync=True)
        failures, *_ = audit_package(avatar_package, skyrim_vr, avatar_sync=True)
        if failures:
            print("Avatar-sync package self-test unexpectedly failed:")
            for failure in failures:
                print(f"- {failure}")
            return 1

        gameplay_package = root / "gameplay"
        populate_test_package(gameplay_package, gameplay=True)
        failures, *_ = audit_package(gameplay_package, skyrim_vr, gameplay=True)
        if failures:
            print("Gameplay package self-test unexpectedly failed:")
            for failure in failures:
                print(f"- {failure}")
            return 1

        dll_package = root / "dll-only"
        populate_test_package(dll_package, dll_only=True)
        failures, *_ = audit_package(dll_package, skyrim_vr, dll_only=True)
        if failures:
            print("DLL-only package self-test unexpectedly failed:")
            for failure in failures:
                print(f"- {failure}")
            return 1

        stale_dll_package = root / "stale-dll-only"
        populate_test_package(stale_dll_package, dll_only=True)
        write_x64_pe(stale_dll_package / "SkyrimTogetherVR.exe")
        write_x64_pe(stale_dll_package / "TPProcess.exe")
        failures, *_ = audit_package(stale_dll_package, skyrim_vr, dll_only=True)
        if "forbidden package file present: SkyrimTogetherVR.exe" not in failures:
            print("DLL-only package self-test did not reject stale default launcher.")
            print("\n".join(failures))
            return 1
        if "forbidden package file present: TPProcess.exe" not in failures:
            print("DLL-only package self-test did not reject stale TPProcess runtime.")
            print("\n".join(failures))
            return 1

        wrong_dll_manifest_package = root / "wrong-dll-manifest"
        populate_test_package(wrong_dll_manifest_package)
        write_build_manifest(wrong_dll_manifest_package)
        failures, *_ = audit_package(wrong_dll_manifest_package, skyrim_vr, dll_only=True)
        if "dll-only build manifest has unexpected target(s): SkyrimTogetherVRClient, SkyrimVRImmersiveLauncher, TPProcess" not in failures:
            print("DLL-only package self-test did not reject full-package manifest targets.")
            print("\n".join(failures))
            return 1
        if "dll-only build manifest copiedArtifacts has unexpected runtime artifact(s): SkyrimTogetherVR.exe, TPProcess.exe" not in failures:
            print("DLL-only package self-test did not reject full-package manifest artifacts.")
            print("\n".join(failures))
            return 1

        stale_avatar_package = root / "stale-avatar"
        populate_test_package(stale_avatar_package)
        write_x64_pe(stale_avatar_package / "SkyrimTogetherVRAvatarSync.exe")
        failures, *_ = audit_package(stale_avatar_package, skyrim_vr)
        if "forbidden package file present: SkyrimTogetherVRAvatarSync.exe" not in failures:
            print("Default package self-test did not reject stale avatar-sync launcher.")
            print("\n".join(failures))
            return 1

        stale_papyrus_package = root / "stale-papyrus"
        populate_test_package(stale_papyrus_package)
        write_file(stale_papyrus_package / "Data" / "Scripts" / "SkyrimTogetherUtils.pex", b"ConnectToSkyrimTogether")
        failures, *_ = audit_package(stale_papyrus_package, skyrim_vr)
        if not any(
            failure.startswith(
                "packaged Papyrus bytecode is missing VR token(s): Data/Scripts/SkyrimTogetherUtils.pex:"
            )
            and "SetSkyrimTogetherConnectionConfig" in failure
            for failure in failures
        ):
            print("Package self-test did not reject stale SkyrimTogetherUtils.pex bytecode.")
            print("\n".join(failures))
            return 1

        stale_se_papyrus_package = root / "stale-se-papyrus"
        populate_test_package(stale_se_papyrus_package)
        write_file(
            stale_se_papyrus_package / "Data" / "Scripts" / "SkyrimTogetherVerifyLaunchScript.pex",
            b"SkyrimTogether.exe",
        )
        failures, *_ = audit_package(stale_se_papyrus_package, skyrim_vr)
        if not any(
            failure.startswith(
                "packaged Papyrus bytecode has stale SE token(s): Data/Scripts/SkyrimTogetherVerifyLaunchScript.pex:"
            )
            for failure in failures
        ):
            print("Package self-test did not reject stale SE Papyrus bytecode.")
            print("\n".join(failures))
            return 1

        stale_default_package = root / "stale-default"
        populate_test_package(stale_default_package, avatar_sync=True)
        write_x64_pe(stale_default_package / "SkyrimTogetherVR.exe")
        failures, *_ = audit_package(stale_default_package, skyrim_vr, avatar_sync=True)
        if "forbidden package file present: SkyrimTogetherVR.exe" not in failures:
            print("Avatar-sync package self-test did not reject stale default launcher.")
            print("\n".join(failures))
            return 1

        stale_gameplay_package = root / "stale-gameplay"
        populate_test_package(stale_gameplay_package, gameplay=True)
        write_x64_pe(stale_gameplay_package / "SkyrimTogetherVR.exe")
        write_x64_pe(stale_gameplay_package / "SkyrimTogetherVRAvatarSync.exe")
        failures, *_ = audit_package(stale_gameplay_package, skyrim_vr, gameplay=True)
        if "forbidden package file present: SkyrimTogetherVR.exe" not in failures:
            print("Gameplay package self-test did not reject stale default launcher.")
            print("\n".join(failures))
            return 1
        if "forbidden package file present: SkyrimTogetherVRAvatarSync.exe" not in failures:
            print("Gameplay package self-test did not reject stale avatar-sync launcher.")
            print("\n".join(failures))
            return 1

        root_staged_package = root / "root-staged"
        populate_test_package(root_staged_package)
        write_file(root_staged_package / "SkyrimTogether.esp")
        failures, *_ = audit_package(root_staged_package, skyrim_vr)
        if "forbidden package file present: SkyrimTogether.esp" not in failures:
            print("Package self-test did not reject root-level staged ESP.")
            print("\n".join(failures))
            return 1

        missing_data_package = root / "missing-data"
        populate_test_package(missing_data_package)
        (missing_data_package / "Data" / "SkyrimTogether.esp").unlink()
        failures, *_ = audit_package(missing_data_package, skyrim_vr)
        if "missing package file: Data/SkyrimTogether.esp" not in failures:
            print("Package self-test did not require Data/SkyrimTogether.esp.")
            print("\n".join(failures))
            return 1

        wrong_manifest_package = root / "wrong-manifest"
        populate_test_package(wrong_manifest_package, avatar_sync=True)
        write_build_manifest(wrong_manifest_package, avatar_sync=True)
        failures, *_ = audit_package(wrong_manifest_package, skyrim_vr)
        if "build manifest avatarSync does not match audit mode: True" not in failures:
            print("Package self-test did not reject wrong manifest mode.")
            print("\n".join(failures))
            return 1

        tampered_manifest_package = root / "tampered-manifest"
        populate_test_package(tampered_manifest_package)
        with (tampered_manifest_package / "SkyrimTogetherVR.exe").open("ab") as handle:
            handle.write(b"tampered")
        failures, *_ = audit_package(tampered_manifest_package, skyrim_vr)
        if "build manifest artifact SHA-256 mismatch: SkyrimTogetherVR.exe" not in failures:
            print("Package self-test did not reject a tampered runtime artifact.")
            print("\n".join(failures))
            return 1

        tampered_payload_package = root / "tampered-payload"
        populate_test_package(tampered_payload_package)
        with (tampered_payload_package / "Tools" / "SkyrimVR" / "vr_paths.py").open("a", encoding="utf-8") as handle:
            handle.write("\n# tampered\n")
        failures, *_ = audit_package(tampered_payload_package, skyrim_vr)
        if "build manifest package SHA-256 mismatch: Tools/SkyrimVR/vr_paths.py" not in failures:
            print("Package self-test did not reject a tampered staged helper.")
            print("\n".join(failures))
            return 1

        unavailable_source_package = root / "unavailable-source"
        populate_test_package(unavailable_source_package)
        manifest_path = unavailable_source_package / "SkyrimTogetherVR_BuildManifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["sourceRevision"] = "unavailable"
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        failures, *_ = audit_package(unavailable_source_package, skyrim_vr)
        if "build manifest sourceRevision does not match sourceProvenance" not in failures:
            print("Package self-test did not reject an unavailable source revision.")
            print("\n".join(failures))
            return 1

        unapproved_dirty_source_package = root / "unapproved-dirty-source"
        populate_test_package(unapproved_dirty_source_package)
        manifest_path = unapproved_dirty_source_package / "SkyrimTogetherVR_BuildManifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        source_provenance = manifest["sourceProvenance"]
        source_provenance["dirty"] = True
        source_provenance["dirtyApproved"] = False
        manifest["sourceRevision"] = f"{source_provenance['revision']}-dirty-{source_provenance['sourceTreeSha256']}"
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        failures, *_ = audit_package(unapproved_dirty_source_package, skyrim_vr)
        if "build manifest sourceProvenance has an unapproved dirty source tree" not in failures:
            print("Package self-test did not reject an unapproved dirty source tree.")
            print("\n".join(failures))
            return 1

        missing_helper_package = root / "missing-helper"
        populate_test_package(missing_helper_package)
        (missing_helper_package / "Tools" / "SkyrimVR" / "vr_paths.py").unlink()
        failures, *_ = audit_package(missing_helper_package, skyrim_vr)
        if "missing package Python helper: Tools/SkyrimVR/vr_paths.py" not in failures:
            print("Package self-test did not reject missing imported Python helper.")
            print("\n".join(failures))
            return 1

    print("Built package audit self-test passed")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true", help="run temporary package-layout self-tests")
    parser.add_argument(
        "--package",
        type=pathlib.Path,
        default=pathlib.Path("artifacts/SkyrimTogetherVR/releasedbg"),
        help="built package directory to audit",
    )
    parser.add_argument("--skyrim-vr", type=pathlib.Path, default=vr_paths.default_skyrim_vr_path(), help=vr_paths.skyrim_vr_help())
    parser.add_argument(
        "--avatar-sync",
        action="store_true",
        help="require the explicit remote-avatar validation executable instead of the default launcher",
    )
    parser.add_argument(
        "--dll-only",
        action="store_true",
        help="audit the DLL-producing partial package produced by BuildSkyrimTogetherVR-DLL-Windows.bat",
    )
    parser.add_argument(
        "--gameplay",
        action="store_true",
        help="require the full gameplay SkyrimTogetherVRGameplay.exe package",
    )
    parser.add_argument(
        "--require-installed-prerequisites",
        action="store_true",
        help="fail if the target Skyrim VR install is missing SKSEVR or the public VR Address Library CSV",
    )
    parser.add_argument(
        "--skip-installed-papyrus-verification",
        action="store_true",
        help="check target prerequisites without requiring its existing PEX files to match the package; intended for installer preflight",
    )
    parser.add_argument(
        "--require-vrik",
        action="store_true",
        help="fail if the target Skyrim VR install is missing Data/SKSE/Plugins/vrik.dll",
    )
    parser.add_argument(
        "--require-higgs",
        action="store_true",
        help="fail if the target Skyrim VR install is missing Data/SKSE/Plugins/higgs_vr.dll or higgs.dll",
    )
    parser.add_argument(
        "--require-planck",
        action="store_true",
        help="fail if the target Skyrim VR install is missing Data/SKSE/Plugins/activeragdoll.dll",
    )
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()

    selected_modes = sum(1 for selected in (args.avatar_sync, args.dll_only, args.gameplay) if selected)
    if selected_modes > 1:
        parser.error("--avatar-sync, --dll-only, and --gameplay cannot be combined")

    root = repo_root()
    package = (root / args.package).resolve() if not args.package.is_absolute() else args.package.resolve()
    skyrim_vr = args.skyrim_vr.resolve()
    failures, prereq, ae_to_se_rows, overrides_rows = audit_package(
        package,
        skyrim_vr,
        avatar_sync=args.avatar_sync,
        dll_only=args.dll_only,
        gameplay=args.gameplay,
        require_installed_prerequisites=args.require_installed_prerequisites,
        verify_installed_papyrus=not args.skip_installed_papyrus_verification,
        require_vrik=args.require_vrik,
        require_higgs=args.require_higgs,
        require_planck=args.require_planck,
    )

    print(f"Built package root: {package}")
    print(f"Package mode: {package_mode_name(avatar_sync=args.avatar_sync, dll_only=args.dll_only, gameplay=args.gameplay)}")
    print(f"AE-to-SE CSV rows: {ae_to_se_rows}")
    print(f"Address override CSV rows: {overrides_rows}")
    print(f"Installed SKSEVR DLL present: {prereq['sksevr']}")
    print(f"Installed SKSEVR Steam loader present: {prereq['sksevr_steam_loader']}")
    print(f"Installed VR Address Library CSV present: {prereq['address_library']}")
    print(f"Installed VRIK DLL present: {prereq['vrik']}")
    print(f"Installed HIGGS DLL present: {prereq['higgs']}")
    print(f"Installed PLANCK DLL present: {prereq['planck']}")
    print(f"Installed VRIK required files missing: {len(prereq['missing_vrik_files'])}")
    print(f"Installed HIGGS required files missing: {len(prereq['missing_higgs_files'])}")
    print(f"Installed PLANCK required files missing: {len(prereq['missing_planck_files'])}")
    print(f"Built package audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
