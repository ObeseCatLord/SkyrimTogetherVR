#!/usr/bin/env bash
# Create, audit, checksum, and optionally upload one complete private handoff.
set -euo pipefail
IFS=$'\n\t'
export LC_ALL=C

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
state_root=${XDG_STATE_HOME:-$HOME/.local/state}/skyrim-together-vr
state_tmp=$state_root/tmp
portable_root=$state_root/portable-openvr
xrizer_root=${STVR_XRIZER_ROOT:-$state_root/xrizer-reviewed}
opencomposite_root=${STVR_OPENCOMPOSITE_ROOT:-$HOME/.local/share/envision/opencomposite}
gameplay_package=''
build_evidence=''
output=''
portable_runtime_dir=''
upload_target=''

usage() {
    cat >&2 <<EOF
Usage: ${0##*/} --gameplay-package ZIP --build-evidence ZIP [options]

Options:
  --output ZIP                  Exact output path (must not exist)
  --portable-runtime-dir DIR    Reuse an already built portable runtime pair
  --xrizer-root DIR             Reviewed XRizer source checkout
  --opencomposite-root DIR      Reviewed OpenComposite source checkout
  --upload-target HOST:DIR/     Upload ZIP and sidecar, then verify remotely
EOF
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 2
}

while (($#)); do
    case $1 in
        --gameplay-package) gameplay_package=${2:?missing package}; shift 2 ;;
        --build-evidence) build_evidence=${2:?missing evidence}; shift 2 ;;
        --output) output=${2:?missing output}; shift 2 ;;
        --portable-runtime-dir) portable_runtime_dir=${2:?missing runtime directory}; shift 2 ;;
        --xrizer-root) xrizer_root=${2:?missing XRizer root}; shift 2 ;;
        --opencomposite-root) opencomposite_root=${2:?missing OpenComposite root}; shift 2 ;;
        --upload-target) upload_target=${2:?missing upload target}; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) usage; die "unknown argument: $1" ;;
    esac
done

[[ -n $gameplay_package && -n $build_evidence ]] || { usage; exit 2; }
command -v git >/dev/null || die 'git is required'
command -v python3 >/dev/null || die 'python3 is required'
command -v sha256sum >/dev/null || die 'sha256sum is required'
command -v unzip >/dev/null || die 'unzip is required'

cd -- "$repo_root"
[[ -z $(git status --porcelain=v1 --untracked-files=all) ]] || die 'repository must be clean'
gameplay_package=$(realpath -e -- "$gameplay_package")
build_evidence=$(realpath -e -- "$build_evidence")

mkdir -p -- "$state_tmp" "$portable_root" "$repo_root/artifacts/SkyrimTogetherVR/review-handoff"
export TMPDIR=$state_tmp

if [[ ${STVR_BUILD_LOCK_HELD:-0} != 1 ]]; then
    exec 8>"${XDG_RUNTIME_DIR:-$state_tmp}/skyrim-together-vr-build-active.lock"
    flock -n 8 || die 'another Skyrim Together VR build or handoff operation is active'
fi

mapfile -t pins < <(python3 - "$repo_root" <<'PY'
import importlib.util
import pathlib
import sys

path = pathlib.Path(sys.argv[1]) / "Tools/SkyrimVR/create_local_agent_handoff.py"
spec = importlib.util.spec_from_file_location("stvr_handoff_creator", path)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)
for name in (
    "XRIZER_BASE_REVISION",
    "XRIZER_RUNTIME_SHA256",
    "OPENCOMPOSITE_REVISION",
    "OPENCOMPOSITE_RUNTIME_SHA256",
):
    print(getattr(module, name))
PY
)
((${#pins[@]} == 4)) || die 'could not read reviewed runtime pins'
xrizer_revision=${pins[0]}
xrizer_sha256=${pins[1]}
opencomposite_revision=${pins[2]}
opencomposite_sha256=${pins[3]}

validate_runtime_dir() {
    local root=$1
    [[ -f $root/xrizer/libxrizer.so && ! -L $root/xrizer/libxrizer.so ]] || return 1
    [[ -f $root/xrizer/bin/linux64/vrclient.so && ! -L $root/xrizer/bin/linux64/vrclient.so ]] || return 1
    [[ -f $root/opencomposite/bin/linux64/vrclient.so && ! -L $root/opencomposite/bin/linux64/vrclient.so ]] || return 1
    [[ $(sha256sum "$root/xrizer/libxrizer.so" | cut -d' ' -f1) == "$xrizer_sha256" ]] || return 1
    [[ $(sha256sum "$root/xrizer/bin/linux64/vrclient.so" | cut -d' ' -f1) == "$xrizer_sha256" ]] || return 1
    [[ $(sha256sum "$root/opencomposite/bin/linux64/vrclient.so" | cut -d' ' -f1) == "$opencomposite_sha256" ]] || return 1
}

if [[ -z $portable_runtime_dir ]]; then
    portable_runtime_dir="$portable_root/${xrizer_sha256:0:12}-${opencomposite_sha256:0:12}"
    if ! validate_runtime_dir "$portable_runtime_dir"; then
        partial="$portable_runtime_dir.partial.$$"
        trap 'rm -rf -- "${partial:-}"' EXIT HUP INT TERM
        rm -rf -- "$partial"
        "$repo_root/Tools/SkyrimVR/build_portable_openvr_runtimes.sh" --output "$partial"
        validate_runtime_dir "$partial" || die 'portable runtime build did not match reviewed hashes'
        rm -rf -- "$portable_runtime_dir"
        mv -- "$partial" "$portable_runtime_dir"
        partial=''
        trap - EXIT HUP INT TERM
    fi
else
    portable_runtime_dir=$(realpath -e -- "$portable_runtime_dir")
    validate_runtime_dir "$portable_runtime_dir" || die 'supplied portable runtime directory does not match reviewed hashes'
fi

ensure_checkout() {
    local root=$1 repository=$2 revision=$3
    if [[ ! -d $root/.git ]]; then
        [[ ! -e $root ]] || die "source root exists but is not a Git checkout: $root"
        git clone --no-checkout "$repository" "$root"
        git -C "$root" checkout --detach "$revision"
    fi
    [[ $(git -C "$root" rev-parse HEAD) == "$revision" ]] || die "unexpected source revision in $root"
}

ensure_checkout "$xrizer_root" 'https://github.com/Supreeeme/xrizer.git' "$xrizer_revision"
if [[ -z $(git -C "$xrizer_root" diff --binary --no-ext-diff --full-index HEAD) ]]; then
    git -C "$xrizer_root" apply --check "$repo_root/Tools/SkyrimVR/xrizer-skyrimvr-monado.patch"
    git -C "$xrizer_root" apply "$repo_root/Tools/SkyrimVR/xrizer-skyrimvr-monado.patch"
fi
ensure_checkout "$opencomposite_root" 'https://gitlab.com/znixian/OpenOVR.git' "$opencomposite_revision"
git -C "$opencomposite_root" submodule update --init --recursive

install -D -m 0755 "$portable_runtime_dir/xrizer/libxrizer.so" "$xrizer_root/target/release/libxrizer.so"
install -D -m 0755 "$portable_runtime_dir/opencomposite/bin/linux64/vrclient.so" \
    "$opencomposite_root/build/bin/linux64/vrclient.so"

if [[ -z $output ]]; then
    short_head=$(git rev-parse --short=8 HEAD)
    stamp=$(date -u +%Y%m%d%H%M%SZ)
    output="$repo_root/artifacts/SkyrimTogetherVR/review-handoff/SkyrimTogetherVR-local-agent-complete-handoff-${short_head}-${stamp}.zip"
else
    output=$(realpath -m -- "$output")
fi
[[ ! -e $output && ! -e $output.sha256.txt ]] || die "output or sidecar already exists: $output"
mkdir -p -- "$(dirname -- "$output")"

python3 "$repo_root/Tools/SkyrimVR/create_local_agent_handoff.py" \
    --repo "$repo_root" \
    --xrizer-root "$xrizer_root" \
    --opencomposite-root "$opencomposite_root" \
    --gameplay-package "$gameplay_package" \
    --build-evidence "$build_evidence" \
    --output "$output"
python3 "$repo_root/Tools/SkyrimVR/audit_local_agent_handoff.py" "$output"
unzip -tq "$output"
(cd -- "$(dirname -- "$output")" && sha256sum -c "$(basename -- "$output").sha256.txt")

if [[ -n $upload_target ]]; then
    [[ $upload_target == *:* ]] || die '--upload-target must use HOST:DIR/ syntax'
    upload_host=${upload_target%%:*}
    upload_dir=${upload_target#*:}
    [[ $upload_host =~ ^[A-Za-z0-9._-]+$ ]] || die 'upload host contains unsupported characters'
    [[ $upload_dir =~ ^[A-Za-z0-9._/-]+/?$ && $upload_dir != /* && $upload_dir != *..* ]] \
        || die 'upload directory must be a safe home-relative path'
    command -v rsync >/dev/null || die 'rsync is required for upload'
    command -v ssh >/dev/null || die 'ssh is required for upload verification'
    rsync -ah --partial "$output" "$output.sha256.txt" "$upload_target"
    remote_name=$(basename -- "$output")
    ssh "$upload_host" "cd -- '$upload_dir' && sha256sum -c '${remote_name}.sha256.txt'"
    printf 'STVR_REMOTE_HANDOFF=%s%s\n' "$upload_target" "$remote_name"
fi

printf 'STVR_LOCAL_HANDOFF=%s\n' "$output"
printf 'STVR_LOCAL_HANDOFF_SHA256=%s\n' "$(sha256sum "$output" | cut -d' ' -f1)"
