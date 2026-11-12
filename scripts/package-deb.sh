#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build-linux}"
root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(sed -n 's/^project(Frame VERSION \([0-9][0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -n1)"
[[ -n "$version" ]] || { echo "Could not read Frame version" >&2; exit 1; }
arch="$(dpkg --print-architecture)"
deb="$root/build/frame_${version}_${arch}.deb"
# Stage on a native Linux filesystem so permissions survive a checkout on
# Windows/NTFS (for example, a WSL build). dpkg rejects a mode-0777 DEBIAN dir.
stage="$(mktemp -d "${TMPDIR:-/tmp}/frame-deb.XXXXXX")"
trap 'rm -rf -- "$stage"' EXIT
mkdir -p "$(dirname "$deb")"
mkdir -p "$stage/DEBIAN" "$stage/opt/frame" "$stage/usr/bin" \
    "$stage/usr/share/applications" "$stage/usr/share/icons/hicolor/scalable/apps" \
    "$stage/usr/share/doc/frame"

install -m 0755 "$build_dir/frame" "$stage/opt/frame/frame"
# Ship the verified client-side race fix privately, not into the system library
# directory. Refuse to silently build a package with the affected dependency.
audio_runtime="${FRAME_PIPEWIRE_RUNTIME:-$root/third_party/pipewire-linux}"
mkdir -p "$stage/opt/frame/lib"
install -m 0755 "$audio_runtime/libpipewire-0.3.so.0" "$stage/opt/frame/lib/"
install -m 0644 "$audio_runtime/PipeWire-COPYING" "$stage/usr/share/doc/frame/"
install -m 0644 "$audio_runtime/VERSION" "$stage/usr/share/doc/frame/PipeWire-VERSION"
install -m 0644 "$root/assets/input.conf" "$stage/opt/frame/input.conf"
install -m 0644 "$root/LICENSE" "$stage/usr/share/doc/frame/copyright"
install -m 0644 "$root/NOTICE" "$stage/usr/share/doc/frame/NOTICE"
install -m 0644 "$root/THIRD-PARTY-NOTICES.md" "$stage/usr/share/doc/frame/THIRD-PARTY-NOTICES"
install -m 0644 "$root/licenses/LGPL-3.0-only.txt" "$stage/usr/share/doc/frame/LGPL-3.0-only.txt"
install -m 0644 "$root/assets/fonts/Inter-OFL.txt" "$stage/usr/share/doc/frame/Inter-OFL.txt"
install -m 0644 "$root/assets/branding/concepts/frame-player-icon-v1.svg" \
    "$stage/usr/share/icons/hicolor/scalable/apps/frame.svg"
cat > "$stage/usr/bin/frame" <<'WRAPPER'
#!/bin/sh
export LD_LIBRARY_PATH="/opt/frame/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec /opt/frame/frame "$@"
WRAPPER
chmod 0755 "$stage/usr/bin/frame"
cat > "$stage/usr/share/applications/frame.desktop" <<'DESKTOP'
[Desktop Entry]
Name=Frame
Comment=Watch your videos
Exec=frame %U
Icon=frame
Terminal=false
Type=Application
Categories=AudioVideo;Video;Player;
MimeType=video/mp4;video/x-matroska;video/webm;video/quicktime;video/x-msvideo;video/x-ms-wmv;
DESKTOP

cat > "$stage/DEBIAN/control" <<CONTROL
Package: frame
Version: $version
Section: video
Priority: optional
Architecture: $arch
Maintainer: VahamCooKey
Depends: libmpv2, libqt6core6t64, libqt6gui6, libqt6network6, libqt6dbus6, libqt6qml6, libqt6quick6, libqt6quickcontrols2-6, libqt6svg6, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-dialogs, qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtqml-workerscript, qml6-module-qtqml-models
Description: A calm place to watch your videos
 Frame plays local video files and stream URLs with subtitles,
 playlists, and familiar playback controls.
CONTROL

dpkg-deb --root-owner-group --build "$stage" "$deb"
echo "Created $deb"
