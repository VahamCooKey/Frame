# Frame - dependency bootstrap (Qt 6.8.3 + libmpv).
# Idempotent: existing dependencies stay unless -RefreshMpv is requested.
param([switch]$RefreshMpv)

$ErrorActionPreference = 'Stop'

$scriptsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $scriptsDir
Set-Location -LiteralPath $root

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$qtTag = "6.8.3"
$qtDir = Join-Path $root "third_party\Qt\$qtTag\msvc2022_64"
$mpvDir = Join-Path $root "third_party\mpv"
$mpvDll = Join-Path $mpvDir "libmpv-2.dll"
$mpvLib = Join-Path $mpvDir "libmpv-2.lib"
$versionsFile = Join-Path $root "third_party\VERSIONS.txt"

$thirdPartyDir = Join-Path $root "third_party"
if (-not (Test-Path -LiteralPath $thirdPartyDir)) {
    New-Item -ItemType Directory -Path $thirdPartyDir | Out-Null
}

# --- Qt 6.8.3 (win64_msvc2022_64) via aqtinstall ---
$qtQmake = Join-Path $qtDir "bin\qmake.exe"
if (Test-Path -LiteralPath $qtQmake) {
    Write-Host "Qt $qtTag already present, skipping install."
} else {
    Write-Host "Qt $qtTag not found; installing via aqtinstall..."
    python -m pip install --quiet aqtinstall
    if ($LASTEXITCODE -ne 0) { throw "pip install aqtinstall failed." }
    python -m aqt install-qt windows desktop $qtTag win64_msvc2022_64 -m qtshadertools -O "third_party\Qt"
    if ($LASTEXITCODE -ne 0) { throw "aqt install-qt failed." }
    if (-not (Test-Path -LiteralPath $qtQmake)) { throw "Qt install finished but qmake.exe is still missing at $qtQmake" }
}

# --- libmpv (zhongfly/mpv-winbuild dev drop) ---
# Includes mpv 4dfee616: fixes Windows shutdown after audio-device enumeration.
$mpvAssetName = "mpv-dev-x86_64-20261024-git-a39482e7a5.7z"
$mpvTag = "2026-10-24-a39482e7a5"
$mpvAssetSha256 = "ac90762775ffe0257e4fff8b4116d8b104620dd628ed70899e84e223c4cff5c0"
$mpvPinnedInstall = $false

if ($RefreshMpv -or -not (Test-Path -LiteralPath $mpvDll)) {
    if (-not (Test-Path -LiteralPath $mpvDir)) {
        New-Item -ItemType Directory -Path $mpvDir | Out-Null
    }

    Write-Host "Retrieving pinned zhongfly/mpv-winbuild release $mpvTag..."
    $mpvDownloadUrl = "https://github.com/zhongfly/mpv-winbuild/releases/download/$mpvTag/$mpvAssetName"

    $tempDir = Join-Path $env:TEMP ("frame-mpv-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $tempDir | Out-Null
    try {
        $archivePath = Join-Path $tempDir $mpvAssetName
        Write-Host "Downloading $mpvAssetName..."
        Invoke-WebRequest -Uri $mpvDownloadUrl -OutFile $archivePath -Headers @{ "User-Agent" = "frame-bootstrap" }
        $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $mpvAssetSha256) { throw "SHA-256 mismatch for $mpvAssetName. Expected $mpvAssetSha256, received $actualHash." }
        $mpvPinFile = Join-Path $mpvDir ".frame-mpv-pin"
        $sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
        $sevenZipPath = if ($sevenZipCommand) { $sevenZipCommand.Source } else { $null }
        if (-not $sevenZipPath) {
            foreach ($candidate in @(
                (Join-Path $env:ProgramFiles "7-Zip\7z.exe"),
                (Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe")
            )) {
                if (Test-Path -LiteralPath $candidate) {
                    $sevenZipPath = $candidate
                    break
                }
            }
        }
        if (-not $sevenZipPath) {
            throw "7-Zip is required to unpack $mpvAssetName. Install 7-Zip and rerun bootstrap.ps1."
        }
        Write-Host "Extracting into third_party\mpv..."
        & $sevenZipPath x $archivePath "-o$mpvDir" -y
        if ($LASTEXITCODE -ne 0) { throw "7-Zip extraction failed for $mpvAssetName." }
        [IO.File]::WriteAllText($mpvPinFile, "$mpvTag`n$mpvAssetName`n$mpvAssetSha256`n", [Text.UTF8Encoding]::new($false))
        $mpvPinnedInstall = $true
    } finally {
        if (Test-Path -LiteralPath $tempDir) {
            Remove-Item -LiteralPath $tempDir -Recurse -Force
        }
    }

    if (-not (Test-Path -LiteralPath $mpvDll)) { throw "Extraction finished but libmpv-2.dll is still missing at $mpvDll" }
} else {
    Write-Host "libmpv-2.dll already present, skipping download."
    $mpvPinFile = Join-Path $mpvDir ".frame-mpv-pin"
    if ((Test-Path -LiteralPath $mpvPinFile) -and ((Get-Content -LiteralPath $mpvPinFile -Raw) -match [regex]::Escape($mpvAssetSha256))) {
        $mpvPinnedInstall = $true
    } else {
        Write-Warning "Existing libmpv does not match Frame's tested pin. Close Frame and rerun scripts\bootstrap.ps1 -RefreshMpv to update it."
    }
}

# --- Import library: generate libmpv-2.lib from the DLL exports ---
if (-not (Test-Path -LiteralPath $mpvLib)) {
    if (-not (Test-Path -LiteralPath $mpvDll)) { throw "libmpv-2.dll missing; cannot generate import library." }

    Write-Host "libmpv-2.lib not found; generating from DLL exports..."
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere.exe not found; install Visual Studio 2022 or newer." }

    $vsPath = & $vswhere -latest -property installationPath
    if (-not $vsPath) { throw "No Visual Studio installation found via vswhere." }

    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

    $dumpbinOutput = cmd /s /c "`"$vcvars`" >nul 2>&1 && dumpbin /exports `"$mpvDll`""
    if ($LASTEXITCODE -ne 0) { throw "dumpbin /exports failed for $mpvDll" }

    # Export lines look like: "ordinal hint RVA name" -> 4 whitespace-separated tokens.
    $symbols = @()
    foreach ($line in $dumpbinOutput) {
        if ($line -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$') {
            $symbols += $Matches[1]
        }
    }
    if ($symbols.Count -eq 0) { throw "No exported symbols parsed from dumpbin output." }

    $defPath = Join-Path $mpvDir "mpv.def"
    $defLines = @("LIBRARY libmpv-2", "EXPORTS")
    foreach ($sym in $symbols) { $defLines += "    $sym" }
    Set-Content -LiteralPath $defPath -Value $defLines -Encoding ASCII

    cmd /s /c "`"$vcvars`" >nul 2>&1 && lib /def:`"$defPath`" /out:`"$mpvLib`" /machine:x64"
    if ($LASTEXITCODE -ne 0) { throw "lib.exe failed to build $mpvLib" }
    if (-not (Test-Path -LiteralPath $mpvLib)) { throw "lib.exe reported success but libmpv-2.lib is missing." }
} else {
    Write-Host "libmpv-2.lib already present, skipping generation."
}

# --- Record versions ---
if ($mpvPinnedInstall) {
    $mpvRecord = "mpv: $mpvAssetName (tag: $mpvTag, archive SHA-256: $mpvAssetSha256, from zhongfly/mpv-winbuild)"
} else {
    $mpvRecord = "mpv: existing local third_party\\mpv library; source build and archive hash unverified"
}
Set-Content -LiteralPath $versionsFile -Value @(
    "qt: $qtTag win64_msvc2022_64 (installed via aqtinstall)",
    $mpvRecord
) -Encoding UTF8

Write-Host "bootstrap complete."
