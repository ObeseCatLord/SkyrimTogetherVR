#!/usr/bin/env python3
"""Audit the SkyrimTogetherVR flat-overlay boundary."""

from __future__ import annotations

import pathlib


REQUIRED_TOKENS = {
    "Code/client/xmake.lua": (
        "if connection_only == nil then",
        "connection_only = true",
        "local flat_overlay = options.flat_overlay or false",
        'add_defines("TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=" .. vr_define_value(connection_only))',
        'add_defines("TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=" .. vr_define_value(unvalidated_hooks))',
        'add_defines("TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY=" .. vr_define_value(flat_overlay))',
        'add_defines("TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES=0")',
    ),
    "Code/client/TiltedOnlineApp.cpp": (
        "TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY",
        "flat D3D overlay/render startup is disabled for this VR target",
        "DirectInput overlay hooks are disabled for this VR target",
        "TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY || !TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY",
    ),
    "Code/client/main.cpp": (
        "SkyrimTogetherVR runtime flags: connectionOnly={}, bringupHooks={}, unvalidatedHooks={}, validatedInlinePatches={}",
        "SkyrimTogetherVR bring-up mode: skipping unvalidated Skyrim gameplay hooks",
        "InstallVrBringupHooks();",
    ),
    "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp": (
        "#if TP_SKYRIM_VR && !TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS",
        "SkyrimTogetherVR renderer init hook reached: self={}, osData={}, fbData={}",
        "Renderer_Init(self, aOSData, aFBData, aOut);",
        "SkyrimTogetherVR renderer init completed",
        "return;",
        "#else",
        "InputService::WndProc",
        "aOSData->pWndProc = Hook_WndProc;",
        "g_sRs = &World::Get().ctx().at<RenderSystemD3D11>();",
        "g_sRs->OnDeviceCreation",
        "#if !TP_SKYRIM_VR || TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS",
    ),
    "Code/client/Services/Generic/OverlayService.cpp": (
        "if (!m_pOverlay)\n        return;",
    ),
    "Code/client/Services/Generic/InputService.cpp": (
        "if (!s_pOverlay)\n        return 0;",
    ),
    "Code/client/Services/Generic/PartyService.cpp": (
        "if (auto* pOverlayService = m_world.ctx().find<OverlayService>())",
        "if (auto* pOverlay = pOverlayService->GetOverlayApp())",
    ),
    "Code/client/Services/Debug/DebugService.cpp": (
        "auto* pWindow = BSGraphics::GetMainWindow();",
        "if (!pWindow)\n        return;",
        "auto* pMainWindow = BSGraphics::GetMainWindow();",
        "if (!pMainWindow || !pMainWindow->IsForeground())",
    ),
    "Code/client/Services/Debug/Views/ComponentView.cpp": (
        "auto* pViewport = BSGraphics::GetMainWindow();",
        "if (!pViewport)\n        return false;",
    ),
    "Code/client/Games/Skyrim/BSInput/BSInputDeviceManager.cpp": (
        "auto* pMainWindow = BSGraphics::GetMainWindow();",
        "if (!pMainWindow || !pMainWindow->IsForeground())",
    ),
    "Tools/SkyrimVR/vr_handoff.py": (
        "render_panel_html",
        "command_serve",
        "/api/connect",
        "/api/disconnect",
        "Remote Players",
        "HIGGS",
    ),
    "GameFiles/SkyrimVR/Scripts/source/SkyrimTogetherVRConnectionMenu.psc": (
        "ShowStatus",
        "ShowTelemetry",
        "Debug.MessageBox",
        "ToggleConfigured",
    ),
}

DOC_TOKENS = {
    "Docs/SkyrimVR/vr-overlay-boundary.md": (
        "Default Boundary",
        "Connection-only",
        "flat CEF/D3D11 overlay",
        "no-op flat overlay service",
        "Renderer Init",
        "Companion",
        "Papyrus",
    ),
    "Docs/SkyrimVR/connection-only-mode.md": (
        "flat D3D11 overlay/render startup",
        "DirectInput overlay toggles",
        "local browser companion panel",
        "Debug.MessageBox",
    ),
    "Docs/SkyrimVR/vr-handoff-bridge.md": (
        "does not inject input hooks",
        "render into the VR swap chain",
        "browser companion panel",
        "Debug.MessageBox",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "audit_vr_overlay_boundary.py",
        "flat CEF/D3D11 overlay",
    ),
}

CONNECTION_ONLY_FORBIDDEN = (
    "ctx().emplace<OverlayService>",
    "ctx().emplace<InputService>",
    "ctx().emplace<DebugService>",
    "ctx().emplace<RenderSystemD3D11>",
    "BehaviorVar::Get()->Init();",
)

RENDERER_VR_BRANCH_FORBIDDEN = (
    "InputService::WndProc",
    "aOSData->pWndProc = Hook_WndProc;",
    "g_sRs = &World::Get().ctx().at<RenderSystemD3D11>();",
    "g_sRs->OnDeviceCreation",
)

DINPUT_VR_DISABLED_BRANCH_FORBIDDEN = (
    "DInputHook::Install",
    "SetToggleKeys",
)


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def read_text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def missing_tokens(root: pathlib.Path, mapping: dict[str, tuple[str, ...]]) -> dict[str, list[str]]:
    missing: dict[str, list[str]] = {}
    for relative_path, tokens in mapping.items():
        path = root / relative_path
        if not path.exists():
            missing[relative_path] = ["<missing file>"]
            continue
        text = read_text(path)
        file_missing = [token for token in tokens if token not in text]
        if file_missing:
            missing[relative_path] = file_missing
    return missing


def extract_block(text: str, start: str, end: str) -> str:
    start_index = text.find(start)
    if start_index < 0:
        return ""
    end_index = text.find(end, start_index)
    if end_index < 0:
        return text[start_index:]
    return text[start_index:end_index]


def format_token_map(mapping: dict[str, list[str]]) -> str:
    lines: list[str] = []
    for relative_path, tokens in sorted(mapping.items()):
        lines.append(f"- {relative_path}")
        lines.extend(f"  - {token}" for token in tokens)
    return "\n".join(lines)


def main() -> int:
    root = repo_root()
    missing_required = missing_tokens(root, REQUIRED_TOKENS)
    missing_docs = missing_tokens(root, DOC_TOKENS)

    world_text = read_text(root / "Code/client/World.cpp")
    connection_block = extract_block(
        world_text,
        "#if TP_SKYRIM_VR && TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY",
        "#else",
    )
    missing_connection_block = []
    if not connection_block:
        missing_connection_block.append("connection-only preprocessor block")
    forbidden_connection = [token for token in CONNECTION_ONLY_FORBIDDEN if token in connection_block]

    renderer_text = read_text(root / "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp")
    main_text = read_text(root / "Code/client/main.cpp")
    renderer_vr_block = extract_block(
        renderer_text,
        "#if TP_SKYRIM_VR && !TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS",
        "#else",
    )
    missing_renderer_block = []
    if not renderer_vr_block:
        missing_renderer_block.append("renderer VR early-return block")
    forbidden_renderer = [token for token in RENDERER_VR_BRANCH_FORBIDDEN if token in renderer_vr_block]
    if "BSGraphics::InstallVrRenderBringupHooks();" in main_text:
        forbidden_renderer.append("default main path installs renderer bring-up hook")

    app_text = read_text(root / "Code/client/TiltedOnlineApp.cpp")
    install_hooks_block = extract_block(
        app_text,
        "void TiltedOnlineApp::InstallHooks2()",
        "void TiltedOnlineApp::UninstallHooks()",
    )
    dinput_vr_disabled_block = extract_block(
        install_hooks_block,
        "#if TP_SKYRIM_VR && (TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY || !TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY)",
        "#else",
    )
    missing_dinput_block = []
    if not install_hooks_block:
        missing_dinput_block.append("InstallHooks2 function block")
    if not dinput_vr_disabled_block:
        missing_dinput_block.append("DirectInput VR disabled branch")
    forbidden_dinput = [token for token in DINPUT_VR_DISABLED_BRANCH_FORBIDDEN if token in dinput_vr_disabled_block]

    print(f"Missing overlay-boundary source tokens: {sum(len(values) for values in missing_required.values())}")
    print(f"Missing overlay-boundary doc tokens: {sum(len(values) for values in missing_docs.values())}")
    print(f"Missing connection-only block checks: {len(missing_connection_block)}")
    print(f"Forbidden connection-only overlay tokens: {len(forbidden_connection)}")
    print(f"Missing renderer VR block checks: {len(missing_renderer_block)}")
    print(f"Forbidden renderer VR overlay tokens: {len(forbidden_renderer)}")
    print(f"Missing DirectInput VR disabled block checks: {len(missing_dinput_block)}")
    print(f"Forbidden DirectInput VR disabled tokens: {len(forbidden_dinput)}")

    if missing_required:
        print("\nMissing source tokens:")
        print(format_token_map(missing_required))
    if missing_docs:
        print("\nMissing doc tokens:")
        print(format_token_map(missing_docs))
    if missing_connection_block:
        print("\nMissing connection-only block:")
        for failure in missing_connection_block:
            print(f"- {failure}")
    if forbidden_connection:
        print("\nForbidden connection-only overlay tokens:")
        for token in forbidden_connection:
            print(f"- {token}")
    if missing_renderer_block:
        print("\nMissing renderer VR block:")
        for failure in missing_renderer_block:
            print(f"- {failure}")
    if forbidden_renderer:
        print("\nForbidden renderer VR overlay tokens:")
        for token in forbidden_renderer:
            print(f"- {token}")
    if missing_dinput_block:
        print("\nMissing DirectInput VR disabled block:")
        for failure in missing_dinput_block:
            print(f"- {failure}")
    if forbidden_dinput:
        print("\nForbidden DirectInput VR disabled tokens:")
        for token in forbidden_dinput:
            print(f"- {token}")

    return 1 if (
        missing_required
        or missing_docs
        or missing_connection_block
        or forbidden_connection
        or missing_renderer_block
        or forbidden_renderer
        or missing_dinput_block
        or forbidden_dinput
    ) else 0


if __name__ == "__main__":
    raise SystemExit(main())
