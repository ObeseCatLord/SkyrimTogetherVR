#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
unit_dir="$HOME/.config/systemd/user"
mkdir -p "$unit_dir"

ln -sfn "$repo_root/Tools/SkyrimVR/systemd/skyrim-together-vr-build-cleanup.service" \
    "$unit_dir/skyrim-together-vr-build-cleanup.service"
ln -sfn "$repo_root/Tools/SkyrimVR/systemd/skyrim-together-vr-build-cleanup.timer" \
    "$unit_dir/skyrim-together-vr-build-cleanup.timer"

systemctl --user daemon-reload
systemctl --user enable skyrim-together-vr-build-cleanup.timer
systemctl --user restart skyrim-together-vr-build-cleanup.timer
systemctl --user status --no-pager skyrim-together-vr-build-cleanup.timer
