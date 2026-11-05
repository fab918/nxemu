param(
    [Parameter(Mandatory=$true)][ValidateSet('A-master','B-pr2','C-lto','D-pgo','E-g2','F-pgo','G-tmp','H-unsafe','I-pgofp')][string]$Variant,
    [Parameter(Mandatory=$true)][string]$Game,
    [Parameter(Mandatory=$true)][string]$Label,
    [int]$Seconds = 180,
    [switch]$Unlock,
    # Optional key presses: "delaySeconds:VK[:holdMs]" separated by commas, e.g. "40:67,45:67".
    [string]$Keys = '',
    [string]$Shots = '',
    # PGO training: dump the counters of these instrumented DLLs before stopping nxemu
    [string[]]$PgoSweep = @()
)
$ErrorActionPreference = 'Stop'
if (Get-Process -Name nxemu -ErrorAction SilentlyContinue) { throw 'nxemu is already running' }
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  public static bool Focus(IntPtr h) {
    IntPtr fg = GetForegroundWindow();
    if (fg == h) return true;
    uint me = GetCurrentThreadId(), fgThread = GetWindowThreadProcessId(fg, IntPtr.Zero), target = GetWindowThreadProcessId(h, IntPtr.Zero);
    AttachThreadInput(me, fgThread, true); AttachThreadInput(target, fgThread, true);
    keybd_event(0x12, 0, 0, UIntPtr.Zero); keybd_event(0x12, 0, 2, UIntPtr.Zero);
    ShowWindow(h, 9); BringWindowToTop(h); SetForegroundWindow(h);
    AttachThreadInput(target, fgThread, false); AttachThreadInput(me, fgThread, false);
    return GetForegroundWindow() == h;
  }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
$root = $PSScriptRoot
$folder = Join-Path $root $Variant
$out = Join-Path $root "runs\$Label-$Variant"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$env:NXEMU_PERF_CSV = Join-Path $out 'perf.csv'
if ($Unlock) { $env:NXEMU_PERF_UNLOCK = '1' } else { Remove-Item Env:NXEMU_PERF_UNLOCK -ErrorAction SilentlyContinue }
$p = Start-Process -FilePath (Join-Path $folder 'nxemu.exe') -WorkingDirectory $folder -ArgumentList @('"' + $Game + '"') -PassThru
$events = @()
foreach ($k in ($Keys -split ',' | Where-Object { $_ })) { $f = $k -split ':'; $events += [pscustomobject]@{ t=[double]$f[0]; vk=[byte]$f[1]; hold=$(if ($f.Count -gt 2) { [int]$f[2] } else { 120 }); done=$false } }
$shotTimes = @($Shots -split ',' | Where-Object { $_ } | ForEach-Object { [double]$_ })
$shotDone = @{}
$mem = New-Object System.Collections.Generic.List[string]
$mem.Add('ms,working_set_mb,private_mb,build_processes')
$sw = [Diagnostics.Stopwatch]::StartNew()
$lastMem = -1000
while ($sw.Elapsed.TotalSeconds -lt $Seconds -and -not $p.HasExited) {
    $t = $sw.Elapsed.TotalSeconds
    if ($sw.ElapsedMilliseconds - $lastMem -ge 1000) {
        $p.Refresh(); $lastMem = $sw.ElapsedMilliseconds
        $busy = @(Get-Process cl,link,ninja,clang,clang-cl -ErrorAction SilentlyContinue).Count
        $mem.Add(('{0},{1:F1},{2:F1},{3}' -f $sw.ElapsedMilliseconds, ($p.WorkingSet64/1MB), ($p.PrivateMemorySize64/1MB), $busy))
    }
    foreach ($e in $events) {
        if (-not $e.done -and $t -ge $e.t) {
            $h = $p.MainWindowHandle
            if ($h -ne [IntPtr]::Zero) { [W]::Focus($h) | Out-Null; Start-Sleep -Milliseconds 200 }
            if ([W]::GetForegroundWindow() -eq $h) {
                $scan = [byte][W]::MapVirtualKey($e.vk, 0)
                [W]::keybd_event($e.vk, $scan, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds $e.hold
                [W]::keybd_event($e.vk, $scan, 2, [UIntPtr]::Zero)
                Add-Content (Join-Path $out 'keys.log') ("{0:F1}s vk {1}" -f $t, $e.vk)
            } else {
                Add-Content (Join-Path $out 'keys.log') ("{0:F1}s vk {1} SKIPPED: window not in foreground" -f $t, $e.vk)
            }
            $e.done = $true
        }
    }
    foreach ($s in $shotTimes) {
        if (-not $shotDone.ContainsKey($s) -and $t -ge $s) {
            $shotDone[$s] = $true
            $r = New-Object W+RECT
            if ([W]::GetWindowRect($p.MainWindowHandle, [ref]$r) -and $r.R -gt $r.L) {
                $bmp = New-Object Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
                $g = [Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
                $bmp.Save((Join-Path $out ("shot-{0:000}s.png" -f $s))); $g.Dispose(); $bmp.Dispose()
            }
        }
    }
    Start-Sleep -Milliseconds 100
}
$exited = $p.HasExited
if (-not $exited -and $PgoSweep.Count -gt 0) {
    $sweep = (Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\*\bin\Hostx64\x64\pgosweep.exe' | Select-Object -Last 1).FullName
    foreach ($dll in $PgoSweep) { & $sweep $dll (Join-Path $out ("{0}!1.pgc" -f [IO.Path]::GetFileNameWithoutExtension($dll))) }
}
if (-not $exited) { Stop-Process -Id $p.Id -Force; $p.WaitForExit(10000) | Out-Null }
$mem | Set-Content (Join-Path $out 'memory.csv')
Copy-Item (Join-Path $folder 'user\log\nxemu_log.txt') (Join-Path $out 'nxemu_log.txt') -ErrorAction SilentlyContinue
"$Label $Variant done after $([int]$sw.Elapsed.TotalSeconds)s, exited by itself: $exited"

