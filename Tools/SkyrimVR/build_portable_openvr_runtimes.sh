#!/usr/bin/env bash
# Build portable Linux OpenVR runtime loaders for the SkyrimTogetherVR handoff.
set -euo pipefail
IFS=$'\n\t'
export LC_ALL=C

readonly XRIZER_REPOSITORY='https://github.com/Supreeeme/xrizer.git'
readonly XRIZER_REVISION='31319560c1bd0f1e5c16936a946bb1c7295dbfd9'
readonly OPENCOMPOSITE_REPOSITORY='https://gitlab.com/znixian/OpenOVR.git'
readonly OPENCOMPOSITE_REVISION='cff07db75c4823afe93ed7027b03d5f7bc86f164'
readonly BUILDER_IMAGE='rust:1.88-slim-bullseye@sha256:df5e57ec8ee5995138c316c3c35fc115413e0c072c78bd3ee593bc5f22aed512'
readonly GLIBC_MAX_MAJOR=2
readonly GLIBC_MAX_MINOR=31

usage() {
  printf 'Usage: %s --output EMPTY_DIRECTORY\n' "${0##*/}" >&2
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

output_dir=''
while (($#)); do
  case "$1" in
    --output)
      (($# >= 2)) || die '--output requires a directory'
      output_dir=$2
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      usage
      die "unknown argument: $1"
      ;;
  esac
done

[[ -n "$output_dir" ]] || { usage; exit 2; }
command -v docker >/dev/null || die 'docker is required'
command -v mktemp >/dev/null || die 'mktemp is required'
command -v readelf >/dev/null || die 'readelf is required'
command -v sha256sum >/dev/null || die 'sha256sum is required'

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
output_dir=$(realpath -m -- "$output_dir")
[[ "$output_dir" != / ]] || die 'refusing / as output directory'
[[ ! -e "$output_dir" ]] || die "output directory must not already exist: $output_dir"
[[ -d "$(dirname -- "$output_dir")" ]] || die "output parent does not exist: $(dirname -- "$output_dir")"

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/stvr-openvr-build.XXXXXXXX")
builder_tag="stvr-openvr-builder:$(basename -- "$work_dir")"
output_created=0
cleanup() {
  local status=$?
  docker image rm --force "$builder_tag" >/dev/null 2>&1 || true
  rm -rf -- "$work_dir"
  if (( status != 0 && output_created )); then
    rm -rf -- "$output_dir"
  fi
  exit "$status"
}
trap cleanup EXIT HUP INT TERM

cat >"$work_dir/Dockerfile" <<'DOCKERFILE'
ARG BUILDER_IMAGE
FROM ${BUILDER_IMAGE}
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates clang cmake git glslang-tools libgl-dev \
    libglu1-mesa-dev libopenxr-dev libvulkan-dev libx11-xcb-dev ninja-build \
    libxcb-glx0-dev \
    pkg-config python3 && rm -rf /var/lib/apt/lists/*
RUN printf '#!/bin/sh\nexec glslangValidator -V "$@"\n' >/usr/local/bin/glslc \
    && chmod 0755 /usr/local/bin/glslc \
    && test "$(getconf GNU_LIBC_VERSION)" = 'glibc 2.31' \
    && rustc --version | grep -Eq '^rustc 1\.88\.0 '
DOCKERFILE

docker pull --quiet "$BUILDER_IMAGE" >/dev/null
docker build --quiet --build-arg "BUILDER_IMAGE=$BUILDER_IMAGE" --tag "$builder_tag" "$work_dir" >/dev/null

mkdir -- "$output_dir"
output_created=1
docker run --rm --interactive --user "$(id -u):$(id -g)" \
  --env HOME=/tmp --env LC_ALL=C --env SOURCE_DATE_EPOCH=0 --env CARGO_INCREMENTAL=0 \
  --env "XRIZER_REPOSITORY=$XRIZER_REPOSITORY" --env "XRIZER_REVISION=$XRIZER_REVISION" \
  --env "OPENCOMPOSITE_REPOSITORY=$OPENCOMPOSITE_REPOSITORY" \
  --env "OPENCOMPOSITE_REVISION=$OPENCOMPOSITE_REVISION" \
  --env "GLIBC_MAX_MAJOR=$GLIBC_MAX_MAJOR" --env "GLIBC_MAX_MINOR=$GLIBC_MAX_MINOR" \
  --volume "$work_dir:/work" --volume "$output_dir:/out" \
  --volume "$script_dir:/recipe:ro" \
  "$builder_tag" bash -euo pipefail -s <<'BUILD'
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
verify_checkout() {
  local source=$1 repository=$2 revision=$3
  [ "$(git -C "$source" config --get remote.origin.url)" = "$repository" ] || die "unexpected origin for $source"
  [ "$(git -C "$source" rev-parse HEAD)" = "$revision" ] || die "unexpected revision for $source"
  git -C "$source" diff --exit-code >/dev/null
  git -C "$source" diff --cached --exit-code >/dev/null
}
verify_elf() {
  local library=$1 version major minor needed relocation_output
  local -a glibc_versions needed_libraries
  [ -f "$library" ] && [ ! -L "$library" ] || die "not a regular file: $library"
  readelf --file-header "$library" | grep -Fq 'Class:                             ELF64'
  readelf --file-header "$library" | grep -Fq 'Machine:                           Advanced Micro Devices X86-64'
  readelf --file-header "$library" | grep -Eq 'Type:[[:space:]]+DYN[[:space:]]'
  mapfile -t glibc_versions < <(readelf --version-info "$library" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -u)
  ((${#glibc_versions[@]})) || die "$library has no GLIBC symbol-version requirements"
  for version in "${glibc_versions[@]}"; do
    major=${version#GLIBC_}; major=${major%%.*}
    minor=${version#GLIBC_}; minor=${minor#*.}; minor=${minor%%[^0-9]*}
    (( major < GLIBC_MAX_MAJOR || (major == GLIBC_MAX_MAJOR && minor <= GLIBC_MAX_MINOR) )) \
      || die "$library requires $version (maximum GLIBC_${GLIBC_MAX_MAJOR}.${GLIBC_MAX_MINOR})"
  done
  mapfile -t needed_libraries < <(readelf --dynamic "$library" | awk '/NEEDED/ { gsub(/[\[\]]/, "", $NF); print $NF }' | sort -u)
  for needed in "${needed_libraries[@]}"; do
    case "$needed" in
      ld-linux-x86-64.so.2|libOpenGL.so.0|libGLU.so.1|libGLX.so.0|libGL.so.1|libX11.so.6|libc.so.6|libdl.so.2|libgcc_s.so.1|libm.so.6|libpthread.so.0|libstdc++.so.6|libvulkan.so.1)
        ;;
      *) die "$library has non-portable DT_NEEDED dependency: $needed" ;;
    esac
  done
  relocation_output=$(ldd -r -- "$library" 2>&1) \
    || { printf '%s\n' "$relocation_output" >&2; die "dynamic relocation check failed for $library"; }
  if grep -Eq 'undefined symbol:|=> not found' <<<"$relocation_output"; then
    printf '%s\n' "$relocation_output" >&2
    die "$library has unresolved dynamic symbols or missing dependencies"
  fi
}
verify_factory_symbol() {
  local library=$1
  readelf --dyn-syms --wide "$library" \
    | awk '$4 == "FUNC" && $5 == "GLOBAL" && $7 != "UND" && $8 == "HmdSystemFactory" { found=1 } END { exit !found }' \
    || die "$library does not export HmdSystemFactory"
}

git clone --no-checkout "$XRIZER_REPOSITORY" /work/xrizer
git -C /work/xrizer checkout --detach "$XRIZER_REVISION"
verify_checkout /work/xrizer "$XRIZER_REPOSITORY" "$XRIZER_REVISION"
git -C /work/xrizer apply --check /recipe/xrizer-skyrimvr-monado.patch
git -C /work/xrizer apply /recipe/xrizer-skyrimvr-monado.patch
git -C /work/xrizer diff --check
(cd /work/xrizer && cargo test --locked --features static-openxr)
(cd /work/xrizer && cargo clean)
(cd /work/xrizer && cargo build --locked --release --features static-openxr)
install -D -m 0755 /work/xrizer/target/release/libxrizer.so /out/xrizer/libxrizer.so
install -D -m 0755 /work/xrizer/target/release/libxrizer.so /out/xrizer/bin/linux64/vrclient.so

git clone --no-checkout "$OPENCOMPOSITE_REPOSITORY" /work/opencomposite
git -C /work/opencomposite checkout --detach "$OPENCOMPOSITE_REVISION"
git -C /work/opencomposite submodule update --init --recursive
verify_checkout /work/opencomposite "$OPENCOMPOSITE_REPOSITORY" "$OPENCOMPOSITE_REVISION"
git -C /work/opencomposite apply --check /recipe/opencomposite-bullseye.patch
git -C /work/opencomposite apply /recipe/opencomposite-bullseye.patch
git -C /work/opencomposite diff --check
cmake -S /work/opencomposite -B /work/opencomposite/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DERROR_ON_WARNING=OFF -DOC_BACKTRACE=OFF
cmake --build /work/opencomposite/build --parallel
install -D -m 0755 /work/opencomposite/build/bin/linux64/vrclient.so /out/opencomposite/bin/linux64/vrclient.so

verify_elf /out/xrizer/libxrizer.so
verify_factory_symbol /out/xrizer/libxrizer.so
verify_elf /out/xrizer/bin/linux64/vrclient.so
cmp -- /out/xrizer/libxrizer.so /out/xrizer/bin/linux64/vrclient.so
verify_elf /out/opencomposite/bin/linux64/vrclient.so
BUILD

for runtime in opencomposite; do
  library="$output_dir/$runtime/bin/linux64/vrclient.so"
  [[ -f "$library" && ! -L "$library" ]] || die "missing regular output file: $library"
done
for library in "$output_dir/xrizer/libxrizer.so" "$output_dir/xrizer/bin/linux64/vrclient.so"; do
  [[ -f "$library" && ! -L "$library" && -x "$library" ]] || die "missing regular executable output file: $library"
done
cmp -- "$output_dir/xrizer/libxrizer.so" "$output_dir/xrizer/bin/linux64/vrclient.so"
printf 'built: %s\n' "$output_dir/xrizer/libxrizer.so"
printf 'built: %s\n' "$output_dir/xrizer/bin/linux64/vrclient.so"
printf 'built: %s\n' "$output_dir/opencomposite/bin/linux64/vrclient.so"
(cd -- "$output_dir" && find xrizer opencomposite -type f -print0 | sort -z | xargs -0 sha256sum)
