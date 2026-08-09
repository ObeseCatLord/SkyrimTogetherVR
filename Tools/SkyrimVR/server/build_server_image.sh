#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
image=${1:-skyrim-together-vr-server:local}

validate_provenance() {
    local name=$1
    local value=$2

    if [[ -z $value || $value == "none" || $value == unknown-* || ! $value =~ ^[A-Za-z0-9][A-Za-z0-9._/-]*$ ]]; then
        printf 'Refusing to build: invalid %s provenance: %q\n' "$name" "$value" >&2
        exit 2
    fi
}

if [[ -n $(git -C "$repo_root" status --porcelain=v1 --untracked-files=all) ]]; then
    echo "Refusing to build a server image from a dirty source tree." >&2
    exit 2
fi

branch=$(git -C "$repo_root" rev-parse --abbrev-ref HEAD)
version=$(git -C "$repo_root" describe --tags)
validate_provenance "branch" "$branch"
validate_provenance "network version" "$version"
printf 'Server image provenance: branch=%s version=%s\n' "$branch" "$version"

build_args=(
    build
    --build-arg GITHUB_ACTIONS=true
    --build-arg "STVR_BUILD_BRANCH=$branch"
    --build-arg "STVR_BUILD_COMMIT=$version"
    -t "$image"
)

if docker buildx version >/dev/null 2>&1; then
    DOCKER_BUILDKIT=1 docker "${build_args[@]}" "$repo_root"
    exit
fi

cacheless=$(mktemp "${TMPDIR:-/tmp}/stvr-server-Dockerfile.XXXXXX")
trap 'rm -f -- "$cacheless"' EXIT

awk '
    /^RUN --mount=type=cache,target=\/root\/\.xmake\/packages/ {
        print "RUN source ~/.xmake/profile && \\"
        replacing = 1
        replaced = 1
        next
    }
    replacing && /^[[:space:]]*--mount=type=cache/ { next }
    replacing && /^[[:space:]]*source ~\/\.xmake\/profile/ {
        replacing = 0
        next
    }
    { print }
    END {
        if (!replaced || replacing) {
            exit 2
        }
    }
' "$repo_root/Dockerfile" > "$cacheless"

echo "Docker BuildKit/buildx is unavailable; building without xmake cache mounts."
docker "${build_args[@]}" -f "$cacheless" "$repo_root"
