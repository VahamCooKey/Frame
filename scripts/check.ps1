# Frame - self-service verification.
# Usage:
#   scripts\check.ps1 [-Fast] [-SelftestFile <path>] [-Run] [-SkipSelftest]
# Exit codes: 10 preflight, 1 configure, 2 build, 3 tests, 4 selftest, 5 run. 0 = all green.
#
# The selftest runs BY DEFAULT since A30: it is the only check that loads
# real QML, and skipping it silently is how the A30 `Window.icon` regression
# passed two "all green" runs (check.ps1:61 used to selftest only when
# -SelftestFile was given, and it never was).

param(
    [switch]$Fast,
    [string]$SelftestFile = "",
    [switch]$Run,
    # Deliberate opt-out for quick compile-only loops; never for a "green" claim.
    [switch]$SkipSelftest
)

$ErrorActionPreference = 'Stop'

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakeCandidates = @(
    $(if ($cmakeCommand) { $cmakeCommand.Source }),
    "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
$cmake = $cmakeCandidates | Select-Object -First 1
if (-not $cmake) {
    Write-Error "cmake.exe not found. Install CMake or the Visual Studio C++ workload."
    exit 10
}

$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root

$qt = "$PSScriptRoot\..\third_party\Qt\6.8.3\msvc2022_64"
$ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"

# --- configure (exit 1) ---
Write-Host "== configure =="
$generator = if ($cmake -match "\\18\\") { "Visual Studio 18 2026" } else { "Visual Studio 17 2022" }
& $cmake -B build -G $generator -A x64 -DCMAKE_PREFIX_PATH="$qt"
if ($LASTEXITCODE -ne 0) {
    Write-Error "configure failed with generator '$generator'."
    exit 1
}

# --- build (exit 2) ---
Write-Host "== build =="
& $cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    Write-Error "build failed."
    exit 2
}

if ($Fast) {
    Write-Host "build ok"
    if (-not $Run) { exit 0 }
}

# --- tests (exit 3) ---
Write-Host "== tests =="
$previousPath = $env:PATH
$env:PATH = "$qt\bin;$(Join-Path $root 'third_party\mpv');$env:PATH"
& $ctest --test-dir build -C Release --output-on-failure
$testExitCode = $LASTEXITCODE
$env:PATH = $previousPath
if ($testExitCode -ne 0) {
    Write-Error "tests failed."
    exit 3
}

# --- selftest (exit 4) ---
if (-not $SkipSelftest) {
    if ($SelftestFile -eq "") {
        $SelftestFile = Join-Path $root "assets\test\sample.mkv"
    }
    if (-not (Test-Path -LiteralPath $SelftestFile)) {
        Write-Error "selftest file not found: $SelftestFile"
        exit 4
    }
    $exe = Join-Path $root "build\Release\frame.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        Write-Error "frame.exe not found at: $exe"
        exit 4
    }
    Write-Host "== selftest: $SelftestFile =="
    & $exe --selftest $SelftestFile
    if ($LASTEXITCODE -ne 0) {
        Write-Error "selftest failed."
        exit 4
    }
}

# --- run (exit 5) ---
if ($Run) {
    $exe = Join-Path $root "build\Release\frame.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        Write-Error "frame.exe not found at: $exe"
        exit 5
    }
    Write-Host "== run: $exe (close the window to finish) =="
    $proc = Start-Process -FilePath $exe -PassThru -Wait
    Write-Host "frame.exe exited with code $($proc.ExitCode)"
    if ($proc.ExitCode -ne 0) {
        Write-Error "run failed."
        exit 5
    }
}

Write-Host "all green"
exit 0
