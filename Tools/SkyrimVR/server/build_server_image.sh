#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
image=${1:-skyrim-together-vr-server:local}

if [[ -n $(git -C "$repo_root" status --porcelain=v1 --untracked-files=all) ]]; then
    echo "Refusing to build a server image from a dirty source tree." >&2
    exit 2
fi

build_args=(
    build
    --build-arg GITHUB_ACTIONS=true
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
