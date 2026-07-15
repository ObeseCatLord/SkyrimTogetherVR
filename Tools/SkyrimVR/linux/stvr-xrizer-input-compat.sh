#!/usr/bin/env bash

# Source this file from a Skyrim VR launcher after GAME_DIR is set.
# Explicit environment variables always win over auto-detection.

stvr_find_xrizer_runtime() {
  local config="${XDG_CONFIG_HOME:-$HOME/.config}/openvr/openvrpaths.vrpath"

  if [ -n "${STVR_XRIZER_RUNTIME:-}" ] && [ -d "$STVR_XRIZER_RUNTIME" ]; then
    printf '%s\n' "$STVR_XRIZER_RUNTIME"
    return 0
  fi

  if [ -n "${PROTON_VR_RUNTIME:-}" ] && [ -d "$PROTON_VR_RUNTIME" ]; then
    printf '%s\n' "$PROTON_VR_RUNTIME"
    return 0
  fi

  if [ -f "$config" ] && command -v python3 >/dev/null 2>&1; then
    python3 - "$config" <<'PY'
import json
import pathlib
import sys

try:
    data = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
except (OSError, ValueError):
    raise SystemExit(1)

for value in data.get("runtime", []):
    path = pathlib.Path(value).expanduser()
    candidates = (path, path.parent) if path.is_file() else (path,)
    for candidate in candidates:
        if (candidate / "vrclient.so").exists() or (candidate / "vrclient_x64.dll").exists():
            print(candidate.resolve())
            raise SystemExit(0)
raise SystemExit(1)
PY
    return $?
  fi

  return 1
}

if [ -z "${PROTON_VR_RUNTIME:-}" ]; then
  if STVR_DETECTED_XRIZER="$(stvr_find_xrizer_runtime)"; then
    export PROTON_VR_RUNTIME="$STVR_DETECTED_XRIZER"
  fi
fi

# The tested XRizer patch keeps Index OpenXR bindings but exposes the legacy
# controller facade Skyrim VR/FUS expects. Set 0 to retain native Knuckles IDs.
export XRIZER_OPENVR_KNUCKLES_AS_OCULUS_TOUCH="${XRIZER_OPENVR_KNUCKLES_AS_OCULUS_TOUCH:-1}"

if [ "${STVR_XRIZER_INPUT_DEBUG:-0}" = "1" ]; then
  export RUST_LOG="${RUST_LOG:+$RUST_LOG,}openvr_calls=trace,tracked_property=trace,xrizer::input=debug,xrizer::input::legacy=trace"
fi
