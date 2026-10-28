param(
    [string]$TargetExe = '',
    [string]$LayerDirectory = '',
    [string]$OutputRoot = '',
    [string]$GamePath = '',
    [ValidateRange(1, 120)][int]$Frames = 2,
    [switch]$Continuous,
    [switch]$ValidateOnly,
    [switch]$Wait
)

$ErrorActionPreference = 'Stop'
if (-not $TargetExe) {
    $TargetExe = Join-Path $PSScriptRoot 'nxemu.exe'
    if (-not (Test-Path -LiteralPath $TargetExe -PathType Leaf)) {
        $TargetExe = Join-Path $PSScriptRoot '../../bin/x64/Release/nxemu.exe'
    }
}
$captureExe = (Resolve-Path -LiteralPath $TargetExe).Path
$captureDirectory = Split-Path -Parent $captureExe
if (-not $LayerDirectory) { $LayerDirectory = Join-Path $captureDirectory 'gpu-capture-layer' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $captureDirectory 'user/dump/gpu' }
$captureLayer = (Resolve-Path -LiteralPath $LayerDirectory).Path
foreach ($dependency in @('VkLayer_gfxreconstruct.dll', 'VkLayer_gfxreconstruct.json')) {
    if (-not (Test-Path -LiteralPath (Join-Path $captureLayer $dependency) -PathType Leaf)) {
        throw "Missing capture layer dependency: $dependency"
    }
}
if (-not $ValidateOnly) {
    foreach ($running in Get-Process -Name 'nxemu','nxemu-gpu-dump','nxemu-controller-fix' -ErrorAction SilentlyContinue) {
        if ($running.Path -and (Split-Path -Parent $running.Path) -eq $captureDirectory) {
            throw 'Ferme NXEmu normalement avant de lancer la capture GPU.'
        }
    }
}

$captureOutput = Join-Path $OutputRoot ((Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $captureOutput | Out-Null
$captureOutput = (Resolve-Path -LiteralPath $captureOutput).Path
$captureStart = [Diagnostics.ProcessStartInfo]::new()
$captureStart.FileName = $captureExe
$captureStart.WorkingDirectory = $captureDirectory
$captureStart.UseShellExecute = $false
if ($GamePath) {
    $captureGame = (Resolve-Path -LiteralPath $GamePath).Path
    if ($captureGame.Contains('"')) { throw 'Invalid game path' }
    $captureStart.Arguments = '"' + $captureGame + '"'
}
foreach ($key in @($captureStart.EnvironmentVariables.Keys)) {
    if ($key -like 'GFXRECON_*' -or $key -like 'NXEMU_GPU_DUMP_*' -or $key -like 'VK_LAYER*' -or $key -like 'VK_LOADER_LAYERS*' -or $key -eq 'VK_INSTANCE_LAYERS') {
        $captureStart.EnvironmentVariables.Remove($key)
    }
}
$captureEnv = @{
    VK_LAYER_PATH = $captureLayer
    VK_INSTANCE_LAYERS = 'VK_LAYER_LUNARG_gfxreconstruct'
    GFXRECON_CAPTURE_FILE = (Join-Path $captureOutput 'gpu.gfxr')
    GFXRECON_CAPTURE_FILE_TIMESTAMP = 'true'
    GFXRECON_CAPTURE_COMPRESSION_TYPE = 'LZ4'
    GFXRECON_CAPTURE_TRIGGER = 'F8'
    GFXRECON_CAPTURE_TRIGGER_BOUNDARY = 'frames'
    GFXRECON_MEMORY_TRACKING_MODE = 'page_guard'
    GFXRECON_LOG_FILE = (Join-Path $captureOutput 'capture-layer.log')
    GFXRECON_LOG_LEVEL = 'info'
}
if (-not $Continuous) { $captureEnv['GFXRECON_CAPTURE_TRIGGER_FRAMES'] = "$Frames" }
foreach ($key in $captureEnv.Keys) { $captureStart.EnvironmentVariables[$key] = $captureEnv[$key] }
$captureHashes = @{}
foreach ($captureFile in @(Get-ChildItem -LiteralPath (Join-Path $captureDirectory 'modules') -Filter '*.dll' -Recurse -ErrorAction SilentlyContinue)) {
    $captureHashes[$captureFile.FullName] = (Get-FileHash -LiteralPath $captureFile.FullName -Algorithm SHA256).Hash
}
@{
    started_at = (Get-Date).ToString('o')
    executable = $captureExe
    executable_sha256 = (Get-FileHash -LiteralPath $captureExe -Algorithm SHA256).Hash
    layer_sha256 = (Get-FileHash -LiteralPath (Join-Path $captureLayer 'VkLayer_gfxreconstruct.dll') -Algorithm SHA256).Hash
    game = if ($GamePath) { $captureGame } else { '' }
    module_sha256 = $captureHashes
    frames = if ($Continuous) { $null } else { $Frames }
    continuous = [bool]$Continuous
    environment = $captureEnv
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $captureOutput 'launch.json') -Encoding utf8
$captureConfig = Join-Path $captureDirectory 'user/config/NxEmu.config'
if (Test-Path -LiteralPath $captureConfig -PathType Leaf) {
    Copy-Item -LiteralPath $captureConfig -Destination (Join-Path $captureOutput 'settings-at-launch.config')
}
Write-Output "Dossier du dump GPU : $captureOutput"
if ($ValidateOnly) {
    Write-Output 'PASS : executable, couche de capture et configuration du lanceur.'
    return
}

$captureProcess = [Diagnostics.Process]::Start($captureStart)
if ($Continuous) {
    Write-Output 'En jeu Vulkan : F8 pour commencer la capture, puis F8 pour la terminer.'
} else {
    Write-Output "En jeu Vulkan : appuie sur F8 au moment du probleme pour capturer $Frames images (arret automatique)."
}
Write-Output 'Le jeu peut se figer pendant la sauvegarde de l''etat GPU.'
if ($Wait) {
    $captureProcess.WaitForExit()
    if ($captureProcess.ExitCode -ne 0) { throw "Capture process exited with code $($captureProcess.ExitCode)" }
}
