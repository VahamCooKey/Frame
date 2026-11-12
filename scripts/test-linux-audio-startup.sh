#!/usr/bin/env bash
# Missing audio services must not hang device enumeration or controller teardown.
# Run in a fresh runtime directory; never touch the user's audio server.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$(cd "${1:-$root/build-linux}" && pwd)"
runtime="$(mktemp -d "${TMPDIR:-/tmp}/frame-no-audio.XXXXXX")"
trap 'rm -rf -- "$runtime"' EXIT
export XDG_RUNTIME_DIR="$runtime" QT_QPA_PLATFORM=offscreen
export PULSE_SERVER="unix:$runtime/no-pulse" PIPEWIRE_REMOTE="$runtime/no-pipewire"
cd "$root"
for iteration in $(seq 1 20); do
    echo "Audio startup/teardown regression $iteration/20"
    timeout 45s "$build_dir/test_playback_requests" rapidPauseToggles
done
