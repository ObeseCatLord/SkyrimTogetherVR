#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
image=${1:-skyrim-together-vr-server:local}

validate_provenance() {
    local name=$1
    local value=$2
    local normalized=${value,,}

    if [[ -z $value || $normalized == "none" || $normalized == "unavailable" || $normalized == unknown-* || ! $value =~ ^[A-Za-z0-9][A-Za-z0-9._/-]*$ ]]; then
        printf 'Refusing to build: invalid %s provenance: %q\n' "$name" "$value" >&2
        exit 2
    fi
}

if [[ -n $(git -C "$repo_root" status --porcelain=v1 --untracked-files=all) ]]; then
    echo "Refusing to build a server image from a dirty source tree." >&2
    exit 2
fi

branch=$(git -C "$repo_root" rev-parse --abbrev-ref HEAD)
network_version=$(git -C "$repo_root" describe --tags)
source_revision=$(git -C "$repo_root" rev-parse --verify HEAD^{commit})
validate_provenance "branch" "$branch"
validate_provenance "network version" "$network_version"
if ((${#network_version} > 128)); then
    printf 'Refusing to build: network version provenance exceeds 128 characters.\n' >&2
    exit 2
fi
if [[ ! $source_revision =~ ^[0-9a-fA-F]{40}$ ]]; then
    printf 'Refusing to build: invalid source revision provenance: %q\n' "$source_revision" >&2
    exit 2
fi
printf 'Server image provenance: branch=%s sourceRevision=%s networkVersion=%s\n' "$branch" "$source_revision" "$network_version"

build_args=(
    build
    --build-arg GITHUB_ACTIONS=true
    --build-arg "STVR_BUILD_BRANCH=$branch"
    --build-arg "STVR_BUILD_COMMIT=$network_version"
    --label "org.opencontainers.image.revision=$source_revision"
    --label "org.opencontainers.image.version=$network_version"
    --label "org.skyrimtogether.network-version=$network_version"
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
