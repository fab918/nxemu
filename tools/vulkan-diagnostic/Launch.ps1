param([switch]$ValidateOnly, [switch]$NoTrace, [switch]$NoFullMemory, [switch]$RetainCommandHistory, [switch]$GpuCheckpoints)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exe = Join-Path $root 'bin\x64\Release\nxemu.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "Executable absent : $exe" }
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
public static class NxLightDiagnostic {
    const int TraceSize = 64 + 65536 * 128;
    static IntPtr traceMapping;
    static IntPtr traceView;
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr OpenFileMapping(uint access, bool inherit, string name);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr MapViewOfFile(IntPtr mapping, uint access, uint high, uint low, UIntPtr size);
    [DllImport("kernel32.dll")]
    static extern bool UnmapViewOfFile(IntPtr view);
    public static bool TryAttach(int pid) {
        if (traceView != IntPtr.Zero) return true;
        traceMapping = OpenFileMapping(4, false, "Local\\NXEmuVkTrace-" + pid);
        if (traceMapping == IntPtr.Zero) return false;
        traceView = MapViewOfFile(traceMapping, 4, 0, 0, (UIntPtr)TraceSize);
        if (traceView != IntPtr.Zero && Marshal.ReadInt64(traceView) == 0x313052544b56584eL) return true;
        Detach();
        return false;
    }
    public static bool SaveTrace(string path) {
        if (traceView == IntPtr.Zero) return false;
        byte[] data = new byte[TraceSize];
        Marshal.Copy(traceView, data, 0, data.Length);
        File.WriteAllBytes(path, data);
        return true;
    }
    public static void Detach() {
        if (traceView != IntPtr.Zero) UnmapViewOfFile(traceView);
        if (traceMapping != IntPtr.Zero) CloseHandle(traceMapping);
        traceView = traceMapping = IntPtr.Zero;
    }
    [DllImport("user32.dll", SetLastError=true)]
    static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint message, UIntPtr wparam, IntPtr lparam, uint flags, uint timeout, out UIntPtr result);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);
    [DllImport("dbghelp.dll", SetLastError=true)]
    static extern bool MiniDumpWriteDump(IntPtr process, int pid, IntPtr file, uint type, IntPtr exception, IntPtr streams, IntPtr callback);
    public static bool Responds(IntPtr hwnd) {
        UIntPtr result;
        return SendMessageTimeout(hwnd, 0, UIntPtr.Zero, IntPtr.Zero, 2, 100, out result) != IntPtr.Zero;
    }
    public static void Dump(int pid, string path) {
        WriteDump(pid, path, 0x1060);
    }
    public static void DumpFull(int pid, string path) {
        // Full memory, handles, unloaded modules, address map and thread info.
        // Ignore inaccessible pages, which may include driver mappings.
        WriteDump(pid, path, 0x21826);
    }
    static void WriteDump(int pid, string path, uint flags) {
        IntPtr process = OpenProcess(0x450, false, pid);
        if (process == IntPtr.Zero) throw new Win32Exception();
        try {
            using (FileStream file = new FileStream(path, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None)) {
                if (!MiniDumpWriteDump(process, pid, file.SafeFileHandle.DangerousGetHandle(), flags, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero))
                    throw new Win32Exception();
            }
        } finally { CloseHandle(process); }
    }
}
'@
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $exe
$info.WorkingDirectory = Split-Path -Parent $exe
$info.UseShellExecute = $false
# Remove diagnostic layers only from the launched child's environment.
foreach ($key in @($info.EnvironmentVariables.Keys)) {
    if ([string]$key -like 'VK_LAYER*' -or [string]$key -like 'VK_LOADER_LAYERS*' -or [string]$key -eq 'VK_INSTANCE_LAYERS' -or [string]$key -like 'GFXRECON_*') {
        $info.EnvironmentVariables.Remove($key)
    }
}
$info.EnvironmentVariables['NXEMU_VK_TRACE'] = if ($NoTrace) { '0' } else { '1' }
$info.EnvironmentVariables['NXEMU_VK_RETAIN_CHUNKS'] = if ($RetainCommandHistory) { '1' } else { '0' }
$info.EnvironmentVariables['NXEMU_VK_CHECKPOINTS'] = if ($GpuCheckpoints) { '1' } else { '0' }
if ($ValidateOnly) { Write-Output "PASS : executable, helper, sans validation, trace Vulkan=$(-not $NoTrace)"; return }
if (Get-Process -Name nxemu -ErrorAction SilentlyContinue) { throw 'Ferme NXEmu normalement avant de lancer ce diagnostic.' }
$output = Join-Path $root ('build\vulkan-diagnostic\session-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$commit = 'unknown'
try { $commit = (& git -C $root rev-parse HEAD 2>$null | Select-Object -First 1) } catch {}
@{
    gitCommit = $commit
    executable = $exe
    sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    started = (Get-Date).ToString('o')
    validation = $false
    vulkanTrace = -not $NoTrace
    videoModuleSha256 = (Get-FileHash -LiteralPath (Join-Path $root 'modules\x64\video\nxemu-video.dll') -Algorithm SHA256).Hash
    pollMilliseconds = 1000
    unresponsiveSeconds = 10
    fullMemoryAfterFirstHang = -not $NoFullMemory
    retainedCpuCommandHistory = [bool]$RetainCommandHistory
    gpuCheckpointsRequested = [bool]$GpuCheckpoints
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'launch.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $root 'config\NxEmu.config') -Destination (Join-Path $output 'NxEmu.config')
. (Join-Path $PSScriptRoot 'Read-GpuLoss.ps1')
$log = Join-Path $root 'bin\x64\Release\user\log\nxemu_log.txt'
$logState = @{Offset=0L;Tail=''}
if (Test-Path -LiteralPath $log) { $logState.Offset = (Get-Item -LiteralPath $log).Length }
$deviceLossPending = $false
$process = [Diagnostics.Process]::Start($info)
Write-Host "NXEmu PID $($process.Id), sans validation Vulkan. Reproduis Unlocked."
if ($NoTrace) { Write-Host 'ESSAI TEMOIN : trace Vulkan desactivee, meme binaire, captures de threads actives.' }
if ($RetainCommandHistory) { Write-Host 'Diagnostic CPU : les 128 derniers chunks executes restent en memoire pour le dump. Aucune commande GPU supplementaire.' }
Write-Host "Garde cette console ouverte. Captures automatiques apres 10 secondes de blocage : $output"
Write-Host 'Tu peux arreter et relancer le jeu plusieurs fois dans NXEmu. Le diagnostic reste actif.'
$started = [Diagnostics.Stopwatch]::StartNew()
$unresponsiveSince = $null
$captured = $false
$captureIndex = 0
$traceAttached = $false
$traceWarning = $false
$fullMemoryAttempted = $false
try {
    while (-not $process.HasExited) {
        Start-Sleep -Milliseconds 1000
        $process.Refresh()
        if ($process.HasExited) { break }
        $deviceLossPending = $deviceLossPending -or (Read-NewGpuLoss -Path $log -State $logState)
        if (-not $NoTrace -and -not $traceAttached) {
            $traceAttached = [NxLightDiagnostic]::TryAttach($process.Id)
            if ($traceAttached) { Write-Host 'Historique Vulkan connecte et conserve entre les relances du jeu.' }
            elseif (-not $traceWarning -and $started.Elapsed.TotalSeconds -gt 20) {
                Write-Warning 'Historique Vulkan absent : verifier le module video instrumente. Les minidumps restent actifs.'
                $traceWarning = $true
            }
        }
        $window = $process.MainWindowHandle
        if (-not $deviceLossPending) {
            if ($started.Elapsed.TotalSeconds -lt 20 -or $window -eq [IntPtr]::Zero) { $unresponsiveSince = $null; continue }
            if ([NxLightDiagnostic]::Responds($window)) {
                $unresponsiveSince = $null
                if ($captured) { Write-Host 'La fenetre repond de nouveau. Diagnostic rearme pour le prochain blocage.' }
                $captured = $false
                continue
            }
            if ($null -eq $unresponsiveSince) { $unresponsiveSince = $started.Elapsed.TotalSeconds; continue }
            if ($captured -or $started.Elapsed.TotalSeconds - $unresponsiveSince -lt 10) { continue }
        }
        $reason = if ($deviceLossPending) { 'device-lost' } else { 'window-unresponsive' }
        $deviceLossPending = $false
        $captureIndex++
        $captureOutput = Join-Path $output ('hang-{0:d3}' -f $captureIndex)
        New-Item -ItemType Directory -Path $captureOutput | Out-Null
        @{reason=$reason;time=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $captureOutput 'trigger.json')
        Write-Host "Incident GPU ($reason) : capture des threads (une breve perturbation est possible pendant la capture)."
        for ($number = 1; $number -le 2; $number++) {
            if ($process.HasExited) { break }
            if (-not $NoTrace -and -not $traceAttached) { $traceAttached = [NxLightDiagnostic]::TryAttach($process.Id) }
            [NxLightDiagnostic]::SaveTrace((Join-Path $captureOutput "vulkan-trace-$number.bin")) | Out-Null
            try { [NxLightDiagnostic]::Dump($process.Id, (Join-Path $captureOutput "nxemu-hang-$number.dmp")) }
            catch { Write-Warning "Minidump $number : $_" }
            if ($number -eq 1) { Start-Sleep -Milliseconds 1000 }
        }
        $captured = $true
        (Get-Date).ToString('o') | Set-Content -LiteralPath (Join-Path $captureOutput 'capture-time.txt')
        $log = Join-Path $root 'bin\x64\Release\user\log\nxemu_log.txt'
        if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $captureOutput 'nxemu_log.txt') }
        Copy-Item -LiteralPath (Join-Path $root 'config\NxEmu.config') -Destination (Join-Path $captureOutput 'NxEmu.config')
        if (-not $NoFullMemory -and -not $fullMemoryAttempted -and -not $process.HasExited) {
            $fullMemoryAttempted = $true
            try {
                & (Join-Path $PSScriptRoot 'Capture-Now.ps1') -ProcessId $process.Id -OutputDirectory (Join-Path $captureOutput 'extended')
            } catch {
                Write-Warning "Capture memoire etendue : $_"
            }
        }
        Write-Host "Captures sauvegardees : $captureOutput. NXEmu reste ouvert."
    }
} finally {
    [NxLightDiagnostic]::SaveTrace((Join-Path $output 'vulkan-trace-final.bin')) | Out-Null
    [NxLightDiagnostic]::Detach()
    $log = Join-Path $root 'bin\x64\Release\user\log\nxemu_log.txt'
    if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $output 'nxemu_log-final.txt') -ErrorAction Continue }
}
Write-Host "Session terminee. Resultats : $output"
