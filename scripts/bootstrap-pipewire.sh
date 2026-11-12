#!/usr/bin/env bash
# Build only Frame's private PipeWire client library; never install a daemon
# or replace a system library. See docs/audits/2026-11-12-linux-audio-hang.md.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
destination="${1:-$root/third_party/pipewire-linux}"
mkdir -p "$destination"
destination="$(cd "$destination" && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/frame-pipewire.XXXXXX")"
trap 'rm -rf -- "$work"' EXIT
version=1.6.11
archive="$work/pipewire.tar.gz"
curl --fail --location --retry 3 "https://codeload.github.com/PipeWire/pipewire/tar.gz/refs/tags/$version" -o "$archive"
echo "f3f70c4af173cc93d05070a9d412efe3ef5889bdf4ac32298e2c0d9ae1350559  $archive" | sha256sum --check -
mkdir "$work/source"
tar -xf "$archive" --strip-components=1 -C "$work/source"
patch --batch --fuzz=0 -d "$work/source" -p1 < "$root/scripts/patches/pipewire-thread-loop-stop.patch"
multiarch="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"
meson setup "$work/build" "$work/source" --prefix=/usr --libdir="lib/$multiarch" \
    --buildtype=release --wrap-mode=nodownload -Dauto_features=disabled \
    -Ddbus=disabled -Dspa-plugins=enabled -Dpipewire-jack=disabled \
    -Dpipewire-v4l2=disabled -Dexamples=disabled -Dtests=disabled -Dsession-managers=[]
ninja -C "$work/build" -j "${CMAKE_BUILD_PARALLEL_LEVEL:-2}" src/pipewire/libpipewire-0.3.so.0.1611.0
install -m 0755 "$work/build/src/pipewire/libpipewire-0.3.so.0.1611.0" "$destination/libpipewire-0.3.so.0"
install -m 0644 "$work/source/COPYING" "$destination/PipeWire-COPYING"
printf '%s\n' 'PipeWire 1.6.11 + 2aee8b43836597661ab516721014d23753ce7608 (thread-loop stop race)' > "$destination/VERSION"
echo "Frame private audio runtime: $destination"
