#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WINE_BIN="${WINE:-wine}"
POWERSHELL_EXE="${STVR_WINE_POWERSHELL:-powershell.exe}"
PS_SCRIPT="${ROOT}/BuildSkyrimTogetherVR-Windows.ps1"

if ! command -v "${WINE_BIN}" >/dev/null 2>&1; then
    echo "Could not find Wine executable: ${WINE_BIN}" >&2
    echo "Set WINE=/path/to/wine or install Wine." >&2
    exit 1
fi

if ! command -v winepath >/dev/null 2>&1; then
    echo "Could not find winepath. Install Wine tools for your distribution." >&2
    exit 1
fi

if [[ ! -f "${PS_SCRIPT}" ]]; then
    echo "Could not find ${PS_SCRIPT}" >&2
    exit 1
fi

PS_PROBE_LOG="$(mktemp)"
trap 'rm -f "${PS_PROBE_LOG}"' EXIT

if ! "${WINE_BIN}" "${POWERSHELL_EXE}" -NoProfile -ExecutionPolicy Bypass -Command "\$PSVersionTable.PSVersion.ToString()" >"${PS_PROBE_LOG}" 2>&1; then
    cat "${PS_PROBE_LOG}" >&2
    echo "Could not start PowerShell through Wine: ${POWERSHELL_EXE}" >&2
    echo "Install Windows PowerShell or PowerShell 7 in the Wine prefix, or set STVR_WINE_POWERSHELL=/path/to/pwsh.exe." >&2
    exit 1
fi

if grep -qiE "fixme:powershell|powershell:wmain stub" "${PS_PROBE_LOG}" || ! grep -Eq "[0-9]+\\.[0-9]+" "${PS_PROBE_LOG}"; then
    cat "${PS_PROBE_LOG}" >&2
    echo "Wine is using its built-in powershell.exe stub, so the Windows build script did not run." >&2
    echo "Install Windows PowerShell or PowerShell 7 in the Wine prefix, or set STVR_WINE_POWERSHELL=/path/to/pwsh.exe." >&2
    exit 1
fi

PS_SCRIPT_WIN="$(winepath -w "${PS_SCRIPT}")"
ARGS=()

if [[ -n "${STVR_XMAKE:-}" ]]; then
    ARGS+=("-Xmake" "${STVR_XMAKE}")
fi

ARGS+=("$@")

exec "${WINE_BIN}" "${POWERSHELL_EXE}" -NoProfile -ExecutionPolicy Bypass -File "${PS_SCRIPT_WIN}" "${ARGS[@]}"
