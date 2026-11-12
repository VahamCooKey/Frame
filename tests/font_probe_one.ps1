param([Parameter(Mandatory=$true)][int]$ProcId, [string]$Label = '')
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System; using System.Text; using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential)]
public struct MBI { public IntPtr BaseAddress; public IntPtr AllocationBase; public uint AllocationProtect;
  public uint __a1; public IntPtr RegionSize; public uint State; public uint Protect; public uint Type; public uint __a2; }
public class Fp {
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a, bool i, int p);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll")] public static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, IntPtr l);
  [DllImport("psapi.dll", CharSet = CharSet.Unicode)] public static extern uint GetMappedFileNameW(IntPtr h, IntPtr a, StringBuilder n, uint s);
}
'@
$h = [Fp]::OpenProcess(0x0410, $false, $ProcId)
$sz = [IntPtr][System.Runtime.InteropServices.Marshal]::SizeOf([type][MBI])
$a = [IntPtr]::Zero; $max = [IntPtr]::new(0x7FFFFFFF0000); $b = 0L; $f = @{}
$sb = New-Object System.Text.StringBuilder 1024
while ([Int64]$a -lt [Int64]$max) {
    $m = New-Object MBI
    if ([Fp]::VirtualQueryEx($h, $a, [ref]$m, $sz) -eq [IntPtr]::Zero) { break }
    $rs = [Int64]$m.RegionSize; if ($rs -le 0) { break }
    if ($m.State -eq 0x1000 -and $m.Type -eq 0x40000 -and [Fp]::GetMappedFileNameW($h, $m.BaseAddress, $sb, 1024) -gt 0) {
        $n = [System.IO.Path]::GetFileName($sb.ToString())
        if ($n -match '\.(ttf|ttc|otf|fon)$' -or $n -match 'FontCache') { $b += $rs; $f[$n] = $true }
    }
    $a = [IntPtr]::new([Int64]$m.BaseAddress + $rs)
}
[Fp]::CloseHandle($h) | Out-Null
$p = Get-Process -Id $ProcId
Write-Output ("  {0,-24} fonts {1,5} files / {2,6} MB   WS {3,7} MB   private {4,7} MB   hnd {5,5}" -f `
    $Label, $f.Count, [math]::Round($b/1MB,1), [math]::Round($p.WorkingSet64/1MB,1), [math]::Round($p.PrivateMemorySize64/1MB,1), $p.HandleCount)
