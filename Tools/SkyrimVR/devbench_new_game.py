#!/usr/bin/env python3
"""Drive Skyrim VR from Main Menu to Realm of Lorkhan through DevBench.

The host keyboard input is used only to select New Game. XRizer's direct
command-file input requests and accepts RaceSex confirmation. DevBench provides
state checks, so the script never relies on fixed sleeps alone.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import secrets
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from collections.abc import Callable

import vr_paths
import vr_handoff


STEAM_APP_ID = "611670"
WIN32_INPUT_HELPER_NAME = "win32_scancode_input.cpp"
WIN32_INPUT_CACHE_ENTRIES = 4
WIN32_INPUT_TIMEOUT = 15.0

XRIZER_INPUT_COMMANDS = frozenset({"menu", "trigger"})
XRIZER_INPUT_CONSUME_TIMEOUT = 5.0
TASK_RUN_CHECKPOINT_INTERVAL = 5.0
ASSIGNMENT_STABILITY_SCHEDULING_MARGIN = 2.0
ASSIGNMENT_STABILITY_WINDOW = (
    2 * TASK_RUN_CHECKPOINT_INTERVAL + ASSIGNMENT_STABILITY_SCHEDULING_MARGIN
)
GAMEPLAY_BRIDGE_LOG_RELATIVE_PATH = pathlib.Path(
    "drive_c/users/steamuser/Documents/My Games/Skyrim VR/SKSE/SkyrimTogetherVRGameplayBridge.log"
)
SKYRIM_VR_SAVES_RELATIVE_PATH = pathlib.Path(
    "drive_c/users/steamuser/Documents/My Games/Skyrim VR/Saves"
)
GAMEPLAY_BRIDGE_STARTUP_MARKER = "validated loader runtime="
LAUNCH_NONCE_PATTERN = re.compile(r"^[0-9a-f]{32}$")
RELEASE_ACTIVE_PLUGIN_ORDER = (
    "Skyrim.esm",
    "Update.esm",
    "Dawnguard.esm",
    "HearthFires.esm",
    "Dragonborn.esm",
    "SkyrimVR.esm",
    "higgs_vr.esp",
    "vrik.esp",
    "Realm of Lorkhan - Custom Alternate Start - Choose your own adventure.esp",
    "SkyrimTogether.esp",
)
# The release lane intentionally runs only the handoff-owned content.  A
# different active set is not a release-admission candidate, even if it works.
REJECT_UNEXPECTED_RELEASE_PLUGINS = True
USABLE_STABLE_MENU_BLOCKERS = frozenset(
    {"Main Menu", "RaceSex Menu", "Loading Menu", "Fader Menu", "MessageBoxMenu"}
)
USABLE_STABLE_MENU_MINIMUM_INTERVAL = 1.0
USABLE_STABLE_MENU_POLL_INTERVAL = 0.25

GAME_PROCESS: subprocess.Popen | None = None

SAFE_MESSAGEBOX_RULES = (
    {
        "id": "realm_lorkhan_intro",
        "body_tokens": ("someplace unknown", "outside of time and space"),
        "button": "begin",
        "single_button": True,
    },
)

MAIN_MENU_INPUT_BLOCKERS = frozenset(
    {
        "Fader Menu",
        "MessageBoxMenu",
        "CalibrationOptionMenu",
    }
)

TASK_SEQUENCE_PATTERN = re.compile(
    r"\btask_run=\d+\s+sequence=(\d+)\s+dispatch_result=0\b"
)


def normalize_messagebox_text(value: object) -> str:
    return " ".join(str(value or "").split()).casefold()


class AutomationError(RuntimeError):
    pass


class TerminalAutomationError(AutomationError):
    """A readout proves admission cannot succeed during this launch."""

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
        except TerminalAutomationError:
            raise
        except AutomationError:
            pass
        time.sleep(0.25)
    raise AutomationError(f"timed out waiting for {description}; last state: {last}")


def required_directory(path: pathlib.Path, description: str) -> pathlib.Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_dir():
        raise AutomationError(f"{description} is missing or not a directory: {resolved}")
    return resolved


def required_executable(path: pathlib.Path, description: str) -> pathlib.Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise AutomationError(f"{description} is missing or not executable: {resolved}")
    return resolved


def first_existing_directory(candidates: tuple[pathlib.Path, ...], description: str) -> pathlib.Path:
    for candidate in candidates:
        expanded = candidate.expanduser()
        if expanded.is_dir():
            return expanded.resolve()
    rendered = ", ".join(str(candidate) for candidate in candidates)
    raise AutomationError(f"could not find {description}; checked: {rendered}")


def find_ge_proton_dir(steam_root: pathlib.Path, override: pathlib.Path | None) -> pathlib.Path:
    if override is not None:
        proton_dir = required_directory(override, "--proton-path")
        required_executable(proton_dir / "proton", "Proton launcher")
        return proton_dir

    for search_root in (steam_root / "compatibilitytools.d", steam_root / "steamapps" / "common"):
        if not search_root.is_dir():
            continue
        children = tuple(
            path
            for path in search_root.iterdir()
            if path.is_dir() and (path / "proton").is_file() and os.access(path / "proton", os.X_OK)
        )
        for name_filter in (
            lambda name: name.casefold().startswith("ge-proton"),
            lambda name: name.casefold().startswith("proton-ge"),
            lambda name: True,
        ):
            candidates = sorted(
                (path for path in children if name_filter(path.name)),
                key=lambda path: [int(part) if part.isdigit() else part.casefold() for part in re.split(r"(\d+)", path.name)],
                reverse=True,
            )
            if candidates:
                return candidates[0].resolve()
    raise AutomationError(
        "could not find a GE-Proton installation with an executable proton launcher; "
        "set --proton-path or STVR_PROTONPATH"
    )


def resolve_wine_prefix(args: argparse.Namespace) -> pathlib.Path:
    prefix_override = args.wine_prefix or os.environ.get("STVR_WINEPREFIX")
    if prefix_override:
        return required_directory(pathlib.Path(prefix_override), "Wine prefix override")

    library_override = args.steam_library or os.environ.get("STVR_STEAM_LIBRARY")
    steam_library = (
        required_directory(pathlib.Path(library_override), "Steam library override")
        if library_override
        else required_directory(args.skyrim_vr.expanduser().resolve().parent.parent.parent, "Steam library from --skyrim-vr")
    )
    return required_directory(
        steam_library / "steamapps" / "compatdata" / STEAM_APP_ID / "pfx",
        f"Skyrim VR Proton prefix for AppID {STEAM_APP_ID}",
    )


def resolve_win32_input_route(args: argparse.Namespace) -> tuple[pathlib.Path, pathlib.Path]:
    wine64_override = args.wine64 or os.environ.get("STVR_WINE64")
    if wine64_override:
        wine64 = required_executable(pathlib.Path(wine64_override), "Wine64 override")
    else:
        steam_root_override = args.steam_root or os.environ.get("STVR_STEAM_ROOT")
        steam_root = (
            required_directory(pathlib.Path(steam_root_override), "Steam root override")
            if steam_root_override
            else first_existing_directory(
                (
                    pathlib.Path(os.environ.get("XDG_DATA_HOME", str(pathlib.Path.home() / ".local/share"))) / "Steam",
                    pathlib.Path.home() / ".steam" / "steam",
                    pathlib.Path.home() / ".steam" / "root",
                ),
                "Steam root",
            )
        )
        proton_override = args.proton_path or os.environ.get("STVR_PROTONPATH")
        proton_dir = find_ge_proton_dir(
            steam_root,
            pathlib.Path(proton_override) if proton_override else None,
        )
        wine64 = required_executable(proton_dir / "files" / "bin" / "wine64", "GE-Proton wine64")

    prefix = resolve_wine_prefix(args)
    return wine64, prefix


def bounded_win32_input_cache() -> pathlib.Path:
    cache_home = pathlib.Path(os.environ.get("XDG_CACHE_HOME", str(pathlib.Path.home() / ".cache")))
    cache_dir = cache_home / "skyrim-together-vr" / "win32-scancode-input"
    try:
        cache_dir.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise AutomationError(f"could not create Win32 input cache directory {cache_dir}: {exc}") from exc
    if not cache_dir.is_dir():
        raise AutomationError(f"Win32 input cache path is not a directory: {cache_dir}")
    return cache_dir


def prune_win32_input_cache(cache_dir: pathlib.Path, keep: pathlib.Path) -> None:
    entries = sorted(
        (
            path
            for path in cache_dir.glob("win32_scancode_input-*.exe")
            if path.is_file()
        ),
        key=lambda path: path.stat().st_mtime_ns,
        reverse=True,
    )
    retained = {keep}
    for entry in entries:
        if len(retained) >= WIN32_INPUT_CACHE_ENTRIES:
            try:
                entry.unlink()
            except OSError:
                pass
        else:
            retained.add(entry)


def compile_win32_input_helper() -> pathlib.Path:
    source = pathlib.Path(__file__).with_name(WIN32_INPUT_HELPER_NAME)
    if not source.is_file():
        raise AutomationError(f"Win32 scan-code helper source is missing: {source}")
    try:
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
    except OSError as exc:
        raise AutomationError(f"could not read Win32 scan-code helper source {source}: {exc}") from exc

    cache_dir = bounded_win32_input_cache()
    target = cache_dir / f"win32_scancode_input-{digest}.exe"
    if target.is_file():
        prune_win32_input_cache(cache_dir, target)
        return target

    compiler = shutil.which("x86_64-w64-mingw32-g++")
    if compiler is None:
        raise AutomationError(
            "x86_64-w64-mingw32-g++ is required to build the Win32 scan-code helper"
        )
    temporary = cache_dir / f".{target.name}.{os.getpid()}.{time.time_ns()}.tmp"
    try:
        result = subprocess.run(
            [compiler, "-std=c++17", "-O2", "-s", "-static", "-o", str(temporary), str(source)],
            text=True,
            capture_output=True,
            check=False,
        )
    except OSError as exc:
        raise AutomationError(f"could not start x86_64-w64-mingw32-g++: {exc}") from exc
    if result.returncode != 0:
        temporary.unlink(missing_ok=True)
        detail = (result.stderr.strip() or result.stdout.strip())[:4000]
        raise AutomationError(f"could not compile Win32 scan-code helper: {detail}")
    try:
        os.replace(temporary, target)
        target.chmod(0o700)
    except OSError as exc:
        temporary.unlink(missing_ok=True)
        raise AutomationError(f"could not publish Win32 scan-code helper {target}: {exc}") from exc
    prune_win32_input_cache(cache_dir, target)
    return target


def main_menu_new_game_input_mode(wine_prefix: pathlib.Path) -> str:
    """Choose the scan-code sequence from the active prefix's save roster."""
    saves_directory = wine_prefix / SKYRIM_VR_SAVES_RELATIVE_PATH
    try:
        has_saves = any(
            path.is_file() and path.suffix.casefold() == ".ess"
            for path in saves_directory.iterdir()
        )
    except FileNotFoundError:
        has_saves = False
    except OSError as exc:
        raise AutomationError(f"could not inspect Skyrim VR saves at {saves_directory}: {exc}") from exc
    return "--end-down-enter" if has_saves else "--end-enter"


def select_new_game_with_win32_scancodes(args: argparse.Namespace) -> None:
    helper = compile_win32_input_helper()
    wine64, prefix = resolve_win32_input_route(args)
    input_mode = main_menu_new_game_input_mode(prefix)
    env = os.environ.copy()
    env["WINEPREFIX"] = str(prefix)
    # Wine descendants can inherit stdout/stderr after the helper exits. A
    # captured pipe therefore waits forever for EOF even though the helper is
    # already a zombie. A regular temporary file preserves diagnostics without
    # coupling completion to descendant descriptor lifetime.
    # Proton's wine64 crashes when a PE helper inherits Linux O_TMPFILE as its
    # output descriptor. Keep this path-backed for Wine compatibility; the
    # context still removes it immediately after the bounded invocation.
    with tempfile.NamedTemporaryFile(
        mode="w+",
        encoding="utf-8",
        errors="replace",
        prefix="stvr-win32-input-",
        suffix=".log",
    ) as output:
        try:
            result = subprocess.run(
                [str(wine64), str(helper), input_mode],
                env=env,
                text=True,
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=WIN32_INPUT_TIMEOUT,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise AutomationError(
                f"Win32 scan-code Main Menu input timed out after {WIN32_INPUT_TIMEOUT:.0f}s"
            ) from exc
        if result.returncode != 0:
            output.seek(0)
            detail = output.read()[-4000:].strip() or f"exit status {result.returncode}"
            raise AutomationError(f"Win32 scan-code Main Menu input failed: {detail}")


def command_file_exists(path: pathlib.Path) -> bool:
    try:
        path.lstat()
    except FileNotFoundError:
        return False
    return True


def remove_stale_xrizer_input_command(path: pathlib.Path) -> None:
    """Remove one pre-launch XRizer command file without touching other paths."""
    try:
        mode = path.lstat().st_mode
    except FileNotFoundError:
        return
    if not stat.S_ISREG(mode):
        raise AutomationError(
            f"XRizer input command path exists but is not a regular file: {path}"
        )
    path.unlink()
    print(f"Removed stale XRizer input command: {path}", flush=True)


def publish_xrizer_input_command(
    path: pathlib.Path, command: str, timeout: float = XRIZER_INPUT_CONSUME_TIMEOUT
) -> None:
    """Atomically publish one supported XRizer command and await its consumption."""
    if command not in XRIZER_INPUT_COMMANDS:
        raise AutomationError(f"unsupported XRizer input command: {command!r}")
    payload = command.encode("ascii")
    if timeout <= 0:
        raise AutomationError(f"XRizer input command timeout must be positive: {timeout}")
    if not path.parent.is_dir():
        raise AutomationError(f"XRizer input command directory does not exist: {path.parent}")
    if command_file_exists(path):
        raise AutomationError(
            f"XRizer input command is still pending; refusing to overwrite it: {path}"
        )

    pending_path = path.with_name(f".{path.name}.{os.getpid()}.{time.time_ns()}.tmp")
    try:
        try:
            with pending_path.open("xb") as handle:
                if handle.write(payload) != len(payload):
                    raise OSError("short write while publishing XRizer input command")
                handle.flush()
                os.fsync(handle.fileno())
        except FileExistsError as exc:
            raise AutomationError(
                f"could not reserve XRizer input command temporary file: {pending_path}"
            ) from exc
        except OSError as exc:
            raise AutomationError(f"could not stage XRizer input command {command!r}: {exc}") from exc

        # link() publishes the complete file atomically and refuses to replace a
        # command created by another producer between the check above and here.
        try:
            os.link(pending_path, path)
        except FileExistsError as exc:
            raise AutomationError(
                f"XRizer input command appeared before publication; refusing to overwrite it: {path}"
            ) from exc
        except OSError as exc:
            raise AutomationError(f"could not publish XRizer input command {command!r}: {exc}") from exc
    finally:
        pending_path.unlink(missing_ok=True)

    print(f"Published XRizer direct controller command: {command}", flush=True)
    wait_until(
        f"XRizer to consume direct controller command {command!r}",
        timeout,
        lambda: command_file_exists(path),
        lambda exists: not exists,
    )


def menu_state(base_url: str) -> dict:
    return post_tool(base_url, "menu", {"action": "list"})


def is_menu_ready_for_input(state: dict, targets: tuple[str, ...]) -> bool:
    """Return whether an input target is open without a startup transition."""
    open_menus = state.get("openMenus", [])
    return (
        any(name in open_menus for name in targets)
        and state.get("messageBoxOpen") is not True
        and not any(
            name in MAIN_MENU_INPUT_BLOCKERS or "loading" in name.casefold()
            for name in open_menus
        )
    )


def complete_racesex_name_stage(
    post_confirmation_state: dict,
    *,
    state_reader: Callable[[], dict],
    trigger_publisher: Callable[[], None],
    timeout: float,
    wait_for_close: Callable[..., dict] = wait_until,
) -> dict:
    """Accept RaceSex's hidden default-name stage with one actionable pulse."""
    if "RaceSex Menu" not in post_confirmation_state.get("openMenus", []):
        return post_confirmation_state
    if not is_menu_ready_for_input(post_confirmation_state, ("RaceSex Menu",)):
        raise AutomationError(
            "RaceSex remained open after its visible confirmation, but the hidden "
            f"name stage is not actionable: {post_confirmation_state}"
        )

    trigger_publisher()
    return wait_for_close(
        "RaceSex Menu to close through vanilla name/finalization",
        timeout,
        state_reader,
        lambda value: "RaceSex Menu" not in value.get("openMenus", []),
    )


def drain_stale_realm_lorkhan_fader(
    *,
    state_reader: Callable[[], dict],
    fader_closer: Callable[[], None],
    timeout: float,
    wait_for_close: Callable[..., dict] = wait_until,
) -> dict:
    """Close only the proven stale HUD/Fader post-finalization state."""
    state = state_reader()
    open_menus = state.get("openMenus", [])
    if "Fader Menu" not in open_menus:
        return state
    if (
        state.get("messageBoxOpen") is True
        or len(open_menus) != 2
        or set(open_menus) != {"HUD Menu", "Fader Menu"}
    ):
        raise AutomationError(
            "Refusing to close Fader Menu from an unsafe post-finalization "
            f"menu state: {state}"
        )

    fader_closer()
    return wait_for_close(
        "stale RealmLorkhan Fader Menu to close",
        timeout,
        state_reader,
        lambda value: "Fader Menu" not in value.get("openMenus", []),
    )


def usable_stable_menu_state(state: dict) -> bool:
    """Accept only a post-load HUD state with no modal or transition menu."""
    return (
        "HUD Menu" in state.get("openMenus", [])
        and state.get("messageBoxOpen") is False
        and not USABLE_STABLE_MENU_BLOCKERS.intersection(state.get("openMenus", []))
    )


def wait_for_usable_stable_menu(
    *,
    state_reader: Callable[[], dict],
    fader_closer: Callable[[], object],
    timeout: float,
    wait_for_state: Callable[..., dict] = wait_until,
    monotonic: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
) -> dict:
    """Close a stale HUD/Fader pair, then require a sustained usable-menu interval."""
    drain_stale_realm_lorkhan_fader(
        state_reader=state_reader,
        fader_closer=fader_closer,
        timeout=timeout,
        wait_for_close=wait_for_state,
    )
    first = wait_for_state(
        "USABLE_STABLE menu state",
        timeout,
        state_reader,
        usable_stable_menu_state,
    )
    stable_since = monotonic()
    stability_deadline = stable_since + timeout

    while True:
        elapsed = monotonic() - stable_since
        if elapsed >= USABLE_STABLE_MENU_MINIMUM_INTERVAL:
            return first
        remaining = stability_deadline - monotonic()
        if remaining <= 0:
            raise AutomationError("timed out waiting for a sustained USABLE_STABLE menu state")
        sleep(
            min(
                USABLE_STABLE_MENU_POLL_INTERVAL,
                USABLE_STABLE_MENU_MINIMUM_INTERVAL - elapsed,
                remaining,
            )
        )
        current = state_reader()
        if usable_stable_menu_state(current):
            first = current
            continue

        # An observed modal or loading transition invalidates the preceding
        # interval.  Wait for a fresh usable state, then begin a new interval.
        remaining = stability_deadline - monotonic()
        if remaining <= 0:
            raise AutomationError("timed out waiting for a sustained USABLE_STABLE menu state")
        first = wait_for_state(
            "USABLE_STABLE menu state after transition",
            remaining,
            state_reader,
            usable_stable_menu_state,
        )
        stable_since = monotonic()


def accept_new_game_confirmation(args: argparse.Namespace) -> dict:
    """Reach RaceSex without firing a blind controller pulse during loading."""
    deadline = time.monotonic() + args.timeout
    next_attempt = 0.0
    attempts = 0
    last: dict = {}
    while time.monotonic() < deadline:
        if GAME_PROCESS is not None and GAME_PROCESS.poll() is not None:
            raise AutomationError(
                f"game launcher exited with status {GAME_PROCESS.returncode} "
                "while accepting New Game"
            )
        last = menu_state(args.url)
        if is_menu_ready_for_input(last, ("RaceSex Menu",)):
            return last

        now = time.monotonic()
        if now >= next_attempt and is_menu_ready_for_input(last, ("Main Menu",)):
            if attempts >= 4:
                raise AutomationError(
                    "New Game remained on an actionable Main Menu after four "
                    f"controller confirmation attempts; last state: {last}"
                )
            publish_xrizer_input_command(args.xrizer_input_command, "trigger")
            attempts += 1
            next_attempt = time.monotonic() + 3.0
        time.sleep(0.25)

    raise AutomationError(
        "timed out waiting for RaceSex after selecting New Game; "
        f"confirmation attempts={attempts}, last state: {last}"
    )


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
    value = values.get(key, "0")
    try:
        return int(value, 0)
    except ValueError:
        try:
            return int(value, 10)
        except ValueError:
            return 0


def status_is_zero(values: dict[str, str], key: str) -> bool:
    return key in values and status_int(values, key) == 0


def bounded_status_error(values: dict[str, str]) -> str:
    """Return the bridge's bounded diagnostic without echoing an unbounded readout."""
    detail = values.get("error", "")
    if not isinstance(detail, str):
        return "invalid error field"
    detail = " ".join(detail.split())
    return detail[:256] or "unspecified error"


def raise_for_nonce_bound_status_error(values: dict[str, str], launch_nonce: str) -> None:
    if values.get("launchNonce") == launch_nonce and values.get("state") == "error":
        raise TerminalAutomationError(
            "Skyrim Together connection entered state=error: " + bounded_status_error(values)
        )


def online_status_ready(values: dict[str, str], session_id: str, baseline_generation: int,
                        launch_nonce: str) -> bool:
    raise_for_nonce_bound_status_error(values, launch_nonce)
    return (
        values.get("state") == "online"
        and values.get("online") == "1"
        and values.get("playerId") not in {None, "", "0"}
        and values.get("sessionId") == session_id
        and values.get("launchNonce") == launch_nonce
        and values.get("clientVersion") not in {None, ""}
        and values.get("serverVersion") == values.get("clientVersion")
        and values.get("gameplayProtocolRevision") == str(vr_handoff.GAMEPLAY_PROTOCOL_REVISION)
        and status_int(values, "serverInstanceNonce") != 0
        and status_int(values, "connectionGeneration") > baseline_generation
    )


def file_prefix_sha256(path: pathlib.Path, byte_count: int) -> str:
    digest = hashlib.sha256()
    remaining = byte_count
    with path.open("rb") as handle:
        while remaining > 0:
            chunk = handle.read(min(remaining, 64 * 1024))
            if not chunk:
                raise AutomationError(
                    f"gameplay bridge log changed while its baseline was captured: {path}"
                )
            digest.update(chunk)
            remaining -= len(chunk)
    return digest.hexdigest()


def capture_gameplay_bridge_log_cursor(
    path: pathlib.Path,
) -> tuple[int, int, int, str] | None:
    """Capture the exact pre-launch log boundary used to ignore old entries."""
    try:
        metadata = path.stat()
    except FileNotFoundError:
        return None
    if not stat.S_ISREG(metadata.st_mode):
        raise AutomationError(f"gameplay bridge log is not a regular file: {path}")
    return (
        metadata.st_dev,
        metadata.st_ino,
        metadata.st_size,
        file_prefix_sha256(path, metadata.st_size),
    )


def read_current_gameplay_bridge_log_text(
    path: pathlib.Path,
    cursor: tuple[int, int, int, str] | None,
    run_started_ns: int,
) -> str:
    """Read current-launch log text without treating pre-launch output as current."""
    try:
        metadata = path.stat()
    except FileNotFoundError:
        return ""
    if not stat.S_ISREG(metadata.st_mode):
        raise AutomationError(f"gameplay bridge log is not a regular file: {path}")

    if cursor is None:
        if metadata.st_mtime_ns < run_started_ns:
            raise AutomationError(
                "gameplay bridge log predates this automation run; cannot attribute "
                f"its output to the current launch: {path}"
            )
        offset = 0
    else:
        device, inode, baseline_size, baseline_hash = cursor
        if metadata.st_dev == device and metadata.st_ino == inode:
            baseline_intact = metadata.st_size >= baseline_size and (
                file_prefix_sha256(path, baseline_size) == baseline_hash
            )
            if baseline_intact:
                if metadata.st_size > baseline_size and metadata.st_mtime_ns < run_started_ns:
                    raise AutomationError(
                        "gameplay bridge log append predates this automation run; cannot "
                        f"attribute its output to the current launch: {path}"
                    )
                offset = baseline_size
            elif metadata.st_mtime_ns >= run_started_ns:
                # CommonLib commonly truncates or replaces its log when the
                # plugin loads. The rewritten whole file is current-launch data.
                offset = 0
            else:
                raise AutomationError(
                    "gameplay bridge log was rewritten before this automation run; cannot "
                    f"attribute its output to the current launch: {path}"
                )
        elif metadata.st_mtime_ns >= run_started_ns:
            # A replacement log is current only when its timestamp proves it was
            # created after this automation started.
            offset = 0
        else:
            raise AutomationError(
                "gameplay bridge log replacement predates this automation run; cannot "
                f"attribute its output to the current launch: {path}"
            )

    try:
        with path.open("rb") as handle:
            handle.seek(offset)
            return handle.read().decode("utf-8", errors="replace")
    except OSError as exc:
        raise AutomationError(f"could not read gameplay bridge log {path}: {exc}") from exc


def new_gameplay_bridge_critical_entries(
    path: pathlib.Path,
    cursor: tuple[int, int, int, str] | None,
    run_started_ns: int,
) -> list[str]:
    return [
        line.strip()
        for line in read_current_gameplay_bridge_log_text(path, cursor, run_started_ns).splitlines()
        if "[critical]" in line.casefold()
    ]


def avatar_assignment_ready(
    avatar: dict[str, str], online_status: dict[str, str], lifecycle_epoch: str,
    launch_nonce: str = "",
) -> bool:
    player_id = status_int(online_status, "playerId")
    zero_required = (
        "localAssignmentRejected",
        "assignmentPending",
        "assignmentBootstrapPending",
        "assignmentBootstrapRetryScheduled",
        "assignmentBootstrapActive",
        "assignmentBootstrapPermanentFailure",
        "assignmentBootstrapFailureCount",
        "assignmentBootstrapEndFailureMask",
        "assignmentBootstrapAppearanceValidationFailureMask",
    )
    return (
        avatar.get("ready") == "1"
        and avatar.get("connected") == "1"
        and avatar.get("localServerAssigned") == "1"
        and player_id > 0
        and status_int(avatar, "localServerId") > 0
        and avatar.get("transportConnectionGeneration")
        == online_status.get("connectionGeneration")
        and avatar.get("lifecycleEpoch") == lifecycle_epoch
        and avatar.get("assignmentGate") == "assigned"
        and avatar.get("assignmentBootstrapGate") == "bootstrap_ready"
        and avatar.get("assignmentBootstrapReady") == "1"
        and avatar.get("assignmentBootstrapFailure") == "none"
        and (not launch_nonce or avatar.get("launchNonce") == launch_nonce)
        and all(status_is_zero(avatar, key) for key in zero_required)
    )


def engine_active_plugin_order(mods: dict) -> tuple[str, ...]:
    """Return DevBench's engine-reported active ordered plugin set.

    DevBench's ``mods`` inspection is expected to be an ordered active list.
    If it starts supplying an explicit ``active`` field, reject any inactive
    entry rather than silently treating it as loaded content.
    """
    entries = mods.get("plugins")
    if not isinstance(entries, list):
        raise AutomationError(f"DevBench did not report an active plugin list: {mods}")
    names: list[str] = []
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("name"), str):
            raise AutomationError(f"DevBench reported an invalid plugin entry: {entry!r}")
        if "active" in entry and entry["active"] is not True:
            raise AutomationError(f"DevBench reported an inactive plugin in its active list: {entry}")
        names.append(entry["name"])
    return tuple(names)


def require_release_active_plugin_order(mods: dict) -> tuple[str, ...]:
    actual = engine_active_plugin_order(mods)
    light_plugins = mods.get("lightPlugins")
    if not isinstance(light_plugins, list):
        raise AutomationError(f"DevBench did not report the active light-plugin list: {mods}")
    if actual != RELEASE_ACTIVE_PLUGIN_ORDER or light_plugins:
        policy = "unexpected plugins are rejected" if REJECT_UNEXPECTED_RELEASE_PLUGINS else "subset policy"
        raise AutomationError(
            "engine-reported active plugin order does not match the isolated release lane "
            f"({policy}): expected={RELEASE_ACTIVE_PLUGIN_ORDER}; actual={actual}; "
            f"lightPluginCount={len(light_plugins)}"
        )
    return actual


def avatar_lifecycle_epoch(lifecycle: dict[str, str]) -> str:
    """Prefer the bridge epoch used by avatar ownership when it is available."""
    bridge_lifecycle_epoch = lifecycle.get("bridgeLifecycleEpoch", "")
    if bridge_lifecycle_epoch not in {"", "0"}:
        return bridge_lifecycle_epoch
    return lifecycle.get("epoch", "")


def verify_avatar_assignment_stability(
    avatar_path: pathlib.Path,
    online_status: dict[str, str],
    lifecycle_epoch: str,
    launch_nonce: str,
    tick_bridge_log_path: pathlib.Path,
    tick_bridge_log_start_offset: int,
    gameplay_bridge_log_path: pathlib.Path | None,
    gameplay_bridge_log_cursor: tuple[int, int, int, str] | None,
    run_started_ns: int,
    on_wait: Callable[[], None],
) -> tuple[dict[str, str], tuple[int, int]]:
    """Reject a transient assignment or a bridge critical after cell synchronization."""
    deadline = time.monotonic() + ASSIGNMENT_STABILITY_WINDOW
    baseline_sequence = max(
        successful_task_sequences(tick_bridge_log_path, tick_bridge_log_start_offset), default=0
    )
    cadence_sequences: set[int] = set()
    last_avatar: dict[str, str] = {}
    gameplay_bridge_startup_seen = gameplay_bridge_log_path is None

    while time.monotonic() < deadline:
        if GAME_PROCESS is not None and GAME_PROCESS.poll() is not None:
            raise AutomationError(
                f"game launcher exited with status {GAME_PROCESS.returncode} "
                "during local avatar assignment stability verification"
            )
        on_wait()
        last_avatar = read_status(avatar_path)
        if not avatar_assignment_ready(last_avatar, online_status, lifecycle_epoch, launch_nonce):
            raise AutomationError(
                "local avatar assignment became invalid during stability verification: "
                f"{last_avatar}"
            )
        if gameplay_bridge_log_path is not None:
            if not gameplay_bridge_log_path.is_file():
                raise AutomationError(
                    "SkyrimTogetherVRGameplayBridge log is unavailable after launch; "
                    f"cannot verify current critical entries: {gameplay_bridge_log_path}"
                )
            gameplay_bridge_log_text = read_current_gameplay_bridge_log_text(
                gameplay_bridge_log_path, gameplay_bridge_log_cursor, run_started_ns
            )
            gameplay_bridge_startup_seen = gameplay_bridge_startup_seen or (
                GAMEPLAY_BRIDGE_STARTUP_MARKER in gameplay_bridge_log_text
            )
            critical_entries = [
                line.strip()
                for line in gameplay_bridge_log_text.splitlines()
                if "[critical]" in line.casefold()
            ]
            if critical_entries:
                raise AutomationError(
                    "SkyrimTogetherVRGameplayBridge emitted a current-launch [critical] entry "
                    "during local avatar assignment verification: "
                    f"{critical_entries[-1]}"
                )
        cadence_sequences.update(
            sequence
            for sequence in successful_task_sequences(
                tick_bridge_log_path, tick_bridge_log_start_offset
            )
            if sequence > baseline_sequence
        )
        time.sleep(0.25)

    if len(cadence_sequences) < 2:
        raise AutomationError(
            "Skyrim Together cadence did not remain active during local avatar assignment "
            f"stability verification; observed sequences={sorted(cadence_sequences)}"
        )
    if not gameplay_bridge_startup_seen:
        raise AutomationError(
            "SkyrimTogetherVRGameplayBridge did not emit its current-launch loader "
            f"validation marker ({GAMEPLAY_BRIDGE_STARTUP_MARKER!r})"
        )
    first_two = tuple(sorted(cadence_sequences)[:2])
    return last_avatar, (first_two[0], first_two[1])


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
        help="deprecated compatibility option; Win32 scan-code input does not use kdotool",
    )
    parser.add_argument(
        "--monado-window-search",
        default="^Monado!.*$",
        help="deprecated compatibility option; XRizer direct input no longer focuses Monado",
    )
    parser.add_argument(
        "--xrizer-input-command",
        type=pathlib.Path,
        default=pathlib.Path("/tmp/stvr-xrizer-input"),
        help="XRizer direct controller command-file path",
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
        "--character-name",
        default="Shezarrine",
        help="name returned by the opt-in XRizer automation keyboard during New Game",
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
        help="start launch-skyrim-together-vr.sh before DevBench state checks",
    )
    parser.add_argument(
        "--ydotool",
        type=pathlib.Path,
        default=pathlib.Path.home() / ".local/bin/ydotool",
        help="deprecated compatibility option; ydotool is no longer used",
    )
    parser.add_argument(
        "--ydotoold",
        type=pathlib.Path,
        default=pathlib.Path.home() / ".local/bin/ydotoold",
        help="deprecated compatibility option; ydotoold is no longer used",
    )
    parser.add_argument(
        "--ydotool-socket",
        type=pathlib.Path,
        default=pathlib.Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
        / "stvr-ydotool.sock",
        help="deprecated compatibility option; ydotool is no longer used",
    )
    parser.add_argument(
        "--wine64",
        type=pathlib.Path,
        help="Wine64 executable for the Win32 scan-code helper (or STVR_WINE64)",
    )
    parser.add_argument(
        "--wine-prefix",
        type=pathlib.Path,
        help="Skyrim VR Proton prefix (or STVR_WINEPREFIX)",
    )
    parser.add_argument(
        "--steam-root",
        type=pathlib.Path,
        help="Steam root used to find GE-Proton (or STVR_STEAM_ROOT)",
    )
    parser.add_argument(
        "--steam-library",
        type=pathlib.Path,
        help="Steam library used to find AppID 611670 compatdata (or STVR_STEAM_LIBRARY)",
    )
    parser.add_argument(
        "--proton-path",
        type=pathlib.Path,
        help="GE-Proton directory used to find wine64 (or STVR_PROTONPATH)",
    )
    args = parser.parse_args()

    if args.connect and not args.launch_game:
        raise AutomationError(
            "--connect verification requires --launch-game so session and request proof comes from a fresh process"
        )
    if args.connect and args.vm_update_mode != "active":
        raise AutomationError("--connect requires --vm-update-mode active after the observer gate has passed")

    launch_nonce = secrets.token_hex(16) if args.connect else ""
    if launch_nonce and not LAUNCH_NONCE_PATTERN.fullmatch(launch_nonce):
        raise AutomationError("generated launch nonce is invalid")

    handoff_dir = args.skyrim_vr / "Data" / "SkyrimTogetherReborn"
    command_path = handoff_dir / "SkyrimTogetherVR.command"
    status_path = handoff_dir / "SkyrimTogetherVR.status"
    lifecycle_path = handoff_dir / "SkyrimTogetherVR.lifecycle"
    player_cell_path = handoff_dir / "SkyrimTogetherVR.playercell"
    avatar_path = handoff_dir / "SkyrimTogetherVR.avatar"
    tick_bridge_log_path = args.skyrim_vr / "Data" / "SKSE" / "Plugins" / "SkyrimTogetherVRTickBridge.log"
    tick_bridge_log_start_offset = (
        tick_bridge_log_path.stat().st_size if tick_bridge_log_path.is_file() else 0
    )
    gameplay_bridge_log_path: pathlib.Path | None = None
    gameplay_bridge_log_cursor: tuple[int, int, int, str] | None = None
    if args.connect:
        for stale_proof in (command_path, status_path, lifecycle_path, player_cell_path, avatar_path):
            stale_proof.unlink(missing_ok=True)
        try:
            gameplay_bridge_log_path = (
                resolve_wine_prefix(args) / GAMEPLAY_BRIDGE_LOG_RELATIVE_PATH
            )
            gameplay_bridge_log_cursor = capture_gameplay_bridge_log_cursor(
                gameplay_bridge_log_path
            )
        except AutomationError as exc:
            raise AutomationError(
                "--connect could not establish required gameplay-bridge log validation: "
                f"{exc}"
            ) from exc
    run_started_ns = time.time_ns()

    if args.launch_game:
        launcher = args.skyrim_vr / "launch-skyrim-together-vr.sh"
        if not launcher.is_file() or not os.access(launcher, os.X_OK):
            raise AutomationError(f"game launcher is missing or not executable: {launcher}")
        remove_stale_xrizer_input_command(args.xrizer_input_command)
        launch_env = os.environ.copy()
        launch_env.pop("STVR_AUTOCONNECT", None)
        launch_env.pop("STVR_PASSWORD", None)
        launch_env["STVR_DISABLE_AUTOCONNECT"] = "1"
        launch_env["STVR_FORCE_PROTON"] = "1"
        launch_env["STVR_VM_UPDATE_MODE"] = args.vm_update_mode
        launch_env["STVR_XRIZER_INPUT_COMMAND"] = str(args.xrizer_input_command)
        if launch_nonce:
            launch_env["STVR_LAUNCH_NONCE"] = launch_nonce
        if not args.load_save:
            launch_env["STVR_XRIZER_KEYBOARD_TEXT"] = args.character_name
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
        print(
            "Launched Skyrim Together VR with XRizer direct controller input "
            f"at {args.xrizer_input_command}; log: {log_path}",
            flush=True,
        )

    state = wait_until(
        "actionable Main Menu or RaceSex Menu",
        args.timeout,
        lambda: advance_to_main_menu(args.url),
        lambda value: is_menu_ready_for_input(value, ("Main Menu", "RaceSex Menu")),
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
            # End normalizes Skyrim VR's Main Menu to its top visible entry.
            # The helper uses the active prefix's save roster: its saved-game
            # route moves one row from Continue to New Game; the clean route
            # activates top-row New Game. Both stay inside Wine's input stack.
            select_new_game_with_win32_scancodes(args)
            # Skyrim VR's modded-new-game confirmation is not a
            # MessageBoxMenu, so gate bounded trigger retries on observable
            # Main Menu/RaceSex state instead of a fixed sleep.
            race_state = accept_new_game_confirmation(args)
        else:
            race_state = state
        print(f"RaceSex ready: {race_state.get('openMenus', [])}")

        # XRizer maps this direct menu pulse to the controller-side RaceSex Done
        # action. Other interaction-profile bindings are not reliable in Skyrim VR.
        publish_xrizer_input_command(args.xrizer_input_command, "menu")

        wait_until(
            "RaceSex confirmation",
            10.0,
            lambda: menu_state(args.url),
            lambda value: value.get("messageBoxOpen") is True,
        )
        description = post_tool(args.url, "menu", {"action": "describe"})
        if allowlisted_messagebox_index(description, "racesex_confirmation") is None:
            raise AutomationError(f"RaceSex confirmation does not default to acceptance: {description}")

        # Route a real controller trigger through XRizer. The dialog closing is not
        # enough: the RaceSex menu itself must close through Skyrim's name/finalize
        # transaction before any client readiness or connection check is allowed.
        publish_xrizer_input_command(args.xrizer_input_command, "trigger")
        print(
            "Activated RaceSex confirmation through XRizer direct controller input: "
            f"{description['buttons'][0]}"
        )

        post_confirmation_state = wait_until(
            "RaceSex confirmation dialog to close after controller activation",
            15.0,
            lambda: menu_state(args.url),
            lambda value: value.get("messageBoxOpen") is False,
        )
        try:
            complete_racesex_name_stage(
                post_confirmation_state,
                state_reader=lambda: menu_state(args.url),
                trigger_publisher=lambda: publish_xrizer_input_command(
                    args.xrizer_input_command, "trigger"
                ),
                timeout=15.0,
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
    require_release_active_plugin_order(mods)
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
            and (not launch_nonce or value.get("launchNonce") == launch_nonce)
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
            and value.get("playerFormId") not in {None, "", "0"}
            and value.get("lifecycleEpoch") == lifecycle.get("epoch")
            and (not launch_nonce or value.get("launchNonce") == launch_nonce),
            on_wait=lambda: handle_blocking_message_box(args.url, "finalization"),
        )
        if player_cell_path.stat().st_mtime_ns < run_started_ns:
            raise AutomationError("player readiness proof predates this automation run")
        session_id = player_cell.get("sessionId", "")
        if session_id in {"", "0"}:
            raise AutomationError(f"player readiness proof has no process session ID: {player_cell}")
    first_identity = finalization_identity(scene, player)
    time.sleep(1.0)
    stable_scene = post_tool(args.url, "inspect", {"kind": "scene"})
    stable_player = post_tool(args.url, "inspect", {"kind": "player"})
    if finalization_identity(stable_scene, stable_player) != first_identity:
        raise AutomationError(
            "player identity or Realm cell changed during finalization stability check: "
            f"{first_identity} -> {finalization_identity(stable_scene, stable_player)}"
        )

    drain_stale_realm_lorkhan_fader(
        state_reader=lambda: menu_state(args.url),
        fader_closer=lambda: post_tool(
            args.url, "menu", {"action": "close", "name": "Fader Menu"}
        ),
        timeout=10.0,
    )

    print(f"Realm of Lorkhan ready at {scene.get('position')}")
    print(f"Player finalized: {player.get('name')} ({player.get('race')})")
    print("Release active plugin order is verified")

    if args.connect:
        def maintain_connection_cadence() -> None:
            handle_blocking_message_box(args.url, "connect_wait")
            raise_for_nonce_bound_status_error(read_status(status_path), launch_nonce)

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
        pending_path.write_text(
            f"action=connect\nendpoint={args.connect}\npassword=\nlaunchNonce={launch_nonce}\n",
            encoding="utf-8",
        )
        pending_path.replace(command_path)
        status = wait_until(
            "Skyrim Together online status",
            args.timeout,
            lambda: read_status(status_path),
            lambda value: online_status_ready(
                value, session_id, baseline_generation, launch_nonce
            ),
            on_wait=maintain_connection_cadence,
        )
        if status_path.stat().st_mtime_ns < run_started_ns:
            raise AutomationError("online status proof predates this automation run")
        def cell_sync_ready(value: dict[str, str]) -> bool:
            common_ready = (
                value.get("ready") == "1"
                and value.get("online") == "1"
                and value.get("sessionId") == session_id
                and value.get("launchNonce") == launch_nonce
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
        avatar = wait_until(
            "Skyrim Together local avatar assignment",
            args.timeout,
            lambda: read_status(avatar_path),
            lambda value: avatar_assignment_ready(
                value, status, avatar_lifecycle_epoch(lifecycle), launch_nonce
            ),
            on_wait=maintain_connection_cadence,
        )
        try:
            avatar_mtime_ns = avatar_path.stat().st_mtime_ns
        except OSError as exc:
            raise AutomationError(
                f"local avatar assignment proof disappeared before verification: {avatar_path}"
            ) from exc
        if avatar_mtime_ns < run_started_ns:
            raise AutomationError("local avatar assignment proof predates this automation run")
        stable_avatar, stability_sequences = verify_avatar_assignment_stability(
            avatar_path,
            status,
            avatar_lifecycle_epoch(lifecycle),
            launch_nonce,
            tick_bridge_log_path,
            tick_bridge_log_start_offset,
            gameplay_bridge_log_path,
            gameplay_bridge_log_cursor,
            run_started_ns,
            maintain_connection_cadence,
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
        print(
            "Skyrim Together local avatar assignment verified: "
            f"player={stable_avatar['localServerId']} "
            f"generation={stable_avatar['transportConnectionGeneration']} "
            f"cadence={stability_sequences[0]},{stability_sequences[1]}",
            flush=True,
        )
        usable_menu = wait_for_usable_stable_menu(
            state_reader=lambda: menu_state(args.url),
            fader_closer=lambda: post_tool(
                args.url, "menu", {"action": "close", "name": "Fader Menu"}
            ),
            timeout=10.0,
        )
        print(f"USABLE_STABLE menu gate passed: {usable_menu.get('openMenus', [])}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AutomationError as exc:
        print(f"devbench-new-game: {exc}", file=sys.stderr)
        raise SystemExit(1)
