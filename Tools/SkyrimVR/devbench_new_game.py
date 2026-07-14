#!/usr/bin/env python3
"""Drive Skyrim VR from Main Menu to Realm of Lorkhan through DevBench.

The host keyboard input is used only to select New Game and request RaceSex
confirmation. DevBench provides state checks and invokes the confirmation callback,
so the script never relies on fixed sleeps alone.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from collections.abc import Callable

import vr_paths


KEY_END = 107
KEY_ENTER = 28
KEY_R = 19
KEY_P = 25

GAME_PROCESS: subprocess.Popen | None = None

SAFE_MESSAGEBOX_RULES = (
    {
        "id": "realm_lorkhan_intro",
        "body_tokens": ("someplace unknown", "outside of time and space"),
        "button": "begin",
        "single_button": True,
    },
)

TASK_SEQUENCE_PATTERN = re.compile(
    r"\btask_run=\d+\s+sequence=(\d+)\s+dispatch_result=0\b"
)


def normalize_messagebox_text(value: object) -> str:
    return " ".join(str(value or "").split()).casefold()


class AutomationError(RuntimeError):
    pass


def post_tool(base_url: str, tool: str, payload: dict, timeout: float = 5.0) -> dict:
    request = urllib.request.Request(
        f"{base_url.rstrip('/')}/api/tool/{tool}",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.load(response)
    except (OSError, urllib.error.URLError, json.JSONDecodeError) as exc:
        raise AutomationError(f"DevBench {tool} request failed: {exc}") from exc


def wait_until(
    description: str,
    timeout: float,
    poll: Callable[[], object],
    predicate: Callable[[object], bool],
    *,
    on_wait: Callable[[], None] | None = None,
):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        if GAME_PROCESS is not None and GAME_PROCESS.poll() is not None:
            raise AutomationError(
                f"game launcher exited with status {GAME_PROCESS.returncode} "
                f"while waiting for {description}"
            )
        if on_wait is not None:
            on_wait()
        try:
            last = poll()
            if predicate(last):
                return last
        except AutomationError:
            pass
        time.sleep(0.25)
    raise AutomationError(f"timed out waiting for {description}; last state: {last}")


def focus_window(window_search: str) -> None:
    result = subprocess.run(
        ["kdotool", "search", window_search, "windowactivate", "%1"],
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise AutomationError(f"could not focus Skyrim window with kdotool: {detail}")


def tap_key(ydotool: pathlib.Path, socket: pathlib.Path, key: int, delay: float = 0.5) -> None:
    env = os.environ.copy()
    env["YDOTOOL_SOCKET"] = str(socket)
    for state in (1, 0):
        result = subprocess.run(
            [str(ydotool), "key", f"{key}:{state}"],
            env=env,
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip()
            raise AutomationError(f"ydotool key {key}:{state} failed: {detail}")
        # Controller input must remain pressed across at least one OpenXR sync.
        time.sleep(delay)
    time.sleep(delay)


def ensure_ydotoold(
    ydotoold: pathlib.Path, socket: pathlib.Path
) -> subprocess.Popen | None:
    if socket.exists():
        return None
    socket.parent.mkdir(parents=True, exist_ok=True)
    daemon = subprocess.Popen(
        [
            str(ydotoold),
            f"--socket-path={socket}",
            "--socket-perm=0600",
            f"--socket-own={os.getuid()}:{os.getgid()}",
        ],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    wait_until(
        "ydotoold socket",
        5.0,
        lambda: socket.exists(),
        bool,
    )
    return daemon


def menu_state(base_url: str) -> dict:
    return post_tool(base_url, "menu", {"action": "list"})


def allowlisted_messagebox_index(description: dict, context: str) -> tuple[int, str] | None:
    body = normalize_messagebox_text(description.get("bodyText", ""))
    buttons = [normalize_messagebox_text(button) for button in description.get("buttons", [])]
    cancel_index = description.get("cancelIndex")

    for rule in SAFE_MESSAGEBOX_RULES:
        body_tokens = [normalize_messagebox_text(token) for token in rule["body_tokens"]]
        if not body or not all(token in body for token in body_tokens):
            continue
        button = normalize_messagebox_text(rule["button"])
        if button not in buttons:
            continue
        if rule.get("single_button") and len(buttons) != 1:
            continue
        index = buttons.index(button)
        if cancel_index == index:
            continue
        return index, rule["id"]

    if context == "racesex_confirmation" and buttons:
        first_button = buttons[0]
        if cancel_index != 0 and first_button in {"ok", "yes", "accept", "done", "finish"}:
            return 0, "racesex_confirmation"

    return None


def handle_blocking_message_box(base_url: str, context: str) -> bool:
    state = menu_state(base_url)
    if "MessageBoxMenu" not in state.get("openMenus", []):
        return False

    description = post_tool(base_url, "menu", {"action": "describe"})
    if not description.get("messageBoxOpen"):
        return False

    choice = allowlisted_messagebox_index(description, context)
    buttons = [normalize_messagebox_text(button) for button in description.get("buttons", [])]
    if choice is None:
        raise AutomationError(
            "Blocking MessageBoxMenu with unsupported text and buttons: "
            f"body={description.get('bodyText')!r}, buttons={buttons}, cancelIndex={description.get('cancelIndex')}, context={context}"
        )

    index, rule_id = choice
    if context != "racesex_confirmation":
        if index < 0 or index >= len(buttons):
            raise AutomationError(f"Computed invalid MessageBoxMenu index {index} for buttons={buttons}")
        post_tool(base_url, "menu", {"action": "accept", "index": index})
        print(
            f"Auto-accepted safe MessageBoxMenu ({rule_id}) during {context}: "
            f"button={buttons[index]!r}"
        )
        return True

    return False


def drain_blocking_message_boxes(
    base_url: str, context: str, timeout: float = 10.0
) -> None:
    deadline = time.monotonic() + timeout
    consecutive_closed_polls = 0
    while time.monotonic() < deadline:
        accepted = handle_blocking_message_box(base_url, context)
        state = menu_state(base_url)
        if "MessageBoxMenu" in state.get("openMenus", []) or state.get("messageBoxOpen"):
            consecutive_closed_polls = 0
        elif accepted:
            consecutive_closed_polls = 0
        else:
            consecutive_closed_polls += 1
            if consecutive_closed_polls >= 2:
                return
        time.sleep(0.25)
    raise AutomationError(f"timed out draining MessageBoxMenu during {context}")


def successful_task_sequences(path: pathlib.Path, start_offset: int = 0) -> list[int]:
    if not path.is_file():
        return []
    with path.open("rb") as handle:
        size = handle.seek(0, os.SEEK_END)
        handle.seek(start_offset if start_offset <= size else 0)
        text = handle.read().decode("utf-8", errors="replace")
    return [int(match.group(1)) for match in TASK_SEQUENCE_PATTERN.finditer(text)]


def wait_for_resumed_cadence(
    path: pathlib.Path,
    baseline_sequence: int,
    timeout: float,
    *,
    start_offset: int = 0,
    on_wait: Callable[[], None] | None = None,
) -> tuple[int, int]:
    def newer_sequences() -> list[int]:
        return sorted(
            {
                sequence
                for sequence in successful_task_sequences(path, start_offset)
                if sequence > baseline_sequence
            }
        )

    sequences = wait_until(
        "two successful post-modal Skyrim Together task ticks",
        timeout,
        newer_sequences,
        lambda value: len(value) >= 2,
        on_wait=on_wait,
    )
    return sequences[0], sequences[1]


def advance_to_main_menu(base_url: str) -> dict:
    state = menu_state(base_url)
    if "CalibrationOptionMenu" in state.get("openMenus", []):
        post_tool(base_url, "menu", {"action": "close", "name": "CalibrationOptionMenu"})
    return state


def read_status(path: pathlib.Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    values = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, separator, value = line.partition("=")
        if separator:
            values[key.strip()] = value.strip()
    return values


def status_int(values: dict[str, str], key: str) -> int:
    try:
        return int(values.get(key, "0"))
    except ValueError:
        return 0


def finalization_identity(scene: dict, player: dict) -> tuple[str, str, str, str]:
    race = player.get("race") or {}
    cell = scene.get("cell") or {}
    return (
        str(player.get("name") or ""),
        str(race.get("formId") or race.get("editorId") or ""),
        str(cell.get("formId") or ""),
        str(cell.get("editorId") or ""),
    )


def main() -> int:
    global GAME_PROCESS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:8921", help="DevBench REST base URL")
    parser.add_argument(
        "--window-search",
        default="^Skyrim VR$",
        help="kdotool window title regular expression for the Skyrim VR window",
    )
    parser.add_argument(
        "--monado-window-search",
        default="^Monado!$",
        help="Monado Qwerty debug window that receives simulated controller input",
    )
    parser.add_argument("--timeout", type=float, default=120.0, help="timeout for each major game state")
    parser.add_argument("--skyrim-vr", type=pathlib.Path, default=vr_paths.default_skyrim_vr_path())
    parser.add_argument("--connect", metavar="HOST:PORT", help="write one connect command after player finalization")
    parser.add_argument(
        "--load-save",
        metavar="SAVE_STEM",
        help="load a deterministic save authored after valid character finalization instead of selecting New Game",
    )
    parser.add_argument(
        "--vm-update-mode",
        choices=("off", "observe", "active"),
        default="observe",
        help="select the runtime VM-update hook mode; connection verification requires active",
    )
    parser.add_argument(
        "--require-exterior-grid",
        action="store_true",
        help="after connecting, require an exterior cell and grid sync instead of accepting the current interior cell",
    )
    parser.add_argument(
        "--launch-game",
        action="store_true",
        help="start launch-skyrim-together-vr.sh after creating the persistent virtual keyboard",
    )
    parser.add_argument(
        "--ydotool",
        type=pathlib.Path,
        default=pathlib.Path.home() / ".local/bin/ydotool",
    )
    parser.add_argument(
        "--ydotoold",
        type=pathlib.Path,
        default=pathlib.Path.home() / ".local/bin/ydotoold",
    )
    parser.add_argument(
        "--ydotool-socket",
        type=pathlib.Path,
        default=pathlib.Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
        / "stvr-ydotool.sock",
    )
    args = parser.parse_args()

    if args.connect and not args.launch_game:
        raise AutomationError(
            "--connect verification requires --launch-game so session and request proof comes from a fresh process"
        )
    if args.connect and args.vm_update_mode != "active":
        raise AutomationError("--connect requires --vm-update-mode active after the observer gate has passed")

    for executable in (args.ydotool, args.ydotoold):
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise AutomationError(f"required executable is missing: {executable}")
    if shutil.which("kdotool") is None:
        raise AutomationError("kdotool is required")

    ensure_ydotoold(args.ydotoold, args.ydotool_socket)
    print(f"Persistent virtual keyboard ready: {args.ydotool_socket}", flush=True)

    handoff_dir = args.skyrim_vr / "Data" / "SkyrimTogetherReborn"
    command_path = handoff_dir / "SkyrimTogetherVR.command"
    status_path = handoff_dir / "SkyrimTogetherVR.status"
    lifecycle_path = handoff_dir / "SkyrimTogetherVR.lifecycle"
    player_cell_path = handoff_dir / "SkyrimTogetherVR.playercell"
    tick_bridge_log_path = args.skyrim_vr / "Data" / "SKSE" / "Plugins" / "SkyrimTogetherVRTickBridge.log"
    tick_bridge_log_start_offset = (
        tick_bridge_log_path.stat().st_size if tick_bridge_log_path.is_file() else 0
    )
    if args.connect:
        for stale_proof in (command_path, status_path, lifecycle_path, player_cell_path):
            stale_proof.unlink(missing_ok=True)
    run_started_ns = time.time_ns()

    if args.launch_game:
        launcher = args.skyrim_vr / "launch-skyrim-together-vr.sh"
        if not launcher.is_file() or not os.access(launcher, os.X_OK):
            raise AutomationError(f"game launcher is missing or not executable: {launcher}")
        launch_env = os.environ.copy()
        launch_env.pop("STVR_AUTOCONNECT", None)
        launch_env.pop("STVR_PASSWORD", None)
        launch_env["STVR_DISABLE_AUTOCONNECT"] = "1"
        launch_env["STVR_FORCE_PROTON"] = "1"
        launch_env["STVR_VM_UPDATE_MODE"] = args.vm_update_mode
        log_path = args.skyrim_vr / "stvr-devbench-launch.log"
        launch_log = log_path.open("ab")
        try:
            GAME_PROCESS = subprocess.Popen(
                [str(launcher)],
                cwd=args.skyrim_vr,
                env=launch_env,
                stdin=subprocess.DEVNULL,
                stdout=launch_log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        finally:
            launch_log.close()
        print(f"Launched Skyrim Together VR; log: {log_path}", flush=True)

    state = wait_until(
        "Main Menu or RaceSex Menu",
        args.timeout,
        lambda: advance_to_main_menu(args.url),
        lambda value: any(
            name in value.get("openMenus", [])
            for name in ("Main Menu", "RaceSex Menu")
        ),
    )
    open_menus = state.get("openMenus", [])
    print(f"Initial menu ready: {open_menus}", flush=True)

    if args.load_save:
        if "Main Menu" not in open_menus or "RaceSex Menu" in open_menus:
            raise AutomationError(
                "--load-save requires a clean Main Menu; refusing to load through an incomplete RaceSex transaction"
            )
        load_result = post_tool(args.url, "game", {"action": "load", "name": args.load_save})
        print(f"Queued deterministic post-character save: {load_result.get('name', args.load_save)}")
        wait_until(
            "deterministic save to leave Main Menu without RaceSex",
            args.timeout,
            lambda: menu_state(args.url),
            lambda value: "Main Menu" not in value.get("openMenus", [])
            and "RaceSex Menu" not in value.get("openMenus", []),
            on_wait=lambda: handle_blocking_message_box(args.url, "load_save"),
        )
    else:
        if "RaceSex Menu" not in open_menus:
            focus_window(args.window_search)
            # Skyrim VR's Scaleform main-menu entries are indexed bottom-to-top:
            # Home selects Quit, while End selects the top New entry.
            tap_key(args.ydotool, args.ydotool_socket, KEY_END)
            tap_key(args.ydotool, args.ydotool_socket, KEY_ENTER)
            time.sleep(1.0)

            # Skyrim VR's modded-new-game confirmation is not a MessageBoxMenu.
            # The patched Monado Qwerty driver maps P to the focused controller trigger.
            focus_window(args.monado_window_search)
            tap_key(args.ydotool, args.ydotool_socket, KEY_P, delay=0.5)
            focus_window(args.window_search)

            race_state = wait_until(
                "RaceSex Menu after selecting New Game",
                args.timeout,
                lambda: menu_state(args.url),
                lambda value: "RaceSex Menu" in value.get("openMenus", []),
            )
        else:
            race_state = state
        print(f"RaceSex ready: {race_state.get('openMenus', [])}")

        focus_window(args.window_search)
        tap_key(args.ydotool, args.ydotool_socket, KEY_R)

        wait_until(
            "RaceSex confirmation",
            10.0,
            lambda: menu_state(args.url),
            lambda value: value.get("messageBoxOpen") is True,
        )
        description = post_tool(args.url, "menu", {"action": "describe"})
        if allowlisted_messagebox_index(description, "racesex_confirmation") is None:
            raise AutomationError(f"RaceSex confirmation does not default to acceptance: {description}")

        # Route a real controller trigger through Monado. The dialog closing is not
        # enough: the RaceSex menu itself must close through Skyrim's name/finalize
        # transaction before any client readiness or connection check is allowed.
        focus_window(args.monado_window_search)
        tap_key(args.ydotool, args.ydotool_socket, KEY_P)
        focus_window(args.window_search)
        print(f"Activated RaceSex confirmation through controller input: {description['buttons'][0]}")

        wait_until(
            "RaceSex confirmation dialog to close after controller activation",
            15.0,
            lambda: menu_state(args.url),
            lambda value: value.get("messageBoxOpen") is False,
        )
        try:
            wait_until(
                "RaceSex Menu to close through vanilla finalization",
                15.0,
                lambda: menu_state(args.url),
                lambda value: "RaceSex Menu" not in value.get("openMenus", []),
            )
        except AutomationError as exc:
            raise AutomationError(
                "RaceSex confirmation did not complete Skyrim's name/finalization transaction. "
                "Do not force-close the menu or pump native ticks; finish with a working VR "
                "keyboard/controller path or load a valid post-character save."
            ) from exc

    scene = wait_until(
        "Realm of Lorkhan player placement",
        args.timeout,
        lambda: post_tool(args.url, "inspect", {"kind": "scene"}),
        lambda value: value.get("playerLoaded") is True
        and value.get("cell", {}).get("editorId") == "RealmLorkhan",
        on_wait=lambda: handle_blocking_message_box(args.url, "finalization"),
    )
    mods = post_tool(args.url, "inspect", {"kind": "mods"})
    names = [entry.get("name") for entry in mods.get("plugins", [])]
    if "SkyrimTogether.esp" not in names:
        raise AutomationError(f"SkyrimTogether.esp is not active: {names}")
    player = post_tool(args.url, "inspect", {"kind": "player"})
    if not player.get("name") or not player.get("race"):
        raise AutomationError(f"player finalization is incomplete: {player}")

    lifecycle: dict[str, str] = {}
    player_cell: dict[str, str] = {}
    session_id = ""
    if args.vm_update_mode == "active":
        lifecycle = wait_until(
            "Skyrim Together stable gameplay lifecycle",
            args.timeout,
            lambda: read_status(lifecycle_path),
            lambda value: value.get("state") == "ready"
            and value.get("ready") == "1"
            and value.get("epoch") not in {None, "", "0"}
            and value.get("ownerThreadId") not in {None, "", "0"}
            and value.get("playerFormId") not in {None, "", "0"}
            and value.get("playerCellFormId") not in {None, "", "0"},
            on_wait=lambda: handle_blocking_message_box(args.url, "finalization"),
        )
        if lifecycle_path.stat().st_mtime_ns < run_started_ns:
            raise AutomationError("lifecycle readiness proof predates this automation run")

        player_cell = wait_until(
            "Skyrim Together player readiness",
            args.timeout,
            lambda: read_status(player_cell_path),
            lambda value: value.get("ready") == "1"
            and value.get("playerFormId") not in {None, "", "0"},
            on_wait=lambda: handle_blocking_message_box(args.url, "finalization"),
        )
        if player_cell_path.stat().st_mtime_ns < run_started_ns:
            raise AutomationError("player readiness proof predates this automation run")
        session_id = player_cell.get("sessionId", "")
        if session_id in {"", "0"}:
            raise AutomationError(f"player readiness proof has no process session ID: {player_cell}")
        if player_cell.get("lifecycleEpoch") != lifecycle.get("epoch"):
            raise AutomationError(
                "player-cell readiness does not belong to the current lifecycle epoch: "
                f"lifecycle={lifecycle.get('epoch')} playercell={player_cell.get('lifecycleEpoch')}"
            )
    first_identity = finalization_identity(scene, player)
    time.sleep(1.0)
    stable_scene = post_tool(args.url, "inspect", {"kind": "scene"})
    stable_player = post_tool(args.url, "inspect", {"kind": "player"})
    if finalization_identity(stable_scene, stable_player) != first_identity:
        raise AutomationError(
            "player identity or Realm cell changed during finalization stability check: "
            f"{first_identity} -> {finalization_identity(stable_scene, stable_player)}"
        )

    print(f"Realm of Lorkhan ready at {scene.get('position')}")
    print(f"Player finalized: {player.get('name')} ({player.get('race')})")
    print("SkyrimTogether.esp is active")

    if args.connect:
        def maintain_connection_cadence() -> None:
            handle_blocking_message_box(args.url, "connect_wait")

        existing_sequences = successful_task_sequences(
            tick_bridge_log_path, tick_bridge_log_start_offset
        )
        cadence_baseline = max(existing_sequences, default=0)
        drain_blocking_message_boxes(args.url, "pre-connect")
        resumed_sequences = wait_for_resumed_cadence(
            tick_bridge_log_path,
            cadence_baseline,
            min(args.timeout, 20.0),
            start_offset=tick_bridge_log_start_offset,
            on_wait=maintain_connection_cadence,
        )
        print(
            "Skyrim Together cadence resumed after modal drain: "
            f"sequences={resumed_sequences[0]},{resumed_sequences[1]}"
        )
        baseline_status = read_status(status_path)
        baseline_cell = read_status(player_cell_path)
        if baseline_cell.get("sessionId") != session_id:
            raise AutomationError("player-cell session changed before the connect command")
        baseline_generation = max(
            status_int(baseline_status, "connectionGeneration"),
            status_int(baseline_cell, "connectionGeneration"),
        )
        baseline_grid_count = status_int(baseline_cell, "gridCellRequestCount")
        baseline_exterior_count = status_int(baseline_cell, "exteriorCellRequestCount")
        baseline_interior_count = status_int(baseline_cell, "interiorCellRequestCount")

        handoff_dir.mkdir(parents=True, exist_ok=True)
        pending_path = command_path.with_suffix(".command.tmp")
        pending_path.write_text(f"action=connect\nendpoint={args.connect}\npassword=\n", encoding="utf-8")
        pending_path.replace(command_path)
        status = wait_until(
            "Skyrim Together online status",
            args.timeout,
            lambda: read_status(status_path),
            lambda value: value.get("state") == "online"
            and value.get("online") == "1"
            and value.get("playerId") not in {None, "", "0"}
            and value.get("sessionId") == session_id
            and status_int(value, "connectionGeneration") > baseline_generation,
            on_wait=maintain_connection_cadence,
        )
        if status_path.stat().st_mtime_ns < run_started_ns:
            raise AutomationError("online status proof predates this automation run")
        def cell_sync_ready(value: dict[str, str]) -> bool:
            common_ready = (
                value.get("ready") == "1"
                and value.get("online") == "1"
                and value.get("sessionId") == session_id
                and value.get("localPlayerId") == status.get("playerId")
                and value.get("connectionGeneration") == status.get("connectionGeneration")
                and value.get("lastCell.connectionGeneration") == status.get("connectionGeneration")
                and status_int(value, "worldSpaceTranslationFailureCount") == 0
                and value.get("lastCell.valid") == "1"
                and status_int(value, "lastCell.cell.serverBaseId") > 0
            )
            if not common_ready:
                return False
            if not args.require_exterior_grid:
                return (
                    status_int(value, "interiorCellRequestCount") > baseline_interior_count
                    or status_int(value, "exteriorCellRequestCount") > baseline_exterior_count
                )
            return (
                value.get("lastCell.exterior") == "1"
                and value.get("lastGrid.valid") == "1"
                and value.get("lastGrid.connectionGeneration") == status.get("connectionGeneration")
                and status_int(value, "gridCellRequestCount") > baseline_grid_count
                and status_int(value, "exteriorCellRequestCount") > baseline_exterior_count
                and status_int(value, "lastGrid.cellCount") > 0
                and status_int(value, "lastGrid.worldSpace.serverBaseId") > 0
                and status_int(value, "lastGrid.playerCell.serverBaseId") > 0
                and status_int(value, "lastCell.worldSpace.serverBaseId") > 0
            )

        cell_sync = wait_until(
            "Skyrim Together first cell synchronization",
            args.timeout,
            lambda: read_status(player_cell_path),
            cell_sync_ready,
            on_wait=maintain_connection_cadence,
        )
        print(f"Skyrim Together online as player {status['playerId']}")
        if args.require_exterior_grid:
            print(
                "Skyrim Together exterior grid sync complete: "
                f"grid={cell_sync['gridCellRequestCount']} "
                f"exterior={cell_sync['exteriorCellRequestCount']}"
            )
        else:
            print(
                "Skyrim Together current-cell sync complete: "
                f"interior={cell_sync['interiorCellRequestCount']} "
                f"exterior={cell_sync['exteriorCellRequestCount']}"
            )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AutomationError as exc:
        print(f"devbench-new-game: {exc}", file=sys.stderr)
        raise SystemExit(1)
