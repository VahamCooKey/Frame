# Run on a disposable Windows CI runner: exercises real install/upgrade/uninstall.
param([Parameter(Mandatory)][string]$Installer)
$ErrorActionPreference = 'Stop'
# A deployed app must work without Qt or runtime SDK directories on PATH.
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$root = Split-Path -Parent $PSScriptRoot
$Installer = (Resolve-Path -LiteralPath $Installer).Path
$appKey = 'HKCU:\Software\Classes\Applications\frame.exe'
$uninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{FB829B86-44D9-4F2D-BAF2-49D54D793E15}_is1'
if ((Test-Path $appKey) -or (Test-Path 'HKCU:\Software\Frame\Capabilities') -or
    (Test-Path "HKCU:\$uninstallKey") -or (Test-Path "HKLM:\$uninstallKey")) {
    throw 'Use a clean test account without an existing Frame installation.'
}
$extensions = [regex]::Matches(
    (Get-Content -Raw -LiteralPath (Join-Path $root 'installer\MediaAssociations.iss')),
    'ValueName: "(\.[a-z0-9]+)"; ValueData: "Frame\.') |
    ForEach-Object { $_.Groups[1].Value }
if ($extensions.Count -ne 21) { throw 'Expected 21 supported media types.' }
$installDir = Join-Path $env:TEMP ('Frame installer test ' + [guid]::NewGuid().ToString('N'))
$sentinel = 'FrameInstallerSmoke.OtherPlayer'
$oldDefaults = @{}
function Get-DefaultSnapshot($Extension) {
    $key = Get-Item "HKCU:\Software\Classes\$Extension" -ErrorAction SilentlyContinue
    $choice = Get-Item "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$Extension\UserChoice" -ErrorAction SilentlyContinue
    # Empty and absent registry data both mean no selected handler. Keep all
    # meaningful ProgIDs and protected UserChoice data for exact comparison.
    return [ordered]@{
        ExtensionDefault = '' + $(if ($key) { $key.GetValue('') })
        UserProgId = '' + $(if ($choice) { $choice.GetValue('ProgId') })
        UserHash = '' + $(if ($choice) { $choice.GetValue('Hash') })
    }
}
foreach ($ext in $extensions) { $oldDefaults[$ext] = Get-DefaultSnapshot $ext }
$otherHandlers = 'HKCU:\Software\Classes\.mp4\OpenWithProgids'
New-Item -Path $otherHandlers -Force | Out-Null
New-ItemProperty -LiteralPath $otherHandlers -Name $sentinel -Value '' -PropertyType String | Out-Null

function Assert-Value($Path, $Name, $Expected) {
    $key = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($key.GetValue($Name) -cne $Expected) { throw "Incorrect registry value: $Path / $Name" }
}
function Uninstall-Frame {
    $uninstaller = Join-Path $installDir 'unins000.exe'
    if (Test-Path -LiteralPath $uninstaller) {
        $p = Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -WindowStyle Hidden -PassThru -Wait
        if ($p.ExitCode -ne 0) { throw "Uninstaller failed: $($p.ExitCode)" }
    }
}

try {
    # Installing twice also covers upgrading an existing installation in place.
    foreach ($attempt in 1..2) {
        $p = Start-Process -FilePath $Installer -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/TASKS=desktopicon',('/DIR="' + $installDir + '"') -WindowStyle Hidden -PassThru -Wait
        if ($p.ExitCode -ne 0) { throw "Installer failed: $($p.ExitCode)" }
        Assert-Value 'HKCU:\Software\RegisteredApplications' 'Frame' 'Software\Frame\Capabilities'
        Assert-Value $appKey 'FriendlyAppName' 'Frame'
        foreach ($dll in @('opengl32sw.dll', 'vulkan-1.dll')) {
            if (-not (Test-Path -LiteralPath (Join-Path $installDir $dll))) { throw "Missing packaged runtime: $dll" }
        }
        foreach ($ext in $extensions) {
            $progId = 'Frame' + $ext
            Assert-Value 'HKCU:\Software\Frame\Capabilities\FileAssociations' $ext $progId
            Assert-Value "$appKey\SupportedTypes" $ext ''
            Assert-Value "HKCU:\Software\Classes\$ext\OpenWithProgids" $progId ''
            Assert-Value "HKCU:\Software\Classes\$progId\shell\open\command" '' ('"' + $installDir + '\frame.exe" "%1"')
            $icon = (Get-Item "HKCU:\Software\Classes\$progId\DefaultIcon").GetValue('')
            if (-not (Test-Path -LiteralPath ($icon -replace ',0$','').Trim('"'))) { throw "Missing icon for $ext" }
        }
    }

    # Verify the actual embedded/installed icon, not just an SVG preview.
    $manifest = Get-Content -Raw (Join-Path $root 'assets\branding\app-icon.json') | ConvertFrom-Json
    $installedIcon = Join-Path $installDir 'Frame.ico'
    if ((Get-FileHash -LiteralPath $installedIcon).Hash.ToLowerInvariant() -ne $manifest.iconSha256) { throw 'Installed shortcut icon differs from canonical artwork.' }
    if ((Get-FileHash -LiteralPath (Join-Path $root 'assets\branding\concepts\frame-player-icon-v1.svg')).Hash.ToLowerInvariant() -ne $manifest.sourceSha256) { throw 'App icon export has a stale SVG source.' }
    Add-Type -AssemblyName System.Drawing
    # GDI+'s Icon(file,size) misreads PNG-compressed ICO entries on some .NET
    # runtimes. Use the same Windows loader as Explorer for the reference.
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FrameIconProbe {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadImage(IntPtr instance, string file, uint type, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr icon);
}
'@
    $embedded = [Drawing.Icon]::ExtractAssociatedIcon((Join-Path $installDir 'frame.exe'))
    $iconHandle = [FrameIconProbe]::LoadImage([IntPtr]::Zero, $installedIcon, 1, $embedded.Width, $embedded.Height, 0x10)
    if ($iconHandle -eq [IntPtr]::Zero) { throw 'Windows could not load the installed icon.' }
    $expected = [Drawing.Icon]::FromHandle($iconHandle).Clone()
    [void][FrameIconProbe]::DestroyIcon($iconHandle)
    $actualBitmap = $embedded.ToBitmap()
    $expectedBitmap = $expected.ToBitmap()
    try {
        for ($x = 0; $x -lt $actualBitmap.Width; $x++) {
            for ($y = 0; $y -lt $actualBitmap.Height; $y++) {
                if ($actualBitmap.GetPixel($x,$y).ToArgb() -ne $expectedBitmap.GetPixel($x,$y).ToArgb()) { throw 'Executable icon does not match the edited logo.' }
            }
        }
    } finally { $actualBitmap.Dispose(); $expectedBitmap.Dispose(); $embedded.Dispose(); $expected.Dispose() }
    $shell = New-Object -ComObject WScript.Shell
    foreach ($link in @((Join-Path ([Environment]::GetFolderPath('Programs')) 'Frame\Frame.lnk'), (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Frame.lnk'))) {
        if (-not (Test-Path -LiteralPath $link)) { throw "Missing shortcut: $link" }
        $shortcutIcon = ($shell.CreateShortcut($link).IconLocation -replace ',\s*0$','').Trim('"')
        if (-not $shortcutIcon.EndsWith('\Frame.ico', [StringComparison]::OrdinalIgnoreCase)) { throw 'Shortcut uses a stale icon source.' }
    }

    # Ask the Windows shell to execute Frame's registered verb with a spaced path.
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FrameShellProbe {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct ShellInfo {
        public int cbSize; public uint mask; public IntPtr hwnd;
        public string verb, file, parameters, directory; public int show;
        public IntPtr instance, idList; public string className;
        public IntPtr classKey; public uint hotKey; public IntPtr icon, process;
    }
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool ShellExecuteEx(ref ShellInfo info);
    [DllImport("kernel32.dll")] static extern uint GetProcessId(IntPtr handle);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
    static extern uint AssocQueryString(uint flags, uint kind, string extension, string extra, System.Text.StringBuilder value, ref uint length);
    public static string DefaultProgId(string extension) {
        uint length = 0;
        AssocQueryString(0, 20, extension, null, null, ref length);
        if (length == 0) return "";
        var value = new System.Text.StringBuilder((int)length);
        return AssocQueryString(0, 20, extension, null, value, ref length) == 0 ? value.ToString() : "";
    }
    public static int Open(string path) { return Open(path, false); }
    public static int Open(string path, bool useDefault) {
        var info = new ShellInfo { cbSize = Marshal.SizeOf(typeof(ShellInfo)),
            mask = useDefault ? 0x140u : 0x141u, verb = "open", file = path, className = useDefault ? null : "Frame.mp4", show = 1 };
        if (!ShellExecuteEx(ref info)) throw new System.ComponentModel.Win32Exception();
        try { return (int)GetProcessId(info.process); }
        finally { if (info.process != IntPtr.Zero) CloseHandle(info.process); }
    }
}
'@
    $mediaPath = Join-Path $installDir 'Explorer open test.mp4'
    # Windows may expand TEMP's RUNNER~1 alias to runneradmin. Match the unique
    # directory + filename, not the spelling of the user's parent directory.
    $mediaSuffix = (Split-Path -Leaf $installDir) + '/Explorer open test.mp4'
    Copy-Item -LiteralPath (Join-Path $root 'assets\test\sample.mkv') -Destination $mediaPath
    $shellProcess = Get-Process -Id ([FrameShellProbe]::Open($mediaPath))
    try {
        $log = Join-Path $env:APPDATA 'Frame\logs\frame.log'
        $loaded = $false
        for ($i = 0; $i -lt 40; $i++) {
            Start-Sleep -Milliseconds 250
            if ($shellProcess.HasExited) { throw 'Frame exited before opening the shell-selected video.' }
            if (Test-Path $log) {
                $matches = @(Get-Content -Encoding UTF8 -LiteralPath $log | Where-Object {
                    $_.Contains('fileLoaded signal: ') -and $_.Replace('\','/').EndsWith($mediaSuffix, [StringComparison]::OrdinalIgnoreCase)
                })
                if ($matches.Count) { $loaded = $true; break }
            }
        }
        if (-not $loaded) {
            if (Test-Path $log) { Get-Content -Encoding UTF8 -LiteralPath $log -Tail 60 | Write-Host }
            throw 'Windows shell launch did not load the selected file.'
        }
    } finally {
        if (-not $shellProcess.HasExited) { Stop-Process -Id $shellProcess.Id -Force; $shellProcess.WaitForExit() }
    }
    Remove-Item -LiteralPath $mediaPath

    & (Join-Path $installDir 'frame.exe') --selftest (Join-Path $root 'assets\test\sample.mkv') | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Installed playback self-test failed: $LASTEXITCODE" }

    # Registering Open with can give previously unassociated types a fallback
    # handler. Compare audio with the same registered state, and separately
    # preserve the exact explicit user choices captured before installation.
    $registeredDefaults = @{}
    foreach ($ext in $extensions) { $registeredDefaults[$ext] = [FrameShellProbe]::DefaultProgId($ext) }
    Uninstall-Frame
    if ((Test-Path $appKey) -or (Test-Path 'HKCU:\Software\Frame\Capabilities')) { throw 'Uninstall left Frame application registration.' }
    $registered = Get-Item 'HKCU:\Software\RegisteredApplications'
    if ($registered.GetValueNames() -contains 'Frame') { throw 'Uninstall left Default apps registration.' }
    foreach ($ext in $extensions) {
        $after = ConvertTo-Json -InputObject (Get-DefaultSnapshot $ext) -Compress
        $before = ConvertTo-Json -InputObject $oldDefaults[$ext] -Compress
        if ($after -cne $before) { throw "Changed existing default for ${ext}: before=$before after=$after" }
        if (Test-Path "HKCU:\Software\Classes\Frame$ext") { throw "Uninstall left $ext ProgID." }
        $handlers = Get-Item "HKCU:\Software\Classes\$ext\OpenWithProgids" -ErrorAction SilentlyContinue
        if ($handlers -and ($handlers.GetValueNames() -contains "Frame$ext")) { throw "Uninstall left $ext Open with entry." }
    }
    Assert-Value $otherHandlers $sentinel ''

    # Explicit consent is a separate phase: silent installs above must preserve
    # defaults; /MAKEDEFAULTS=video intentionally selects every video format.
    $effectiveBefore = @{}
    foreach ($ext in $extensions) { $effectiveBefore[$ext] = [FrameShellProbe]::DefaultProgId($ext) }
    $p = Start-Process -FilePath $Installer -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/MAKEDEFAULTS=video',('/DIR="' + $installDir + '"') -WindowStyle Hidden -PassThru -Wait
    if ($p.ExitCode -ne 0) { throw 'Bulk-default installation failed.' }
    $videoTypes = @('.mkv','.mp4','.m4v','.avi','.mov','.webm','.wmv','.flv','.ts','.m2ts','.mpg','.mpeg','.ogv','.3gp')
    foreach ($ext in $videoTypes) {
        if ([FrameShellProbe]::DefaultProgId($ext) -ine "Frame$ext") { throw "Windows did not accept the bulk default for $ext" }
    }
    foreach ($ext in $extensions | Where-Object { $_ -notin $videoTypes }) {
        $actual = [FrameShellProbe]::DefaultProgId($ext)
        if ($actual -ine $registeredDefaults[$ext]) { throw "Video-only setup changed audio defaults for ${ext}: expected=$($registeredDefaults[$ext]) actual=$actual" }
        if ((ConvertTo-Json (Get-DefaultSnapshot $ext) -Compress) -cne (ConvertTo-Json $oldDefaults[$ext] -Compress)) { throw "Video-only setup changed the explicit audio preference for $ext" }
    }
    $defaultMedia = Join-Path $installDir 'Double click default test.mp4'
    Copy-Item -LiteralPath (Join-Path $root 'assets\test\sample.mkv') -Destination $defaultMedia
    $defaultProcess = Get-Process -Id ([FrameShellProbe]::Open($defaultMedia, $true))
    try {
        if (-not $defaultProcess.Path.EndsWith('\frame.exe', [StringComparison]::OrdinalIgnoreCase)) { throw 'Default shell opening selected another player.' }
        $loadedDefault = $false
        $suffix = (Split-Path -Leaf $installDir) + '/Double click default test.mp4'
        for ($i = 0; $i -lt 60; $i++) {
            Start-Sleep -Milliseconds 250
            if ($defaultProcess.HasExited) { throw 'Default-launched Frame exited before loading.' }
            if (@(Get-Content -Encoding UTF8 -LiteralPath $log | Where-Object { $_.Contains('fileLoaded signal: ') -and $_.Replace('\','/').EndsWith($suffix, [StringComparison]::OrdinalIgnoreCase) }).Count) { $loadedDefault = $true; break }
        }
        if (-not $loadedDefault) { throw 'Default file opening did not load the selected video.' }
    } finally {
        if (-not $defaultProcess.HasExited) { Stop-Process -Id $defaultProcess.Id -Force; $defaultProcess.WaitForExit() }
    }
    Remove-Item -LiteralPath $defaultMedia
    Uninstall-Frame
    foreach ($ext in $extensions) {
        if ([FrameShellProbe]::DefaultProgId($ext) -ine $effectiveBefore[$ext]) { throw "Uninstall did not restore the previous default for $ext" }
    }
    Assert-Value $otherHandlers $sentinel ''
    Write-Host 'PASS: canonical executable/shortcut icons, install/upgrade, 21 registrations, explicit bulk video defaults, actual default shell opening, installed playback, uninstall/default restoration and other handlers.'
} finally {
    Uninstall-Frame
    Remove-ItemProperty -LiteralPath $otherHandlers -Name $sentinel -ErrorAction SilentlyContinue
}
