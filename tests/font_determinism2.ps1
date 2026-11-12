# F44 determinism harness, exact-pid edition.
# The old determinism.sh picked "newest gdplayer by StartTime" at probe time,
# which is wrong whenever any other instance is alive (parallel agents, the
# user). The pid is captured at launch and every measurement targets it, via
# the proven font_probe_one.ps1 (my inline re-type of its VirtualQueryEx loop
# failed intermittently - see the session log - so measurement reuses it).
param([int]$Runs = 1)

Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public class Dw {
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int ht, bool r);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[Dw]::SetProcessDPIAware() | Out-Null

$exe = "D:\Coding Projects\GD Player\build\Release\frame.exe"
$medias = @(
  "D:\Coding Projects\GD Player\build-tmp\media\base_h264_1080p_loop.mp4",
  "D:\Coding Projects\GD Player\build-tmp\media\base_hevc_4k_loop.mp4",
  "D:\Coding Projects\GD Player\assets\test\sample.mkv"
)

for ($r = 1; $r -le $Runs; $r++) {
  foreach ($media in $medias) {
    $name = Split-Path $media -Leaf
    $stamp = Get-Date -Format "HH:mm:ss"
    $p = Start-Process -FilePath $exe -ArgumentList ('"' + $media + '"') -PassThru
    $deadline = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $deadline -and $p.MainWindowHandle -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 100; $p.Refresh() }
    if ($p.MainWindowHandle -ne [IntPtr]::Zero) { [Dw]::MoveWindow($p.MainWindowHandle, 2560, 420, 1600, 900, $true) | Out-Null }
    Start-Sleep -Seconds 13
    $p.Refresh()
    if ($p.HasExited) { Write-Output ("{0} {1,-30} EXITED early rc={2}" -f $stamp, $name, $p.ExitCode); continue }
    Write-Output ("{0} run{1} pid {2} [{3}]" -f $stamp, $r, $p.Id, $name)
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "tests\font_probe_one.ps1" -ProcId $p.Id -Label ("run{0} {1}" -f $r, $name)
    $p.CloseMainWindow() | Out-Null
    if (-not $p.WaitForExit(5000)) { $p.Kill() }
    Start-Sleep -Seconds 3
  }
}
