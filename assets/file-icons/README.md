# Frame Windows file icons

File-type icons for every video and audio format Frame registers, drawn as a
"lit screen". The colour tells you the **kind** and the label tells you the
**format**:

- **Field (top):** the kind colour. Video fields are deep, cool shades with a
  paper (`#F5F4F0`) play mark. Audio fields are bright, warm shades with a
  charcoal (`#202020`) waveform and rounder top corners. Kind is carried
  three ways (hue, lightness and glyph polarity, corner shape), so it
  survives greyscale and colour-vision deficiencies.
- **Plinth (bottom):** charcoal, with the format label in paper letters.

The play mark is the exact path from the user's edited
`frame-player-icon-v1.svg`. The source SVG is read, never modified. At 64 px
and up the field is the brand's 208 × 144 screen.

Open `preview.html` to compare the exported images on dark and light
backgrounds. It is self-contained and works offline.

## Formats

Each kind has three sub-shades with a fixed logic, plus a muted generic
shade. Video shades are mainstream, open and legacy. Audio shades are lossy,
lossless and open.

| Kind | Shade | Colour | Formats (16 px label) |
|---|---|---|---|
| Video | Mainstream | Blue `#2A6CC9` | MP4, M4V, MOV, 3GP |
| Video | Open | Teal `#00789F` | MKV, WEBM (WEB), OGV |
| Video | Legacy | Violet `#5D4FB9` | AVI, WMV, FLV, MPEG (MPG), TS/M2TS |
| Video | Generic | Slate `#5D708C` | VIDEO (VID) |
| Audio | Lossy | Amber `#FFAD46` | MP3, AAC, M4A |
| Audio | Lossless | Coral `#FF8E60` | FLAC (FLA), WAV |
| Audio | Open | Gold `#F0CF4C` | OGG, OPUS (OPU) |
| Audio | Generic | Sand `#D3BDA6` | AUDIO (AUD) |

The generator refuses colours that miss these contrast floors:

- paper on every video field: at least 4.5:1 (weakest 4.56:1)
- charcoal on every audio field: at least 7:1 (weakest 7.21:1)
- paper label on charcoal: at least 7:1 (14.80:1)
- audio keyline against white: at least 3:1 (weakest 3.35:1)

The review's open teal `#0081AB` (4.04:1) was darkened to `#00789F` at the
same hue so it meets the 4.5:1 floor.

## Sizes and files

Each `.ico` contains separate transparent RGBA images at **16, 20, 24, 32, 40,
48, 64, 96, 128, and 256 px**. There are four renderings:

- **16, 20 and 24 px** are drawn pixel by pixel with Pillow, with no smoothing
  except the 24 px play mark.
  - 16 px: field rows 0–8 and a 4 × 7 play mark. The waveform has bars 3/5/7/5/3
    tall with 1 px gaps.
  - 20 px: field rows 0–10 and a 5 × 9 play mark.
  - Labels use a 3 × 5 pixel font at 16 and 20 px; M and W are 5 px wide so
    they never read as H. 24 px uses a 5 × 7 font. 16 and 24 px show up to
    three letters, 20 px up to four.
  - The generator checks that every pair of letters used differs by at least
    2 px. There is no SVG for these sizes.
- **32, 40 and 48 px** (`compact/*.svg`) are drawn with every edge on the pixel
  grid of each size. The labels are outlined Inter Bold with a 9 px cap at 32
  and a 14 px cap at 48. `compact/<id>.svg` is the 48 px drawing; 32 and 40 px
  use the same rules and are written only to the build folder.
- **64–256 px** (`*.svg`) are drawn on an 8-unit grid, which falls on whole
  pixels at 64, 96, 128 and 256. The field is the brand screen and the plinth has
  the brand's `#5C5C5C` rim.

From 24 px up, audio fields carry a darker keyline (same hue) so the bright
colour holds on white backgrounds.

All lettering in the SVGs is converted to paths. There are no font
dependencies, embedded raster images, gradients, or effects. The icon sizes
follow [Microsoft's Windows icon guidance](https://learn.microsoft.com/en-us/windows/win32/menurc/about-icons).

## Windows use

`installer/MediaAssociations.iss` gives every Frame ProgID a `DefaultIcon`
that points at the matching ICO under `{app}\icons\3\`. `.m2ts` shares the TS
icon, and `.mpg`/`.mpeg` share the MPEG icon; the mapping lives in
`formats.json`.

The folder name carries the pack generation (`generation` in `formats.json`)
because Explorer caches icons by path and index, so a redrawn icon at an old
path keeps showing the old artwork. When the artwork changes, bump
`generation` and the installer folder together; setup also deletes the
previous generation's folder. Setup and uninstall send `SHCNE_ASSOCCHANGED` so
Explorer re-reads the registrations.

Explorer shows a ProgID's icon only for types where that ProgID is the user's
chosen handler, so these icons appear for the formats the user has set Frame
as the default for. Frame never alters another player's ProgIDs; see
[Microsoft's application registration documentation](https://learn.microsoft.com/en-us/windows/win32/shell/app-registration).

## Rebuild from the edited SVG

This is for development only. You need Python with `fontTools` and `Pillow`,
an Inter Bold font, and the project's Qt SDK with its Svg module. The player
gains no runtime dependency.

```powershell
$qtDir = Join-Path $PWD 'third_party/Qt/6.8.3/msvc2022_64'
cmake -S tools/icon-export -B build-ui/icon-export "-DCMAKE_PREFIX_PATH=$qtDir"
cmake --build build-ui/icon-export --config Release
$env:PATH = (Join-Path $PWD 'third_party/Qt/6.8.3/msvc2022_64/bin') + ';' + $env:PATH
$env:QT_PLUGIN_PATH = Join-Path $PWD 'third_party/Qt/6.8.3/msvc2022_64/plugins'
python -I scripts/generate-file-icons.py --renderer build-ui/icon-export/Release/frame-icon-export.exe --font C:/Windows/Fonts/Inter-Bold.ttf
./scripts/check-file-icons.ps1
```

`generate-file-icons.py` writes the SVGs, ICOs and offline preview. It keeps
intermediate PNGs and the 32/40 px compact SVGs under the ignored
`build-ui/file-icons` folder. The source path, palette, labels and extension
mapping are in `formats.json`.

## Verification — 2026-11-05 (generation 3)

- 21 icon families (13 video, 8 audio) at ten sizes each. Windows `LoadImage`
  loaded all **210 icon/size combinations** and released every handle
  (`scripts/check-file-icons.ps1`).
- The user's edited SVG is read only; its SHA256 is unchanged:
  `fba998680b0e59618c43dce74e68483ea551b5534ba09303847c2758e5a43851`.
- Design review 1 acceptance checks, measured from the ICO frames:

  | Check | Result |
  |---|---|
  | Kind colour coverage at 16/20/24/32 px | at least 46.4 / 47.7 / 45.7 / 47.3 % |
  | MP4 vs MP3 pixel difference at 16 px | 57.9 % |
  | Hue gap between kinds / mean lightness gap | 117° / 0.29 |
  | Greyscale squint gap (dark / light background) | 0.036 / 0.048 |
  | Letter pairs | all differ by at least 2 px |

Lettering was outlined from Inter Bold 3.019, Copyright 2020 The Inter Project
Authors. Inter is licensed under SIL OFL 1.1; its license is included as
`Inter-OFL.txt`. No font binary is bundled.
