#!/usr/bin/env bash

# Source this file from a Skyrim VR launcher after GAME_DIR and
# stvr_append_pressure_vessel_ro are set.  Runtime choice is intentionally
# fail-closed: VR_OVERRIDE is the only selector we pass to Proton/OpenVR.

stvr_openvr_runtime_die() {
  printf 'OpenVR runtime selection: %s\n' "$*" >&2
  return 1
}

stvr_resolve_runtime_dir() {
  local runtime="$1" resolved
  [ -n "$runtime" ] && [ -d "$runtime" ] || return 1
  resolved="$(readlink -f -- "$runtime")" || return 1
  [[ "$resolved" == /* && "$resolved" != *:* && "$resolved" != *$'\n'* && "$resolved" != *$'\r'* ]] || return 1
  printf '%s\n' "$resolved"
}

stvr_path_is_within() {
  local root="$1" path="$2"
  [ "$path" = "$root" ] || [[ "$path" == "$root"/* ]]
}

# A loader may be a symlink, but its resolved regular-file target must remain
# inside the selected runtime.  Parse its ELF structure without loading it:
# ldd can execute a hostile/non-native loader via dynamic-loader machinery.
stvr_validate_elf64_x86_64() {
  local library="$1"
  command -v python3 >/dev/null 2>&1 || return 1
  python3 - "$library" <<'PY'
import sys

try:
    with open(sys.argv[1], "rb") as source:
        source.seek(0, 2)
        file_size = source.tell()
        source.seek(0)
        header = source.read(64)

    elf_header_size = 64
    program_header_size = 56
    if len(header) < elf_header_size:
        raise ValueError("truncated ELF header")
    if header[:4] != b"\x7fELF" or header[4] != 2 or header[5] != 1 or header[6] != 1:
        raise ValueError("not a 64-bit little-endian ELF file")

    elf_type = int.from_bytes(header[16:18], "little")
    machine = int.from_bytes(header[18:20], "little")
    header_version = int.from_bytes(header[20:24], "little")
    program_offset = int.from_bytes(header[32:40], "little")
    header_size = int.from_bytes(header[52:54], "little")
    program_entry_size = int.from_bytes(header[54:56], "little")
    program_count = int.from_bytes(header[56:58], "little")
    if elf_type != 3 or machine != 62 or header_version != 1:
        raise ValueError("not an x86-64 ET_DYN ELF file")
    if header_size != elf_header_size or program_entry_size != program_header_size:
        raise ValueError("invalid ELF or program header size")
    if not program_count or program_offset < elf_header_size or program_offset > file_size:
        raise ValueError("missing or invalid program header table")
    if program_count > (file_size - program_offset) // program_entry_size:
        raise ValueError("truncated program header table")

    with open(sys.argv[1], "rb") as source:
        for index in range(program_count):
            source.seek(program_offset + index * program_entry_size)
            program_header = source.read(program_header_size)
            if len(program_header) != program_header_size:
                raise ValueError("truncated program header table")
            program_type = int.from_bytes(program_header[0:4], "little")
            if program_type != 1:  # PT_LOAD
                continue
            file_offset = int.from_bytes(program_header[8:16], "little")
            virtual_address = int.from_bytes(program_header[16:24], "little")
            segment_file_size = int.from_bytes(program_header[32:40], "little")
            memory_size = int.from_bytes(program_header[40:48], "little")
            alignment = int.from_bytes(program_header[48:56], "little")
            if (segment_file_size and memory_size >= segment_file_size and file_offset <= file_size
                    and segment_file_size <= file_size - file_offset
                    and (alignment in (0, 1) or alignment & (alignment - 1) == 0)
                    and (alignment <= 1 or (virtual_address - file_offset) % alignment == 0)):
                break
        else:
            raise ValueError("no sane, file-contained PT_LOAD segment")
except (OSError, ValueError) as error:
    raise SystemExit(str(error))
PY
}

stvr_resolve_native_loader() {
  local runtime="$1" relative="$2" candidate resolved
  candidate="$runtime/$relative"
  [ -e "$candidate" ] || return 1
  resolved="$(readlink -f -- "$candidate")" || return 1
  stvr_path_is_within "$runtime" "$resolved" && [ -f "$resolved" ] || return 1
  stvr_validate_elf64_x86_64 "$resolved" || return 1
  printf '%s\n' "$resolved"
}

stvr_validate_xrizer_elf_symbols() {
  local library="$1" symbols legacy_symbol
  command -v readelf >/dev/null 2>&1 || {
    printf 'OpenVR runtime selection: cannot inspect XRizer ELF %s because readelf is unavailable\n' \
      "$library" >&2
    return 1
  }
  symbols="$(readelf --dyn-syms --wide "$library" 2>/dev/null)" || {
    printf 'OpenVR runtime selection: cannot inspect XRizer ELF %s with readelf\n' "$library" >&2
    return 1
  }
  legacy_symbol="$(awk '
    {
      for (field = 1; field < NF; field++) {
        if ($field == "UND" && $(field + 1) ~ /^_ZNSt12experimental10filesystem/) {
          print $(field + 1)
          exit
        }
      }
    }
  ' <<<"$symbols")"
  if [ -n "$legacy_symbol" ]; then
    printf 'OpenVR runtime selection: rejecting XRizer ELF %s with unresolved legacy std::experimental::filesystem symbol %s (libstdc++ ABI mismatch)\n' \
      "$library" "$legacy_symbol" >&2
    return 1
  fi
}

stvr_validate_pathreg() {
  local pathreg="$1" resolved
  [ -n "$pathreg" ] && [ -f "$pathreg" ] || return 1
  resolved="$(readlink -f -- "$pathreg")" || return 1
  [[ "$resolved" == /* && "$resolved" != *:* && "$resolved" != *$'\n'* && "$resolved" != *$'\r'* ]] || return 1
  command -v python3 >/dev/null 2>&1 || return 1
  python3 - "$resolved" <<'PY' >/dev/null
import json
import sys

try:
    with open(sys.argv[1], encoding="utf-8") as source:
        data = json.load(source)
    if not isinstance(data, dict) or data.get("version") != 1 or not isinstance(data.get("runtime"), list):
        raise ValueError("expected version=1 and a runtime array")
    if not all(isinstance(item, str) for item in data["runtime"]):
        raise ValueError("runtime array contains a non-string value")
except (OSError, ValueError, json.JSONDecodeError) as error:
    raise SystemExit(str(error))
PY
  printf '%s\n' "$resolved"
}

stvr_default_pathreg() {
  [ -n "${GAME_DIR:-}" ] || return 1
  stvr_validate_pathreg "$GAME_DIR/.stvr-openvr/openvrpaths.vrpath"
}

stvr_select_pathreg() {
  if [ -n "${STVR_OPENVR_PATHREG:-}" ]; then
    stvr_validate_pathreg "$STVR_OPENVR_PATHREG"
  else
    stvr_default_pathreg
  fi
}

stvr_host_registry_runtime() {
  local config_home registry
  config_home="${XDG_CONFIG_HOME:-${HOME:+$HOME/.config}}"
  [ -n "$config_home" ] || return 1
  registry="$config_home/openvr/openvrpaths.vrpath"
  [ -f "$registry" ] && command -v python3 >/dev/null 2>&1 || return 1
  python3 - "$registry" <<'PY'
import json
import pathlib
import sys

try:
    with open(sys.argv[1], encoding="utf-8") as source:
        data = json.load(source)
    values = data.get("runtime") if isinstance(data, dict) else None
    if not isinstance(values, list) or len(values) != 1 or not isinstance(values[0], str) or not values[0]:
        raise ValueError("registry must select exactly one runtime")
    print(pathlib.Path(values[0]).expanduser().resolve(strict=True))
except (OSError, ValueError, json.JSONDecodeError) as error:
    raise SystemExit(str(error))
PY
}

stvr_is_bundled_runtime() {
  local kind="$1" runtime="$2" bundled
  [ -n "${GAME_DIR:-}" ] || return 1
  bundled="$(stvr_resolve_runtime_dir "$GAME_DIR/.stvr-openvr/$kind")" || return 1
  [ "$runtime" = "$bundled" ]
}

# Prints the normalized runtime root. A packaged XRizer must carry both the
# runtime-root library Proton/OpenVR loads and its OpenVR loader copy.
stvr_validate_xrizer_runtime() {
  local supplied="$1" runtime release loader xrizer_loader xrizer_library
  runtime="$(stvr_resolve_runtime_dir "$supplied")" || return 1
  if stvr_is_bundled_runtime xrizer "$runtime"; then
    if [ -f "$runtime/libxrizer.so" ] && [ ! -L "$runtime/libxrizer.so" ] && \
      [ -f "$runtime/bin/linux64/vrclient.so" ] && [ ! -L "$runtime/bin/linux64/vrclient.so" ] && \
      xrizer_loader="$(stvr_resolve_native_loader "$runtime" 'libxrizer.so')" && \
      loader="$(stvr_resolve_native_loader "$runtime" 'bin/linux64/vrclient.so')" && \
      cmp -s -- "$runtime/libxrizer.so" "$runtime/bin/linux64/vrclient.so"; then
      stvr_validate_xrizer_elf_symbols "$xrizer_loader" || return 1
      stvr_validate_xrizer_elf_symbols "$loader" || return 1
      printf '%s\n' "$runtime"
      return 0
    fi
    return 1
  fi
  if loader="$(stvr_resolve_native_loader "$runtime" 'bin/linux64/vrclient.so')"; then
    if [ -f "$runtime/libxrizer.so" ]; then
      xrizer_library="$runtime/libxrizer.so"
    elif [ -f "$runtime/bin/linux64/libxrizer.so" ]; then
      xrizer_library="$runtime/bin/linux64/libxrizer.so"
    else
      return 1
    fi
    stvr_validate_xrizer_elf_symbols "$xrizer_library" || return 1
    stvr_validate_xrizer_elf_symbols "$loader" || return 1
    printf '%s\n' "$runtime"
    return 0
  fi
  release="$runtime/target/release"
  if [ -f "$release/libxrizer.so" ] && loader="$(stvr_resolve_native_loader "$release" 'vrclient.so')" && \
    stvr_validate_xrizer_elf_symbols "$release/libxrizer.so" && \
    stvr_validate_xrizer_elf_symbols "$loader"; then
    printf '%s\n' "$release"
    return 0
  fi
  # An official build-output directory is also acceptable when supplied
  # directly, but a generic OpenVR registry entry never is.
  if [ -f "$runtime/libxrizer.so" ] && loader="$(stvr_resolve_native_loader "$runtime" 'vrclient.so')" && \
    stvr_validate_xrizer_elf_symbols "$runtime/libxrizer.so" && \
    stvr_validate_xrizer_elf_symbols "$loader"; then
    printf '%s\n' "$runtime"
    return 0
  fi
  return 1
}

stvr_validate_opencomposite_runtime() {
  local supplied="$1" runtime
  runtime="$(stvr_resolve_runtime_dir "$supplied")" || return 1
  stvr_resolve_native_loader "$runtime" 'bin/linux64/vrclient.so' >/dev/null || return 1
  printf '%s\n' "$runtime"
}

stvr_validate_steamvr_runtime() {
  local supplied="$1" runtime
  runtime="$(stvr_resolve_runtime_dir "$supplied")" || return 1
  stvr_resolve_native_loader "$runtime" 'bin/linux64/vrclient.so' >/dev/null || return 1
  # These server-side binaries are SteamVR-specific and rule out XRizer and
  # OpenComposite runtime directories that merely expose vrclient.so.
  [ -f "$runtime/bin/linux64/vrserver" ] || [ -f "$runtime/bin/linux64/vrcompositor" ] || return 1
  printf '%s\n' "$runtime"
}

stvr_mount_runtime_artifacts() {
  local runtime="$1" pathreg="$2" pathreg_parent
  command -v stvr_append_pressure_vessel_ro >/dev/null 2>&1 || return 1
  pathreg_parent="$(dirname -- "$pathreg")"
  stvr_append_pressure_vessel_ro "$runtime"
  stvr_append_pressure_vessel_ro "$pathreg_parent"
}

stvr_configure_openvr_runtime() {
  local selected runtime candidate pathreg
  selected="${STVR_OPENVR_RUNTIME:-xrizer}"
  case "$selected" in
    xrizer)
      if [ -n "${STVR_XRIZER_RUNTIME:-}" ]; then
        runtime="$(stvr_validate_xrizer_runtime "$STVR_XRIZER_RUNTIME")" || stvr_openvr_runtime_die \
          'STVR_XRIZER_RUNTIME must be a validated XRizer native runtime root'
      elif runtime="$(stvr_validate_xrizer_runtime "${GAME_DIR:-}/.stvr-openvr/xrizer")"; then
        :
      elif candidate="$(stvr_host_registry_runtime 2>/dev/null)" && runtime="$(stvr_validate_xrizer_runtime "$candidate")"; then
        :
      else
        stvr_openvr_runtime_die \
          'XRizer requires GAME_DIR/.stvr-openvr/xrizer or STVR_XRIZER_RUNTIME with native vrclient.so and XRizer identity'
      fi
      pathreg="$(stvr_select_pathreg)" || stvr_openvr_runtime_die \
        'XRizer requires a valid neutral OpenVR path registry; install .stvr-openvr/openvrpaths.vrpath or set STVR_OPENVR_PATHREG'
      unset PROTON_VR_RUNTIME STVR_OPENCOMPOSITE_RUNTIME
      export VR_OVERRIDE="$runtime"
      export VR_PATHREG_OVERRIDE="$pathreg"
      export XRIZER_OPENVR_KNUCKLES_AS_OCULUS_TOUCH="${XRIZER_OPENVR_KNUCKLES_AS_OCULUS_TOUCH:-1}"
      # XRizer has no interactive OpenVR keyboard. Supplying a bounded default
      # lets a physical controller complete Skyrim's normal confirm/name
      # transaction; automation can override the same callback before launch.
      if [[ ! -v STVR_XRIZER_KEYBOARD_TEXT ]]; then
        STVR_XRIZER_KEYBOARD_TEXT=Prisoner
      elif [ -z "$STVR_XRIZER_KEYBOARD_TEXT" ]; then
        stvr_openvr_runtime_die \
          'STVR_XRIZER_KEYBOARD_TEXT must not be empty when XRizer is selected'
      fi
      export STVR_XRIZER_KEYBOARD_TEXT
      if [ "${STVR_XRIZER_INPUT_DEBUG:-0}" = "1" ]; then
        export RUST_LOG="${RUST_LOG:+$RUST_LOG,}openvr_calls=trace,tracked_property=trace,xrizer::input=debug,xrizer::input::legacy=trace"
      fi
      ;;
    opencomposite)
      if [ -n "${STVR_OPENCOMPOSITE_RUNTIME:-}" ]; then
        runtime="$(stvr_validate_opencomposite_runtime "$STVR_OPENCOMPOSITE_RUNTIME")" || stvr_openvr_runtime_die \
          'STVR_OPENCOMPOSITE_RUNTIME must contain native ELF64 x86-64 bin/linux64/vrclient.so'
      else
        runtime="$(stvr_validate_opencomposite_runtime "${GAME_DIR:-}/.stvr-openvr/opencomposite")" || stvr_openvr_runtime_die \
          'OpenComposite requires GAME_DIR/.stvr-openvr/opencomposite or STVR_OPENCOMPOSITE_RUNTIME with native bin/linux64/vrclient.so'
      fi
      pathreg="$(stvr_select_pathreg)" || stvr_openvr_runtime_die \
        'OpenComposite requires a valid neutral OpenVR path registry; install .stvr-openvr/openvrpaths.vrpath or set STVR_OPENVR_PATHREG'
      unset PROTON_VR_RUNTIME STVR_XRIZER_RUNTIME XRIZER_OPENVR_KNUCKLES_AS_OCULUS_TOUCH
      export VR_OVERRIDE="$runtime"
      export VR_PATHREG_OVERRIDE="$pathreg"
      ;;
    steamvr)
      if [ -n "${STVR_STEAMVR_RUNTIME:-}" ]; then
        runtime="$(stvr_validate_steamvr_runtime "$STVR_STEAMVR_RUNTIME")" || stvr_openvr_runtime_die \
          'STVR_STEAMVR_RUNTIME must be a SteamVR native runtime with vrserver or vrcompositor markers'
      else
        candidate="$(stvr_host_registry_runtime 2>/dev/null)" || stvr_openvr_runtime_die \
          'SteamVR requires STVR_STEAMVR_RUNTIME or a host openvrpaths.vrpath selecting exactly one SteamVR runtime'
        runtime="$(stvr_validate_steamvr_runtime "$candidate")" || stvr_openvr_runtime_die \
          'host OpenVR registry does not select a validated SteamVR runtime'
      fi
      pathreg="$(stvr_select_pathreg)" || stvr_openvr_runtime_die \
        'SteamVR requires a valid neutral OpenVR path registry; install .stvr-openvr/openvrpaths.vrpath or set STVR_OPENVR_PATHREG'
      unset PROTON_VR_RUNTIME STVR_XRIZER_RUNTIME XRIZER_OPENVR_KNUCKLES_AS_OCULUS_TOUCH STVR_OPENCOMPOSITE_RUNTIME
      export VR_OVERRIDE="$runtime"
      export VR_PATHREG_OVERRIDE="$pathreg"
      ;;
    *)
      stvr_openvr_runtime_die \
        "invalid STVR_OPENVR_RUNTIME=$selected (expected xrizer, opencomposite, or steamvr)"
      ;;
  esac
  stvr_mount_runtime_artifacts "$runtime" "$pathreg" || stvr_openvr_runtime_die \
    'launcher cannot expose the selected OpenVR runtime and path registry to pressure-vessel safely'
  STVR_SELECTED_OPENVR_RUNTIME="$selected"
  STVR_SELECTED_OPENVR_RUNTIME_PATH="$runtime"
  STVR_SELECTED_OPENVR_PATHREG="$pathreg"
  export STVR_SELECTED_OPENVR_RUNTIME STVR_SELECTED_OPENVR_RUNTIME_PATH STVR_SELECTED_OPENVR_PATHREG
}

stvr_configure_openvr_runtime
