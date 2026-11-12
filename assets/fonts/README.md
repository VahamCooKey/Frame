# Bundled interface fonts

Frame bundles unmodified **Inter 4.1** static TrueType faces for interface
text. Regular (400), Medium (500) and SemiBold (600) cover the explicit UI
weights. They are application-local Qt resources, not installed OS fonts.
Subtitle rendering retains its own fonts and styling. Qt fallback remains
available for characters outside Inter's coverage.

Source: https://github.com/rsms/inter/releases/tag/v4.1

Archive: https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip

Archive SHA-256:
`9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e`

Downloaded from the official release on 2026-11-04. Each font is extracted
verbatim from `extras/ttf/` in that archive. `Inter-OFL.txt` is the archive's
unmodified `LICENSE.txt`, containing the copyright notice and SIL OFL 1.1.

| File | Bytes | SHA-256 |
|---|---:|---|
| Inter-Regular.ttf | 411640 | `40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82` |
| Inter-Medium.ttf | 417300 | `97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872` |
| Inter-SemiBold.ttf | 419744 | `78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3` |

`installInterfaceFonts()` registers these files before QML is instantiated,
then sets the application font using the family reported by Qt. Registration
failure removes partially registered faces and retains the platform default.
No system-font enumeration or background bulk scan is performed by this
helper. Bundling does not eliminate all platform font initialization or
fallback costs, and rendering can still differ with DPI and rasterization.
