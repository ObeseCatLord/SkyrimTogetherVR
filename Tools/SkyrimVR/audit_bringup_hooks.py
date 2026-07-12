#!/usr/bin/env python3
import pathlib
import re
import sys


EXPECTED_VR_TARGET_CONFIGS = {
    "SkyrimTogetherVRClient": {
        "connection_only": True,
        "remote_avatar_sync": False,
        "remote_avatar_actor_targets": False,
    },
    "SkyrimTogetherVRClientAvatarSync": {
        "connection_only": True,
        "remote_avatar_sync": True,
        "remote_avatar_actor_targets": True,
    },
    "SkyrimTogetherVRGameplayClient": {
        "connection_only": False,
        "remote_avatar_sync": True,
        "remote_avatar_actor_targets": True,
    },
}

REQUIRED_VR_BUILD_FUNCTION_TOKENS = (
    'local connection_only = options.connection_only',
    "if connection_only == nil then",
    "connection_only = true",
    "local remote_avatar_sync = options.remote_avatar_sync or false",
    "local remote_avatar_actor_targets = options.remote_avatar_actor_targets or false",
    "local unvalidated_hooks = options.unvalidated_hooks or false",
    "local flat_overlay = options.flat_overlay or false",
    'add_defines("TP_SKYRIM_VR=1")',
    'add_defines("TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS=1")',
    'add_defines("TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=" .. vr_define_value(unvalidated_hooks))',
    'add_defines("TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY=" .. vr_define_value(flat_overlay))',
    'add_defines("TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=" .. vr_define_value(connection_only))',
    'add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=" .. vr_define_value(remote_avatar_sync))',
    'add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=" .. vr_define_value(remote_avatar_actor_targets))',
    'add_defines("TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES=0")',
)

REQUIRED_VR_OBSERVATION_SERVICE_DEFINES = (
    "TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE",
    "TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE",
    "TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE",
    "TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE",
    "TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE",
    "TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE",
    "TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE",
    "TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE",
    "TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE",
    "TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE",
    "TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE",
    "TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE",
)

FORBIDDEN_VR_TARGET_OPTIONS = (
    "unvalidated_hooks",
    "validated_inline_patches",
)

REQUIRED_TOKENS = {
    "Code/client/xmake.lua": (
        'add_defines("TP_SKYRIM_VR=1")',
        'add_defines("TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS=1")',
        'add_defines("TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=" .. vr_define_value(unvalidated_hooks))',
        'add_defines("TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=" .. vr_define_value(connection_only))',
        'add_defines("TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES=0")',
    ),
    "Code/client/main.cpp": (
        "SkyrimTogetherVR runtime flags: connectionOnly={}, bringupHooks={}, unvalidatedHooks={}, validatedInlinePatches={}",
        "Installing SkyrimTogetherVR startup/main-loop/render bring-up hooks",
        "SkyrimTogetherVR bring-up hooks are disabled at compile time",
        "SkyrimTogetherVR bring-up mode: skipping unvalidated Skyrim gameplay hooks",
        "BuildVRCompatibilityStatus",
        "WriteVRCompatibilityStatusFile",
        "PLANCK detected; keeping SkyrimTogetherVR in PLANCK/HIGGS-compatible hook mode",
        "HIGGS or PLANCK is installed; refusing to install unvalidated SkyrimTogetherVR gameplay hooks",
        "InstallVrMainLoopBringupHooks();",
        "BSGraphics::InstallVrRenderBringupHooks();",
        "SkyrimTogetherVR client startup hook reached",
    ),
    "Code/immersive_launcher/stubs/DllBlocklist.cpp": (
        "g_IsPlanckActive",
        "IsPlanck",
        'L"activeragdoll.dll"',
    ),
    "Code/immersive_launcher/stubs/FileMapping.cpp": (
        "stubs::IsPlanck(name)",
        "stubs::g_IsPlanckActive = true",
    ),
    "Code/client/TiltedOnlineApp.cpp": (
        "stubs::g_IsPlanckActive",
        "PLANCK SKSE plugin is loaded; active-ragdoll compatibility guard is active",
    ),
    "Code/client/SkyrimVM64.cpp": (
        "Installing SkyrimTogetherVR main-loop observer: mainLoop={}",
        "SkyrimTogetherVR main-loop observer reached",
        "SkyrimTogetherVR main-loop cadence: call={} elapsedMs={} thread={}",
        "s_mainLoopCallCount.fetch_add(1, std::memory_order_relaxed)",
        "callCount <= 2 || callCount % 300 == 0",
        "static void InstallVrMainLoopObserver()",
        "TP_HOOK(&MainLoop, HookMainLoop);",
        "void InstallVrMainLoopBringupHooks()",
    ),
    "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp": (
        "Installing SkyrimTogetherVR renderer bring-up hook: rendererInit={}",
        "SkyrimTogetherVR renderer bring-up hook skipped because renderer init address did not resolve",
        "SkyrimTogetherVR renderer init hook reached: self={}, osData={}, fbData={}",
        "SkyrimTogetherVR renderer init completed",
        "TP_HOOK_IMMEDIATE(&Renderer_Init, &Hook_Renderer_Init);",
    ),
    "Docs/SkyrimVR/connection-only-mode.md": (
        "Build-Time Guard",
        "effective VR client target configuration",
        "TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=0",
        "Expected First-Run Log Breadcrumbs",
        "SkyrimTogetherVR runtime flags:",
        "Installing SkyrimTogetherVR startup/main-loop/render bring-up hooks",
        "Installing SkyrimTogetherVR main-loop observer:",
        "SkyrimTogetherVR main-loop observer reached",
        "World::Update() is not called from a VR hook",
        "Installing SkyrimTogetherVR renderer bring-up hook:",
        "SkyrimTogetherVR renderer init hook reached:",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "runtime flag summary",
        "effective VR client target configuration",
        "explicit avatar-sync-only target configuration",
        "resolved main-loop observer address",
        "resolved renderer-init hook address",
        "Tools/SkyrimVR/audit_bringup_hooks.py",
    ),
}


FORBIDDEN_TOKENS = {
    "Code/client/xmake.lua": (
        'add_defines("TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=1")',
        'add_defines("TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES=1")',
    ),
}


def repo_root():
    return pathlib.Path(__file__).resolve().parents[2]


def parse_bool_option(options_text: str, key: str, default: bool) -> bool:
    match = re.search(rf"\b{re.escape(key)}\s*=\s*(true|false)", options_text)
    if not match:
        return default
    return match.group(1) == "true"


def parse_vr_target_configs(xmake_text: str) -> dict[str, dict[str, bool]]:
    pattern = re.compile(
        r'build_vr_client\("(?P<name>[^"]+)"(?:\s*,\s*\{(?P<options>.*?)\})?\s*\)',
        re.S,
    )
    configs: dict[str, dict[str, bool]] = {}
    for match in pattern.finditer(xmake_text):
        name = match.group("name")
        options = match.group("options") or ""
        configs[name] = {
            "connection_only": parse_bool_option(options, "connection_only", True),
            "remote_avatar_sync": parse_bool_option(options, "remote_avatar_sync", False),
            "remote_avatar_actor_targets": parse_bool_option(options, "remote_avatar_actor_targets", False),
        }
        for forbidden_option in FORBIDDEN_VR_TARGET_OPTIONS:
            if re.search(rf"\b{re.escape(forbidden_option)}\s*=", options):
                configs[name][forbidden_option] = True
    return configs


def audit_vr_target_configs(root: pathlib.Path) -> list[str]:
    path = root / "Code" / "client" / "xmake.lua"
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    failures: list[str] = []

    for token in REQUIRED_VR_BUILD_FUNCTION_TOKENS:
        if token not in text:
            failures.append(f"Code/client/xmake.lua: VR build function missing `{token}`")

    for define in REQUIRED_VR_OBSERVATION_SERVICE_DEFINES:
        token = f'add_defines("{define}=1")'
        if token not in text:
            failures.append(f"Code/client/xmake.lua: VR observation define missing `{token}`")

    configs = parse_vr_target_configs(text)
    missing_targets = sorted(set(EXPECTED_VR_TARGET_CONFIGS) - set(configs))
    extra_targets = sorted(set(configs) - set(EXPECTED_VR_TARGET_CONFIGS))
    if missing_targets:
        failures.append("Code/client/xmake.lua: missing VR client target config(s): " + ", ".join(missing_targets))
    if extra_targets:
        failures.append("Code/client/xmake.lua: unexpected VR client target config(s): " + ", ".join(extra_targets))

    for target, expected in EXPECTED_VR_TARGET_CONFIGS.items():
        actual = configs.get(target)
        if actual is None:
            continue
        for key, expected_value in expected.items():
            if actual.get(key) != expected_value:
                failures.append(
                    f"Code/client/xmake.lua: {target} effective {key}={actual.get(key)} expected {expected_value}"
                )
        for forbidden_option in FORBIDDEN_VR_TARGET_OPTIONS:
            if actual.get(forbidden_option):
                failures.append(f"Code/client/xmake.lua: {target} must not set {forbidden_option}")

    default_config = configs.get("SkyrimTogetherVRClient", {})
    if default_config.get("remote_avatar_actor_targets"):
        failures.append("Code/client/xmake.lua: default SkyrimTogetherVRClient must not enable actor target mutation")

    avatar_config = configs.get("SkyrimTogetherVRClientAvatarSync", {})
    if avatar_config.get("remote_avatar_actor_targets") and not avatar_config.get("remote_avatar_sync"):
        failures.append("Code/client/xmake.lua: avatar actor targets require remote_avatar_sync=true")

    return failures


def audit_vr_main_loop_observer(root: pathlib.Path) -> list[str]:
    path = root / "Code" / "client" / "SkyrimVM64.cpp"
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    failures: list[str] = []
    vr_block_start = text.find("#if TP_SKYRIM_VR")
    vr_block_end = text.find("#else", vr_block_start)
    vr_block = text[vr_block_start:vr_block_end] if vr_block_start >= 0 and vr_block_end >= 0 else ""

    for forbidden_token in (
        "VMContext",
        "TVMUpdate",
        "TVMDestructor",
        "HookVMUpdate",
        "HookVMDestructor",
        "TP_HOOK(&VMUpdate",
        "TP_HOOK(&VMDestructor",
        "g_appInstance->Update()",
    ):
        if forbidden_token in vr_block:
            failures.append(f"Code/client/SkyrimVM64.cpp: VR observer block must not contain `{forbidden_token}`")

    return failures


def main():
    root = repo_root()
    failures = []
    structural_failures = audit_vr_target_configs(root)
    failures.extend(structural_failures)
    observer_failures = audit_vr_main_loop_observer(root)
    failures.extend(observer_failures)

    for relative_path, tokens in REQUIRED_TOKENS.items():
        path = root / relative_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        for token in tokens:
            if token not in text:
                failures.append(f"{relative_path}: missing token `{token}`")

    for relative_path, tokens in FORBIDDEN_TOKENS.items():
        path = root / relative_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        for token in tokens:
            if token in text:
                failures.append(f"{relative_path}: forbidden token `{token}`")

    print(f"Audited bring-up files: {len(REQUIRED_TOKENS)}")
    print(f"VR target config failures: {len(structural_failures)}")
    print(f"VR main-loop observer failures: {len(observer_failures)}")
    print(f"Bring-up hook audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
