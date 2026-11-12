# Building Frame

Frame needs a C++20 compiler, CMake 3.24 or newer, Qt 6.8 or newer, and libmpv development files. Windows release builds use Visual Studio 2022 or newer. Linux release packages are built and tested on Ubuntu 26.04 x86-64.

## Windows

Install Visual Studio 2022 or newer with the **Desktop development with C++** workload, a recent Windows SDK, and 7-Zip. From PowerShell in the repository folder:

```powershell
scripts\bootstrap.ps1
scripts\check.ps1
```

The bootstrap downloads Qt and a prebuilt libmpv development package into ignored `third_party` folders. The check script configures, builds, runs the tests, and plays the included sample in its self-test window.

For an existing checkout, close Frame and run `scripts\bootstrap.ps1 -RefreshMpv` when the bootstrap reports a different playback-library pin. The current pin includes the Windows audio-device shutdown fix; rebuilding the app alone does not replace an older library in `third_party`.

## Ubuntu and other Linux distributions

Install a C++20 compiler, Qt 6.8+, Qt Quick Controls and Dialogs, CMake, Ninja, pkg-config, and libmpv development files. On an Ubuntu release with those packages:

```sh
sudo apt install build-essential cmake ninja-build pkg-config libmpv-dev qt6-base-dev qt6-declarative-dev qt6-svg-dev dbus-daemon
scripts/bootstrap.sh
scripts/check.sh
```

The bootstrap prints additional Qt QML packages where the distribution separates them. Frame has been exercised on Linux with X11 and Wayland. Package names and available Qt versions differ by distribution; Ubuntu 26.04 is the supported `.deb` target.

Ubuntu 26.04's PipeWire client has a shutdown race when no audio server is
available. Until the distribution includes the upstream fix, build Frame's
private client runtime before testing or packaging:

```sh
sudo apt install meson curl patch
bash scripts/bootstrap-pipewire.sh
export LD_LIBRARY_PATH="$PWD/third_party/pipewire-linux${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
scripts/check.sh
bash scripts/test-linux-audio-startup.sh build-linux
bash scripts/package-deb.sh build-linux
```

The `.deb` launcher selects this library only for Frame; system audio services
remain unchanged. See [the diagnosis and verification](audits/2026-11-12-linux-audio-hang.md).

## Tests

Linux desktop integration uses Qt D-Bus, included by `qt6-base-dev`; packages
also depend on `libqt6dbus6`. `dbus-run-session` gives desktop-service tests a
private bus. Configure with `-DFRAME_ENABLE_RENDER_TESTS=ON` to register native
window/media-control and rendered-video checks; those need a desktop session.

`scripts/check.ps1` (Windows) and `scripts/check.sh` (Linux) run the build, automated tests, and playback self-test. For a quick compile without the full checks, pass `-Fast` or `--fast`. See [GitHub Actions](../.github/workflows/ci.yml) for the release-build matrix.

## Release packages

Push a version tag such as `v0.8.0` to build the Windows setup and portable ZIP, and the Ubuntu `.deb`. GitHub Actions attaches those files to a GitHub Release.
