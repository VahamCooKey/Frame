# Licenses and third-party software

Frame is distributed under the GNU General Public License, version 3 only. The complete text is in [LICENSE](../LICENSE). Contributions to Frame are accepted under the same license; see [CONTRIBUTING](../CONTRIBUTING.md).

The Windows preference checksum in `src/platform/UserChoiceHashWin.cpp` and its header is adapted from Mozilla and remains under MPL-2.0. These files can be combined with Frame under MPL's secondary-license provisions. Their source attribution and the complete MPL text are included in the repository and Windows packages. See [third-party notices](../THIRD-PARTY-NOTICES.md).

Frame uses Qt 6.8.3, which is dynamically linked and distributed under the GNU Lesser General Public License version 3. The installer includes a copy of the LGPL text, and the Qt libraries remain replaceable. Qt source and license information are available from [Qt](https://www.qt.io/licensing/open-source-lgpl-obligations). The text is also in [`licenses/LGPL-3.0-only.txt`](../licenses/LGPL-3.0-only.txt).

Frame uses libmpv and the mpv render API. Windows builds bundle a dynamically linked libmpv DLL from the GPL-enabled x86-64 build published by [mpv-winbuild](https://github.com/zhongfly/mpv-winbuild). That build also includes FFmpeg components. The binary build and upstream commit are recorded in [`third_party/VERSIONS.txt`](../third_party/VERSIONS.txt); the upstream build project describes its source components and licenses. Linux packages link to the distribution's libmpv package and rely on that distribution's license and source offers.

The Windows setup and portable packages include Qt's `qt.conf`, Frame's license, the notices, and the pinned third-party version record. Some vector file-icon artwork is based on the Frame player icon. Its preview tooling uses Inter; the font's SIL Open Font License notice is in [`assets/file-icons/Inter-OFL.txt`](../assets/file-icons/Inter-OFL.txt).

The libmpv binary's source offer and license information are published by its upstream build project. See the pinned release details in the package's `VERSIONS.txt`. Qt license texts and corresponding source are available at the Qt LGPL page. Review `THIRD-PARTY-NOTICES.md` for third-party links and package details. Software licenses grant copyright permissions; no brand or trademark rights are implied.
