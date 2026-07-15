#!/usr/bin/env python3
import pathlib
import re
import sys


VR_MAIN_DRAW_ADDRESS_ID = "35560"
VR_MAIN_DRAW_RVA = "5b9330"
VR_WIN_MAIN_ADDRESS_ID = "35545"
VR_WIN_MAIN_RVA = "5b4290"


EXPECTED_VR_TARGET_CONFIGS = {
    "SkyrimTogetherVRClient": {
        "connection_only": True,
        "remote_avatar_sync": False,
        "remote_avatar_actor_targets": False,
        "observation_services": False,
        "pose_service": False,
        "remote_player_proxy": False,
    },
    "SkyrimTogetherVRClientAvatarSync": {
        "connection_only": True,
        "remote_avatar_sync": True,
        "remote_avatar_actor_targets": True,
        "observation_services": True,
        "pose_service": True,
        "remote_player_proxy": True,
    },
    "SkyrimTogetherVRGameplayClient": {
        "connection_only": False,
        "remote_avatar_sync": True,
        "remote_avatar_actor_targets": True,
        "observation_services": True,
        "pose_service": True,
        "remote_player_proxy": True,
    },
}

REQUIRED_VR_BUILD_FUNCTION_TOKENS = (
    'local connection_only = options.connection_only',
    "if connection_only == nil then",
    "connection_only = true",
    "local remote_avatar_sync = options.remote_avatar_sync or false",
    "local remote_avatar_actor_targets = options.remote_avatar_actor_targets or false",
    "local observation_services = options.observation_services or false",
    "local pose_service = options.pose_service or false",
    "local remote_player_proxy = options.remote_player_proxy or false",
    "local unvalidated_hooks = options.unvalidated_hooks or false",
    "local flat_overlay = options.flat_overlay or false",
    'add_defines("TP_SKYRIM_VR=1")',
    'add_defines("TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS=1")',
    'add_defines("TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=" .. vr_define_value(unvalidated_hooks))',
    'add_defines("TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY=" .. vr_define_value(flat_overlay))',
    'add_defines("TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=" .. vr_define_value(connection_only))',
    'add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=" .. vr_define_value(remote_avatar_sync))',
    'add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=" .. vr_define_value(remote_avatar_actor_targets))',
    'add_defines("TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE=" .. vr_define_value(pose_service))',
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

ALWAYS_ENABLED_VR_SERVICE_DEFINES = {
    "TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE",
    "TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE",
}

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
        "Installing SkyrimTogetherVR deferred startup/update-owner hook",
        "SkyrimTogetherVR bring-up hooks are disabled at compile time",
        "SkyrimTogetherVR bring-up mode: skipping unvalidated Skyrim gameplay hooks",
        "BuildVRCompatibilityStatus",
        "WriteVRCompatibilityStatusFile",
        "PLANCK detected; keeping SkyrimTogetherVR in PLANCK/HIGGS-compatible hook mode",
        "HIGGS or PLANCK is installed; refusing to install unvalidated SkyrimTogetherVR gameplay hooks",
        "InstallVrMainLoopBringupHooks();",
        "Installing SkyrimTogetherVR WinMain lifecycle hook:",
        "TP_HOOK(&s_vrWinMain, HookVrWinMain);",
        "~ShutdownGuard() { RunTiltedEnd(); }",
        "g_appInstance->EndMain();",
        "SkyrimTogetherVR client startup hook reached",
        "SkyrimTogetherVR::TickBridge::Retire();",
        "ValidateVrDefaultHookContracts",
        "Skyrim VR 1.4.15 prologue mismatch",
        "0x5B4290",
        "0x5B9330",
    ),
    "Code/client/VRCompatibilityStatus.h": (
        "bool PoseService{false};",
        "bool BodyPoseCapture{false};",
        "bool FbtInstalled{false};",
        "bool FbtLoaded{false};",
    ),
    "Code/client/VRCompatibilityStatus.cpp": (
        "TP_SKYRIM_VR_ENABLE_POSE_SERVICE",
        "status.PoseService = TP_SKYRIM_VR_ENABLE_POSE_SERVICE != 0;",
        "status.BodyPoseCapture = TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE != 0;",
        'file << "fbtPolicy=local_post_higgs_capture_and_network_cache_only',
        'file << "posePolicy=" << GetObservationPolicy(acStatus.PoseService)',
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
        "POINTER_SKYRIMSE(void, winMain, 35545);",
        "connection-only mode: Discord SDK callbacks are disabled",
        "World::Destroy();",
    ),
    "Code/client/SkyrimVM64.cpp": (
        "Installing SkyrimTogetherVR Main::Draw owner observer: mainDraw=0x{:X}",
        "SkyrimTogetherVR Main::Draw observer reached:",
        "SkyrimTogetherVR Main::Draw cadence:",
        "SkyrimTogetherVR VR update-owner runtime mode: {}",
        "RunTiltedApp();",
        "std::call_once(s_observerActivationOnce, []() { SkyrimTogetherVR::TickBridge::Activate(); });",
        "SkyrimTogetherVR::TickBridge::TryConsumeUpdatePermit(sequence)",
        "s_clientUpdateInProgress.test_and_set(std::memory_order_acquire)",
        "SkyrimTogetherVR::TickBridge::RecordOwnerHeartbeat()",
        "SkyrimTogetherVR::TickBridge::RecordOwnerUpdateCompleted(sequence)",
        "static void InstallVrMainDrawObserver()",
        "TP_HOOK(&MainDraw, HookMainDraw);",
        "void InstallVrMainLoopBringupHooks()",
    ),
    "Code/client/Services/Generic/VRLifecycleService.cpp": (
        'constexpr char kStatusFileName[] = "SkyrimTogetherVR.lifecycle"',
        'static const BSFixedString s_raceSexMenu("RaceSex Menu")',
        'static const BSFixedString s_loadingMenu("Loading Menu")',
        'static const BSFixedString s_faderMenu("Fader Menu")',
        "TryGetReadablePlayerForVR()",
        "m_loadInvalidated.exchange(false, std::memory_order_acq_rel)",
        'Suspend("load_event", true)',
        "m_stableTickCount >= kRequiredStableTicks",
        "stableFor >= kRequiredStableDuration",
        "load-event sink is disabled until BSTEventSource::AddEventSink has an exact VR target",
    ),
    "Code/client/World.cpp": (
        "ctx().emplace<VRLifecycleService>(*this)",
        "lifecycle.Update(cDeltaSeconds)",
        "HandleLifecycleBoundary()",
        "if (!lifecycle.IsReady())",
        "kMaximumUpdateDeltaSeconds = 0.25",
        "std::clamp(cDeltaSeconds, 0.0, kMaximumUpdateDeltaSeconds)",
        "SkyrimTogetherVR could not construct the client world:",
        "m_transport.Close();",
        "entt::locator<World>::reset();",
    ),
    "Code/client/Services/Generic/VRConnectionService.cpp": (
        'SetStatus("waiting_for_gameplay")',
        "lifecycle.GetEpoch() != lifecycleEpoch",
        "discarded a stale queued connect from lifecycle epoch",
        'SetStatus(m_hasPendingCommand ? "waiting_for_gameplay" : "offline")',
        'file << "lifecycleState="',
        'file << "lifecycleEpoch="',
    ),
    "Code/client/Services/Generic/TransportService.cpp": (
        "m_world.ctx().at<VRLifecycleService>().IsReady()",
        "The stable lifecycle gate owns player/cell readiness",
    ),
    "Code/immersive_launcher/Launcher.cpp": (
        'std::strcmp(aArgv[i], "--vm-update-mode") == 0',
        'SetEnvironmentVariableA("STVR_VM_UPDATE_MODE", pMode)',
        'std::strcmp(pMode, "observe")',
        'std::strcmp(pMode, "active")',
        "could not install the mapped-game CRT startup hooks",
    ),
    "Code/client/Games/Skyrim/Events/EventDispacther.cpp": (
        "blocked an unvalidated VR BSTEventSource::AddEventSink request",
        "blocked an unvalidated VR BSTEventSource::RemoveEventSink request",
    ),
    "Libraries/TiltedReverse/Code/reverse/src/FunctionHook.cpp": (
        "rolling back all hooks after a delayed MinHook failure",
        "for (auto it = m_installedHooks.rbegin(); it != m_installedHooks.rend(); ++it)",
    ),
    "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp": (
        "#if !TP_SKYRIM_VR || TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS",
    ),
    "Docs/SkyrimVR/connection-only-mode.md": (
        "Build-Time Guard",
        "effective VR client target configuration",
        "TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=0",
        "Expected First-Run Log Breadcrumbs",
        "SkyrimTogetherVR runtime flags:",
        "Installing SkyrimTogetherVR deferred startup/update-owner hook",
        "Installing SkyrimTogetherVR Main::Draw owner observer:",
        "SkyrimTogetherVR Main::Draw observer reached:",
        "STVR_VM_UPDATE_MODE",
        "never executes client or engine work",
        "SkyrimTogetherVR.lifecycle",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "runtime flag summary",
        "effective VR client target configuration",
        "explicit avatar-sync-only target configuration",
        "resolved Main::Draw owner address",
        "Tools/SkyrimVR/audit_bringup_hooks.py",
    ),
}


FORBIDDEN_TOKENS = {
    "Code/client/xmake.lua": (
        'add_defines("TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=1")',
        'add_defines("TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES=1")',
    ),
    "Code/client/main.cpp": (
        "BSGraphics::InstallVrRenderBringupHooks();",
        "RunTiltedApp();\n    struct ShutdownGuard",
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
            "observation_services": parse_bool_option(options, "observation_services", False),
            "pose_service": parse_bool_option(options, "pose_service", False),
            "remote_player_proxy": parse_bool_option(options, "remote_player_proxy", False),
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
        if define in ALWAYS_ENABLED_VR_SERVICE_DEFINES:
            token = f'add_defines("{define}=1")'
        elif define == "TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE":
            token = f'add_defines("{define}=" .. vr_define_value(remote_player_proxy))'
        else:
            token = f'add_defines("{define}=" .. vr_define_value(observation_services))'
        if token not in text:
            failures.append(f"Code/client/xmake.lua: VR observation define missing `{token}`")

    pose_token = 'add_defines("TP_SKYRIM_VR_ENABLE_POSE_SERVICE=" .. vr_define_value(pose_service))'
    if pose_token not in text:
        failures.append(f"Code/client/xmake.lua: VR pose define missing `{pose_token}`")

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
    for option in ("observation_services", "pose_service", "remote_player_proxy"):
        if default_config.get(option):
            failures.append(f"Code/client/xmake.lua: default SkyrimTogetherVRClient must keep {option}=false")

    avatar_config = configs.get("SkyrimTogetherVRClientAvatarSync", {})
    if avatar_config.get("remote_avatar_actor_targets") and not avatar_config.get("remote_avatar_sync"):
        failures.append("Code/client/xmake.lua: avatar actor targets require remote_avatar_sync=true")

    return failures


def audit_vr_update_owner(root: pathlib.Path) -> list[str]:
    path = root / "Code" / "client" / "SkyrimVM64.cpp"
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    failures: list[str] = []
    vr_block_start = text.find("#if TP_SKYRIM_VR")
    vr_block_end = text.find("#else", vr_block_start)
    vr_block = text[vr_block_start:vr_block_end] if vr_block_start >= 0 and vr_block_end >= 0 else ""

    for required_token in (
        "TP_THIS_FUNCTION(TMainDraw, void, Main, std::uint32_t, bool);",
        "void TP_MAKE_THISCALL(HookMainDraw, Main, std::uint32_t aUnk, bool aMainMenuOpen)",
        "POINTER_SKYRIMSE(TMainDraw, cMainDraw, 35560);",
        "STVR_VM_UPDATE_MODE",
        "TryConsumeUpdatePermit",
        "GetActivationThreadId",
        "VrUpdateMode::Observe",
        "VrUpdateMode::Active",
        "TiltedPhoques::ThisCall(MainDraw, apThis, aUnk, aMainMenuOpen);",
        "RunTiltedApp();",
        "s_observerActivationOnce",
        "s_clientUpdateInProgress.test_and_set",
        "beforeConsume.Ready",
        "beforeUpdate.Ready",
    ):
        if required_token not in vr_block:
            failures.append(f"Code/client/SkyrimVM64.cpp: VR observer block must contain `{required_token}`")

    for forbidden_token in (
        "TP_THIS_FUNCTION(TVrVmUpdate, int",
        "int TP_MAKE_THISCALL(HookVrVmUpdate",
        "compare_exchange_strong",
        "s_vmUpdateOwnerFault",
        "VMContext",
        "inactive",
        "TVMDestructor",
        "HookVMDestructor",
        "TP_HOOK(&VMDestructor",
        "POINTER_SKYRIMSE(TMainLoop",
        "TP_HOOK(&MainLoop",
        "HookMainLoop",
        "36564",
        "::ConsumeUpdatePermit()",
        "VrVmUpdate",
        "HookVrVmUpdate",
        "53926",
    ):
        if forbidden_token in vr_block:
            failures.append(f"Code/client/SkyrimVM64.cpp: opaque VR observer block must not contain `{forbidden_token}`")

    tick_bridge_path = root / "Code" / "client" / "VRTickBridge.cpp"
    tick_bridge_text = tick_bridge_path.read_text(encoding="utf-8", errors="replace") if tick_bridge_path.exists() else ""
    for forbidden_token in ("g_appInstance->Update()", "World::Get().Update()", "TiltedOnlineApp.h"):
        if forbidden_token in tick_bridge_text:
            failures.append(f"Code/client/VRTickBridge.cpp: task callback bridge must not contain `{forbidden_token}`")

    return failures


def audit_vr_owner_addresses(root: pathlib.Path) -> list[str]:
    path = root / "GameFiles" / "SkyrimVR" / "Data" / "SKSE" / "Plugins" / "SkyrimTogetherVR_AddressOverrides.csv"
    failures: list[str] = []
    rows: dict[str, list[tuple[int, list[str]]]] = {
        VR_MAIN_DRAW_ADDRESS_ID: [],
        VR_WIN_MAIN_ADDRESS_ID: [],
    }
    if path.exists():
        for line_number, raw_line in enumerate(path.read_text(encoding="utf-8-sig", errors="replace").splitlines(), 1):
            columns = [column.strip() for column in raw_line.split(",")]
            if columns and columns[0] in rows:
                rows[columns[0]].append((line_number, columns))

    expected = {
        VR_MAIN_DRAW_ADDRESS_ID: (VR_MAIN_DRAW_RVA, ("database", "exact_vr_database"), "Main::Draw"),
        VR_WIN_MAIN_ADDRESS_ID: (VR_WIN_MAIN_RVA, ("database", "exact_vr_database"), "WinMain"),
    }
    for address_id, (expected_rva, expected_provenance, target_name) in expected.items():
        matches = rows[address_id]
        if len(matches) != 1:
            failures.append(f"{path.relative_to(root)}: expected exactly one address ID {address_id} row, found {len(matches)}")
            continue
        line_number, columns = matches[0]
        actual_rva_text = columns[1].lower().removeprefix("0x") if len(columns) > 1 else ""
        try:
            actual_rva = int(actual_rva_text, 16)
        except ValueError:
            actual_rva = -1
        if actual_rva != int(expected_rva, 16):
            failures.append(
                f"{path.relative_to(root)}:{line_number}: address ID {address_id} must map to Skyrim VR "
                f"{target_name} RVA 0x{expected_rva}, found 0x{actual_rva_text or 'missing'}"
            )
        actual_provenance = tuple(columns[2:4]) if len(columns) >= 4 else ()
        if actual_provenance != expected_provenance:
            failures.append(
                f"{path.relative_to(root)}:{line_number}: address ID {address_id} must record provenance "
                f"{expected_provenance}, found {actual_provenance}"
            )

    override_ids = {
        columns[0]
        for raw_line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines()
        if (columns := [column.strip() for column in raw_line.split(",")]) and columns[0].isdigit()
    } if path.exists() else set()
    alias_path = path.with_name("SkyrimTogetherVR_AE_to_SE.csv")
    if alias_path.exists():
        for line_number, raw_line in enumerate(alias_path.read_text(encoding="utf-8-sig", errors="replace").splitlines(), 1):
            columns = [column.strip() for column in raw_line.split(",")]
            if len(columns) >= 2 and columns[0].isdigit() and columns[1].isdigit() and columns[1] in override_ids:
                failures.append(
                    f"{alias_path.relative_to(root)}:{line_number}: alias target {columns[1]} collides with an explicit project override"
                )

    return failures


def audit_commonlib_abi_contracts(root: pathlib.Path) -> list[str]:
    failures: list[str] = []
    pch_path = root / "Code" / "vr_gameplay_bridge" / "pch.h"
    pch_text = pch_path.read_text(encoding="utf-8", errors="replace") if pch_path.exists() else ""
    include_positions = {
        header: pch_text.find(f"#include <{header}>")
        for header in ("SKSE/SKSE.h", "RE/Skyrim.h", "Windows.h")
    }
    for header, position in include_positions.items():
        if position < 0:
            failures.append(f"Code/vr_gameplay_bridge/pch.h: missing `<{header}>` include")
    windows_position = include_positions["Windows.h"]
    commonlib_positions = (include_positions["SKSE/SKSE.h"], include_positions["RE/Skyrim.h"])
    if windows_position >= 0 and all(position >= 0 for position in commonlib_positions):
        if windows_position < max(commonlib_positions):
            failures.append(
                "Code/vr_gameplay_bridge/pch.h: Windows.h must be included after CommonLib headers"
            )

    for path in (root / "Code").rglob("*"):
        if path.suffix.lower() not in {".h", ".hpp", ".cpp", ".cxx"}:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if re.search(r"^\s*#\s*define\s+SPDLOG_WCHAR_FILENAMES\b", text, re.MULTILINE):
            failures.append(
                f"{path.relative_to(root)}: manually enables SPDLOG_WCHAR_FILENAMES outside the package ABI"
            )

    runtime_header = root / "Code" / "vr_common" / "VRGameplayBridge.h"
    runtime_text = runtime_header.read_text(encoding="utf-8", errors="replace") if runtime_header.exists() else ""
    for token in (
        "kSkyrimVrRuntimeVersion = 0x010400F0",
        "kSkseVrInterfaceRuntimeVersion = 0x010400F1",
        "kMinimumSkseVrVersion = 0x020000C0",
        "kMinimumSkseVrReleaseIndex = 60",
    ):
        if token not in runtime_text:
            failures.append(f"Code/vr_common/VRGameplayBridge.h: missing distinct VR runtime contract `{token}`")

    bridge_main = root / "Code" / "vr_gameplay_bridge" / "main.cpp"
    bridge_text = bridge_main.read_text(encoding="utf-8", errors="replace") if bridge_main.exists() else ""
    if "interfaceRuntime.pack() != SkyrimTogetherVR::GameplayBridge::kSkseVrInterfaceRuntimeVersion" not in bridge_text:
        failures.append(
            "Code/vr_gameplay_bridge/main.cpp: must validate SKSEVR's 1.4.15.1 interface version, not CommonLib's 1.4.15.0 executable version"
        )
    init_position = bridge_text.find("SKSE::Init(a_skse);")
    runtime_gate_position = bridge_text.find("interfaceRuntime.pack() != SkyrimTogetherVR::GameplayBridge::kSkseVrInterfaceRuntimeVersion")
    if init_position < 0 or runtime_gate_position < 0 or init_position < runtime_gate_position:
        failures.append(
            "Code/vr_gameplay_bridge/main.cpp: must validate the SKSEVR loader contract before CommonLib initialization"
        )
    for token in (
        "skseVersion < SkyrimTogetherVR::GameplayBridge::kMinimumSkseVrVersion",
        "releaseIndex < SkyrimTogetherVR::GameplayBridge::kMinimumSkseVrReleaseIndex",
    ):
        if token not in bridge_text:
            failures.append(f"Code/vr_gameplay_bridge/main.cpp: missing SKSEVR 2.0.12 loader gate `{token}`")

    return failures


def main():
    root = repo_root()
    failures = []
    structural_failures = audit_vr_target_configs(root)
    failures.extend(structural_failures)
    observer_failures = audit_vr_update_owner(root)
    failures.extend(observer_failures)
    vm_address_failures = audit_vr_owner_addresses(root)
    failures.extend(vm_address_failures)
    commonlib_abi_failures = audit_commonlib_abi_contracts(root)
    failures.extend(commonlib_abi_failures)

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
    print(f"VR update-owner failures: {len(observer_failures)}")
    print(f"VR owner address failures: {len(vm_address_failures)}")
    print(f"CommonLib ABI contract failures: {len(commonlib_abi_failures)}")
    print(f"Bring-up hook audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
