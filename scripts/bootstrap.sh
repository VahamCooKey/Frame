#!/usr/bin/env bash
# Frame - dependency bootstrap for Linux.
# Detects distribution and helps install required packages (Qt 6, libmpv, cmake).

set -euo pipefail

echo "== Frame Linux Dependency Bootstrap =="
echo "Frame requires Qt 6.8 or newer. Older distro repositories need a newer Qt SDK (set QT_DIR)."

if command -v apt-get >/dev/null 2>&1; then
    echo "Debian/Ubuntu detected."
    PACKAGES=(
        build-essential
        cmake
        pkg-config
        ninja-build
        libmpv-dev
        qt6-base-dev
        qt6-declarative-dev
        qt6-svg-dev
        qml6-module-qtquick
        qml6-module-qtquick-controls
        qml6-module-qtquick-layouts
        qml6-module-qtquick-templates
        qml6-module-qtquick-window
        qml6-module-qtquick-dialogs
        qml6-module-qtqml-workerscript
        qml6-module-qtqml-models
        qt6-wayland
    )
    echo "Required packages: ${PACKAGES[*]}"
    if [[ "${1:-}" == "--install" || "${1:-}" == "-y" ]]; then
        sudo apt-get update
        sudo apt-get install -y "${PACKAGES[@]}"
    else
        echo "Run with '--install' to install automatically:"
        echo "  sudo apt-get update && sudo apt-get install -y ${PACKAGES[*]}"
    fi
elif command -v pacman >/dev/null 2>&1; then
    echo "Arch Linux detected."
    PACKAGES=(
        base-devel
        cmake
        ninja
        mpv
        qt6-base
        qt6-declarative
        qt6-svg
        qt6-wayland
    )
    echo "Required packages: ${PACKAGES[*]}"
    if [[ "${1:-}" == "--install" || "${1:-}" == "-y" ]]; then
        sudo pacman -S --needed "${PACKAGES[@]}"
    else
        echo "Run with '--install' to install automatically:"
        echo "  sudo pacman -S --needed ${PACKAGES[*]}"
    fi
elif command -v dnf >/dev/null 2>&1; then
    echo "Fedora detected."
    PACKAGES=(
        gcc-c++
        cmake
        ninja-build
        pkgconf-pkg-config
        mpv-libs-devel
        qt6-qtbase-devel
        qt6-qtdeclarative-devel
        qt6-qtsvg-devel
        qt6-qtwayland
    )
    echo "Required packages: ${PACKAGES[*]}"
    if [[ "${1:-}" == "--install" || "${1:-}" == "-y" ]]; then
        sudo dnf install -y "${PACKAGES[@]}"
    else
        echo "Run with '--install' to install automatically:"
        echo "  sudo dnf install -y ${PACKAGES[*]}"
    fi
else
    echo "Unrecognized package manager. Please ensure CMake, C++ compiler, Qt6, and libmpv-dev are installed."
fi

echo "Done."
exit 0

