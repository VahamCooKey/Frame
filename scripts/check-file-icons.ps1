param([string]$IconDir = "")
$ErrorActionPreference = 'Stop'
if (-not $IconDir) {
    $IconDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets\file-icons'
}
$interopSource = @'
using System;
using System.Runtime.InteropServices;
public static class FrameFileIconCheck {
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadImage(IntPtr instance, string name, uint type, int cx, int cy, uint flags);
    [DllImport("user32.dll")]
    public static extern bool DestroyIcon(IntPtr icon);
}
'@
if (-not ('FrameFileIconCheck' -as [type])) { Add-Type -TypeDefinition $interopSource }
$manifest = Get-Content -LiteralPath (Join-Path $IconDir 'formats.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$sizes = 16,20,24,32,40,48,64,96,128,256
$verified = 0
foreach ($format in $manifest.formats) {
    $path = (Resolve-Path -LiteralPath (Join-Path $IconDir ($format.id + '.ico'))).Path
    foreach ($size in $sizes) {
        $handle = [FrameFileIconCheck]::LoadImage([IntPtr]::Zero, $path, 1, $size, $size, 0x10)
        if ($handle -eq [IntPtr]::Zero) {
            throw "Cannot load $path at $size px; Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
        try { $verified++ }
        finally {
            if (-not [FrameFileIconCheck]::DestroyIcon($handle)) { throw 'Could not release icon handle.' }
        }
    }
}
Write-Output "Windows loaded all $verified icon/size combinations; all handles released."
