# Frame - portable zip packager (v1).
# Stages build\Release (windeployqt already deployed there by check.ps1) and
# zips it with the license files. Run check.ps1 first; this never builds.
#
# Usage: scripts\package.ps1 [-BuildDir <path>]   (default: build\Release)
# Exit codes: 10 preflight, 1 stage, 2 zip. 0 = packaged.
param(
    [string]$BuildDir = ""
)

$ErrorActionPreference = 'Stop'
$scriptsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $scriptsDir

if ($BuildDir -eq "") { $BuildDir = Join-Path $root "build\Release" }

# --- preflight (exit 10) ---
$exe = Join-Path $BuildDir "frame.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Error "frame.exe not found in $BuildDir - run scripts\check.ps1 first."
    exit 10
}
$license = Join-Path $root "LICENSE"
$notice = Join-Path $root "NOTICE"
foreach ($f in @($license, $notice)) {
    if (-not (Test-Path -LiteralPath $f)) {
        Write-Error "missing $f"
        exit 10
    }
}

# Version from the exe's own stamped resource - single source of truth.
$version = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if (-not $version) {
    Write-Error "no ProductVersion stamped on frame.exe - is the .rc wired in?"
    exit 10
}

# --- stage (exit 1) ---
$staging = Join-Path $root ("build\stage\Frame " + $version)
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging -Force | Out-Null

try {
    # Everything windeployqt laid down + the exe + the runtime config file.
    # -Path (not -LiteralPath): the trailing wildcard must expand.
    Copy-Item -Path (Join-Path $BuildDir "*") -Destination $staging -Recurse -Force

    # libmpv loads Vulkan even when Frame renders through OpenGL. Bundle the
    # loader rather than relying on a developer's PATH or a GPU driver install.
    $loader = @($BuildDir) + @($env:PATH -split ';') + @(Join-Path $env:SystemRoot 'System32') |
        Where-Object { $_ } | ForEach-Object { Join-Path $_ 'vulkan-1.dll' } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $loader) { throw 'Missing Vulkan loader; install the Vulkan runtime before packaging.' }
    Copy-Item -LiteralPath $loader -Destination (Join-Path $staging 'vulkan-1.dll') -Force
    if (-not (Test-Path -LiteralPath (Join-Path $staging 'opengl32sw.dll'))) {
        throw 'Missing Qt software OpenGL renderer; rerun the build/deployment step.'
    }

    # Source-tree files the build does not copy.
    Copy-Item -LiteralPath $license -Destination $staging
    Copy-Item -LiteralPath $notice -Destination $staging
    Copy-Item -LiteralPath (Join-Path $root "THIRD-PARTY-NOTICES.md") -Destination $staging
    Copy-Item -LiteralPath (Join-Path $root "third_party\VERSIONS.txt") -Destination $staging
    $licenseDir = Join-Path $staging "licenses"
    New-Item -ItemType Directory -Path $licenseDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root "licenses\LGPL-3.0-only.txt") -Destination $licenseDir
    Copy-Item -LiteralPath (Join-Path $root "licenses\Vulkan-Loader.txt") -Destination $licenseDir
    Copy-Item -LiteralPath (Join-Path $root "licenses\Mesa.txt") -Destination $licenseDir
    Copy-Item -LiteralPath (Join-Path $root "licenses\LLVM.txt") -Destination $licenseDir
    Copy-Item -LiteralPath (Join-Path $root "licenses\MPL-2.0.txt") -Destination $licenseDir
    Copy-Item -LiteralPath (Join-Path $root "assets\fonts\Inter-OFL.txt") -Destination $licenseDir
} catch {
    Write-Error "staging failed: $_"
    exit 1
}

# --- zip (exit 2) ---
$zip = Join-Path $root ("build\Frame-" + $version + "-win-x64.zip")
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
try {
    Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $zip -CompressionLevel Optimal
} catch {
    Write-Error "zip failed: $_"
    exit 2
}

$sizeMb = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)
Write-Host "packaged: $zip ($sizeMb MB)"
exit 0
