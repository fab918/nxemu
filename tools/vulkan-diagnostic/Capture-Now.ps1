[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [switch]$NoFullMemory
)
$ErrorActionPreference = 'Stop'
if (-not ('NxLightDiagnostic' -as [type])) {
    . (Join-Path $PSScriptRoot 'Launch.ps1') -ValidateOnly -NoTrace -NoFullMemory:$NoFullMemory
}
$targetProcess = Get-Process -Id $ProcessId -ErrorAction Stop
if ($targetProcess.ProcessName -ne 'nxemu') { throw 'Le processus cible doit etre NXEmu.' }
$exePath = $targetProcess.Path
$runtimeRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $exePath) '../../..'))
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$captureDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
if (Get-ChildItem -LiteralPath $captureDirectory -Filter '*.dmp') { throw 'Le dossier contient deja un dump.' }
$targetProcess.Refresh()
$snapshot = [ordered]@{
    processId = $ProcessId
    executable = $exePath
    processStart = $targetProcess.StartTime.ToString('o')
    captureStart = (Get-Date).ToString('o')
    privateBytes = $targetProcess.PrivateMemorySize64
    workingSetBytes = $targetProcess.WorkingSet64
    virtualBytes = $targetProcess.VirtualMemorySize64
    cpuSeconds = $targetProcess.TotalProcessorTime.TotalSeconds
    threadCount = $targetProcess.Threads.Count
    rendererTraceAdded = $false
    memoryCapture = 'not attempted'
}
$snapshot | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $captureDirectory 'capture.json') -Encoding utf8

# Capture actual loaded binaries, not a guessed module directory.
$moduleInfo = @($targetProcess.Modules | ForEach-Object {
    $modulePath = $_.FileName
    $moduleHash = $null
    if ($_.ModuleName -match '^nxemu.*\.(exe|dll)$') {
        $moduleHash = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
        $pdbDirectory = Join-Path (Split-Path -Parent $modulePath) 'pdb'
        $pdbName = [IO.Path]::GetFileNameWithoutExtension($modulePath) + '.pdb'
        $pdbPath = Join-Path $pdbDirectory $pdbName
        if (Test-Path -LiteralPath $pdbPath) {
            $symbolDirectory = Join-Path $captureDirectory 'symbols'
            New-Item -ItemType Directory -Path $symbolDirectory -Force | Out-Null
            Copy-Item -LiteralPath $pdbPath -Destination $symbolDirectory
        }
    }
    [pscustomobject]@{name=$_.ModuleName;path=$modulePath;baseAddress=('0x{0:x}' -f $_.BaseAddress.ToInt64());size=$_.ModuleMemorySize;sha256=$moduleHash}
})
$moduleInfo | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $captureDirectory 'loaded-modules.json') -Encoding utf8
foreach ($relativePath in @('config/NxEmu.config','bin/x64/Release/user/log/nxemu_log.txt')) {
    $sourcePath = Join-Path $runtimeRoot $relativePath
    if (Test-Path -LiteralPath $sourcePath) { Copy-Item -LiteralPath $sourcePath -Destination $captureDirectory }
}
try {
    @(Get-WinEvent -FilterHashtable @{LogName='System';StartTime=(Get-Date).AddMinutes(-20)} -MaxEvents 400 -ErrorAction Stop |
        Where-Object { $_.ProviderName -match 'nvlddmkm|^Display$|WHEA' } |
        Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message) |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $captureDirectory 'gpu-system-events.json') -Encoding utf8
} catch {
    $_.ToString() | Set-Content -LiteralPath (Join-Path $captureDirectory 'event-query-status.txt')
}

if (-not $NoFullMemory) {
    $driveRoot = [IO.Path]::GetPathRoot($captureDirectory)
    $freeBytes = ([IO.DriveInfo]::new($driveRoot)).AvailableFreeSpace
    # Leave room for the existing evidence and refuse a clearly full volume.
    $minimumFree = [math]::Max(16GB, 2 * $targetProcess.PrivateMemorySize64 + 4GB)
    if ($freeBytes -lt $minimumFree) {
        $snapshot.memoryCapture = "skipped: $freeBytes bytes free, $minimumFree required"
    } else {
        Write-Host 'Capture memoire complete du processus bloque (uniquement maintenant, aucun cout par frame).'
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $fullPath = Join-Path $captureDirectory 'nxemu-full-memory.dmp'
        try {
            [NxLightDiagnostic]::DumpFull($ProcessId, $fullPath)
            $snapshot.memoryCapture = 'complete'
            $snapshot.fullDumpBytes = (Get-Item -LiteralPath $fullPath).Length
        } catch {
            $snapshot.memoryCapture = "failed: $_"
        }
        $snapshot.captureSeconds = $timer.Elapsed.TotalSeconds
    }
}
# A second view after the full dump distinguishes a persistent wait from progress.
for ($number = 1; $number -le 2; $number++) {
    try { [NxLightDiagnostic]::Dump($ProcessId, (Join-Path $captureDirectory "nxemu-state-$number.dmp")) }
    catch { Write-Warning "Minidump $number : $_" }
    if ($number -eq 1) { Start-Sleep -Milliseconds 1000 }
}
$snapshot.captureEnd = (Get-Date).ToString('o')
$snapshot | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $captureDirectory 'capture.json') -Encoding utf8
Write-Output "Capture etendue sauvegardee : $captureDirectory"
