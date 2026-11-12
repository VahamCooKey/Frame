# Opening media from Windows

Windows setup registers Frame in **Open with** and **Default apps** for 21 media formats. The setup task **Use Frame for all supported video files** selects the 14 video formats together. Uncheck it to keep your current defaults. Audio defaults have a separate, unchecked task.

The helper runs for the current user, without elevation or policy changes. It checks preference compatibility, saves previous choices, requests the selected defaults and verifies Windows' effective Shell handlers. Windows versions or security policies can reject this compatibility mechanism. An incomplete batch is rolled back where possible; interactive setup then opens the native Default apps page. Frame cannot guarantee automatic defaults on every Windows installation.

Silent installs preserve defaults unless `/MAKEDEFAULTS=video`, `/MAKEDEFAULTS=audio` or `/MAKEDEFAULTS=all` is explicitly supplied. The portable ZIP does not register file types or change defaults.

Supported video extensions: MKV, MP4, M4V, AVI, MOV, WEBM, WMV, FLV, TS, M2TS, MPG, MPEG, OGV, 3GP. Audio: MP3, M4A, FLAC, WAV, AAC, OGG, OPUS.

## Registration and removal

`installer/MediaAssociations.iss` supplies per-user Open with, application ProgIDs and Default apps capabilities. Open commands quote both paths. Every registered video and audio type has its own Explorer icon: a colour-coded screen or disc with the format letters at list sizes, and the charcoal Frame look with a format badge at larger sizes (`assets/file-icons`, installed under `{app}\icons` so a redrawn set never collides with Explorer's icon cache). The app and shortcuts use the canonical Frame logo. Setup and uninstall send `SHCNE_ASSOCCHANGED` so Explorer refreshes the icons. Explorer thumbnails remain available.

The native `frame-associations` helper uses the Mozilla-derived preference checksum, attributed under MPL-2.0. It stores previous per-user choices under `HKCU\Software\Frame\AssociationBackup`. Uninstall restores saved choices only when the current choice still points to Frame, preserving later user changes. Other players' registrations and Frame viewing preferences remain in place.

## Verification

The release job runs `scripts/test-installer.ps1` in a disposable Windows account. It checks install and upgrade, canonical executable and shortcut icons, all 21 registrations, installed playback, a bulk video-default selection, actual default Shell opening, audio-default preservation and uninstall restoration. It also checks that another handler survives. Packages must work without development tools on PATH.

The test refuses an account with an existing Frame installation. Do not run it in your normal account:

```powershell
./scripts/test-installer.ps1 -Installer build/Frame-Setup-0.8.0.exe
```

References: [Microsoft application registration](https://learn.microsoft.com/en-us/windows/win32/shell/app-registration), [Default Programs](https://learn.microsoft.com/en-us/windows/win32/shell/default-programs), and [Mozilla's Windows preference implementation](https://github.com/mozilla/gecko-dev/blob/master/browser/components/shell/WindowsUserChoice.cpp).
