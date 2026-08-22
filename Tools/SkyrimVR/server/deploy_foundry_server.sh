#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
remote=${STVR_SERVER_REMOTE:-foundry}
container=${STVR_SERVER_CONTAINER:-skyrim-together-vr}
server_root=${STVR_SERVER_ROOT:-/home/ubuntu/docker/skyrimtogethervr}
remote_build=/home/ubuntu/.cache/skyrim-together-vr/source

usage() {
    cat <<'EOF'
Usage: deploy_foundry_server.sh [image-tag]

Incrementally transfers a clean committed tree (including submodule contents),
builds the server natively on Foundry, and replaces only the STVR container.

Overrides: STVR_SERVER_REMOTE, STVR_SERVER_CONTAINER, STVR_SERVER_ROOT.
EOF
}

if (($# > 1)); then
    usage >&2
    exit 2
fi

for command_name in git rsync ssh; do
    command -v "$command_name" >/dev/null || {
        printf 'Required command is unavailable: %s\n' "$command_name" >&2
        exit 2
    }
done

if [[ -n $(git -C "$repo_root" status --porcelain=v1 --untracked-files=all) ]]; then
    echo "Refusing to deploy from a dirty source tree." >&2
    exit 2
fi

if git -C "$repo_root" submodule status --recursive | grep -Eq '^[+-U]'; then
    echo "Refusing to deploy with missing or mismatched submodules." >&2
    exit 2
fi

branch=$(git -C "$repo_root" rev-parse --abbrev-ref HEAD)
network_version=$(git -C "$repo_root" describe --tags)
source_revision=$(git -C "$repo_root" rev-parse --verify HEAD^{commit})
short_revision=${source_revision:0:8}
image=${1:-skyrim-together-vr-server:${short_revision}-arm64}

case "$remote_build" in
    */.cache/skyrim-together-vr/source) ;;
    *) printf 'Unsafe remote build path: %s\n' "$remote_build" >&2; exit 2 ;;
esac

printf 'Deploying %s (%s) to %s as %s\n' "$network_version" "$source_revision" "$remote" "$image"

ssh "$remote" bash -s -- "$remote_build" <<'REMOTE_PREPARE'
set -euo pipefail
build_root=$1
case "$build_root" in
    */.cache/skyrim-together-vr/source) ;;
    *) echo "Unsafe build path." >&2; exit 2 ;;
esac
mkdir -p "$build_root"
REMOTE_PREPARE

rsync -a --delete --delete-excluded \
    --exclude='/.git/' \
    --exclude='.git' \
    --exclude='/.github/' \
    --exclude='/.pytest_cache/' \
    --exclude='/.xmake/' \
    --exclude='/artifacts/' \
    --exclude='/build/' \
    --exclude='/dist/' \
    --exclude='/handoff/' \
    --exclude='*.log' \
    --exclude='*.zip' \
    "$repo_root/" "$remote:$remote_build/"

ssh "$remote" env \
    STVR_DEPLOY_BUILD_ROOT="$remote_build" \
    STVR_DEPLOY_BRANCH="$branch" \
    STVR_DEPLOY_NETWORK_VERSION="$network_version" \
    STVR_DEPLOY_SOURCE_REVISION="$source_revision" \
    STVR_DEPLOY_IMAGE="$image" \
    STVR_DEPLOY_CONTAINER="$container" \
    STVR_DEPLOY_SERVER_ROOT="$server_root" \
    bash -s <<'REMOTE_DEPLOY'
set -euo pipefail

build_root=$STVR_DEPLOY_BUILD_ROOT
branch=$STVR_DEPLOY_BRANCH
network_version=$STVR_DEPLOY_NETWORK_VERSION
source_revision=$STVR_DEPLOY_SOURCE_REVISION
image=$STVR_DEPLOY_IMAGE
container=$STVR_DEPLOY_CONTAINER
server_root=$STVR_DEPLOY_SERVER_ROOT
old_image=
replaced=false

for command_name in docker jq; do
    command -v "$command_name" >/dev/null || {
        printf 'Required remote command is unavailable: %s\n' "$command_name" >&2
        exit 2
    }
done

if docker container inspect "$container" >/dev/null 2>&1; then
    old_image=$(docker inspect "$container" --format '{{.Config.Image}}')
    docker inspect "$container" | jq -e --arg root "$server_root" '
        .[0].HostConfig.NetworkMode == "host" and
        .[0].HostConfig.RestartPolicy.Name == "unless-stopped" and
        ([.[0].Mounts[] | select(.Destination == "/st-server/config" and .Source == ($root + "/config"))] | length == 1) and
        ([.[0].Mounts[] | select(.Destination == "/st-server/Data" and .Source == ($root + "/Data"))] | length == 1) and
        ([.[0].Mounts[] | select(.Destination == "/st-server/logs" and .Source == ($root + "/logs"))] | length == 1)
    ' >/dev/null || {
        echo "Refusing to replace a container whose runtime layout is not the expected STVR layout." >&2
        exit 2
    }
fi

mkdir -p "$server_root/config" "$server_root/Data" "$server_root/logs"

STVR_BUILD_BRANCH="$branch" \
STVR_BUILD_COMMIT="$network_version" \
STVR_SOURCE_REVISION="$source_revision" \
    "$build_root/Tools/SkyrimVR/server/build_server_image.sh" "$image"

docker image inspect "$image" | jq -e --arg revision "$source_revision" --arg version "$network_version" '
    .[0].Architecture == "arm64" and
    .[0].Config.Labels["org.opencontainers.image.revision"] == $revision and
    .[0].Config.Labels["org.skyrimtogether.network-version"] == $version
' >/dev/null

rollback() {
    status=$?
    if [[ $replaced == true ]]; then
        docker rm -f "$container" >/dev/null 2>&1 || true
        if [[ -n $old_image ]]; then
            echo "New server failed verification; restoring $old_image." >&2
            docker run -d \
                --name "$container" \
                --restart unless-stopped \
                --network host \
                -v "$server_root/config:/st-server/config" \
                -v "$server_root/Data:/st-server/Data" \
                -v "$server_root/logs:/st-server/logs" \
                -v /etc/localtime:/etc/localtime:ro \
                -v /etc/timezone:/etc/timezone:ro \
                "$old_image" >/dev/null
        fi
    fi
    exit "$status"
}
trap rollback ERR

if docker container inspect "$container" >/dev/null 2>&1; then
    docker stop -t 15 "$container" >/dev/null
    docker rm "$container" >/dev/null
    replaced=true
fi

docker run -d \
    --name "$container" \
    --restart unless-stopped \
    --network host \
    -v "$server_root/config:/st-server/config" \
    -v "$server_root/Data:/st-server/Data" \
    -v "$server_root/logs:/st-server/logs" \
    -v /etc/localtime:/etc/localtime:ro \
    -v /etc/timezone:/etc/timezone:ro \
    "$image" >/dev/null
replaced=true

for _ in {1..15}; do
    if [[ $(docker inspect "$container" --format '{{.State.Running}} {{.RestartCount}}') == "true 0" ]] && \
       ss -H -lun | awk '{print $5}' | grep -Eq '(^|:)26099$'; then
        break
    fi
    sleep 1
done

[[ $(docker inspect "$container" --format '{{.State.Running}} {{.RestartCount}}') == "true 0" ]]
ss -H -lun | awk '{print $5}' | grep -Eq '(^|:)26099$'
[[ $(docker ps --filter "name=^/${container}$" --format '{{.Names}}' | wc -l) -eq 1 ]]

trap - ERR
if [[ -n $old_image && $old_image != "$image" ]]; then
    docker image rm "$old_image" >/dev/null 2>&1 || true
fi

echo "Server deployment verified:"
docker ps --filter "name=^/${container}$" --format '  {{.Names}} {{.Image}} {{.Status}}'
docker logs --tail 30 "$container"
REMOTE_DEPLOY
