#!/usr/bin/env bash
# Frame - self-service verification (POSIX / Linux).
# Mirrors scripts/check.ps1:
#   scripts/check.sh [--fast|-Fast] [--selftest-file|-SelftestFile <path>] [--skip-selftest|-SkipSelftest] [--run|-Run] [--build-dir|-BuildDir <path>]
#
# Exit codes: 10 preflight, 1 configure, 2 build, 3 tests, 4 selftest, 5 run. 0 = all green.
#
# Selftest runs by default (same as check.ps1 since A30) -- it is the only
# check that exercises real QML and the embedded mpv renderer.

set -euo pipefail

FAST=false
SELFTEST_FILE=""
SKIP_SELFTEST=false
RUN=false
BUILD_DIR="build-linux"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fast|-Fast|-fast)
            FAST=true
            shift
            ;;
        --selftest-file|-SelftestFile|-selftest-file)
            [[ $# -ge 2 && -n "$2" ]] || { echo "ERROR: $1 needs a file path." >&2; exit 10; }
            SELFTEST_FILE="$2"
            shift 2
            ;;
        --skip-selftest|-SkipSelftest|-skip-selftest)
            SKIP_SELFTEST=true
            shift
            ;;
        --run|-Run|-run)
            RUN=true
            shift
            ;;
        --build-dir|-BuildDir|-build-dir)
            [[ $# -ge 2 && -n "$2" ]] || { echo "ERROR: $1 needs a directory." >&2; exit 10; }
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: scripts/check.sh [--fast] [--selftest-file <path>] [--skip-selftest] [--run] [--build-dir <dir>]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 10
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"
if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$ROOT/$BUILD_DIR"
fi

# --- preflight (exit 10) ---
if ! command -v cmake >/dev/null 2>&1; then
    echo "ERROR: cmake not found. Please install cmake (e.g. sudo apt install cmake)." >&2
    exit 10
fi

if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    echo "ERROR: C++ compiler (g++ or clang++) not found. Please install a compiler (e.g. sudo apt install g++)." >&2
    exit 10
fi

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "ERROR: pkg-config not found. Please install pkg-config (e.g. sudo apt install pkg-config)." >&2
    exit 10
fi

if ! pkg-config --exists mpv 2>/dev/null; then
    echo "ERROR: libmpv development files not found via pkg-config." >&2
    echo "Please install libmpv-dev (e.g. sudo apt install libmpv-dev) or set PKG_CONFIG_PATH." >&2
    exit 10
fi

# --- configure (exit 1) ---
echo "== configure =="
CMAKE_GEN_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    CMAKE_GEN_ARGS+=(-G "Ninja")
fi

if [[ -n "${QT_DIR:-}" ]]; then
    CMAKE_GEN_ARGS+=("-DCMAKE_PREFIX_PATH=$QT_DIR")
elif [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    CMAKE_GEN_ARGS+=("-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH")
fi

if ! cmake -S "$ROOT" -B "$BUILD_DIR" "${CMAKE_GEN_ARGS[@]}"; then
    echo "ERROR: configure failed." >&2
    exit 1
fi

# --- build (exit 2) ---
echo "== build =="
if ! cmake --build "$BUILD_DIR" --parallel; then
    echo "ERROR: build failed." >&2
    exit 2
fi

if [[ "$FAST" == "true" ]]; then
    echo "build ok"
    if [[ "$RUN" != "true" ]]; then
        exit 0
    fi
fi

# --- tests (exit 3) ---
echo "== tests =="
if ! ctest --test-dir "$BUILD_DIR" --output-on-failure; then
    echo "ERROR: tests failed." >&2
    exit 3
fi

# --- selftest (exit 4) ---
if [[ "$SKIP_SELFTEST" != "true" ]]; then
    if [[ -z "$SELFTEST_FILE" ]]; then
        SELFTEST_FILE="$ROOT/assets/test/sample.mkv"
    fi

    if [[ ! -f "$SELFTEST_FILE" ]]; then
        echo "ERROR: selftest file not found: $SELFTEST_FILE" >&2
        exit 4
    fi

    EXE="$BUILD_DIR/frame"
    if [[ ! -x "$EXE" ]]; then
        echo "ERROR: frame binary not found at: $EXE" >&2
        exit 4
    fi

    echo "== selftest: $SELFTEST_FILE =="
    # If in a headless shell without DISPLAY or WAYLAND_DISPLAY, use xvfb-run if available
    if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
        if command -v xvfb-run >/dev/null 2>&1; then
            if ! xvfb-run -a "$EXE" --selftest "$SELFTEST_FILE"; then
                echo "ERROR: selftest failed under xvfb-run." >&2
                exit 4
            fi
        else
            if ! "$EXE" --selftest "$SELFTEST_FILE"; then
                echo "ERROR: selftest failed (no DISPLAY or WAYLAND_DISPLAY and xvfb-run not installed)." >&2
                exit 4
            fi
        fi
    else
        if ! "$EXE" --selftest "$SELFTEST_FILE"; then
            echo "ERROR: selftest failed." >&2
            exit 4
        fi
    fi
fi

# --- run (exit 5) ---
if [[ "$RUN" == "true" ]]; then
    EXE="$BUILD_DIR/frame"
    if [[ ! -x "$EXE" ]]; then
        echo "ERROR: frame binary not found at: $EXE" >&2
        exit 5
    fi
    echo "== run: $EXE (close the window to finish) =="
    if ! "$EXE"; then
        echo "ERROR: run failed." >&2
        exit 5
    fi
fi

echo "all green"
exit 0

