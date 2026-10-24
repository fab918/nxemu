param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$RenderDocDirectory,
    [Parameter(Mandatory=$true)][string]$CaptureDirectory,
    [string]$Game
)
$ErrorActionPreference = 'Stop'
$appPath = (Resolve-Path -LiteralPath $Executable).Path
$rdPath = (Resolve-Path -LiteralPath $RenderDocDirectory).Path
$rdCommand = Join-Path $rdPath 'renderdoccmd.exe'
if (!(Test-Path -LiteralPath $rdCommand) -or !(Test-Path -LiteralPath (Join-Path $rdPath 'renderdoc.json'))) {
    throw 'Use the official Windows x64 portable RenderDoc directory.'
}
New-Item -ItemType Directory -Path $CaptureDirectory -Force | Out-Null
$capturePath = (Resolve-Path -LiteralPath $CaptureDirectory).Path
# Layer enabled only for this child process, without registry installation.
$names = @('VK_LAYER_PATH','VK_INSTANCE_LAYERS','ENABLE_VULKAN_RENDERDOC_CAPTURE','NXEMU_RENDERDOC_CAPTURE_DIR','NXEMU_NATIVE_PRESENT_DIR')
$previous = @{}
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:VK_LAYER_PATH = $rdPath
    $env:VK_INSTANCE_LAYERS = 'VK_LAYER_RENDERDOC_Capture'
    $env:ENABLE_VULKAN_RENDERDOC_CAPTURE = '1'
    $env:NXEMU_RENDERDOC_CAPTURE_DIR = $capturePath
    Remove-Item Env:NXEMU_NATIVE_PRESENT_DIR -ErrorAction SilentlyContinue
    $arguments = @('capture','--working-dir',(Split-Path -Parent $appPath),'--capture-file',(Join-Path $capturePath 'nxemu'),$appPath)
    if ($Game) { $arguments += (Resolve-Path -LiteralPath $Game).Path }
    $launchOutput = & $rdCommand @arguments 2>&1
    $captureExit = $LASTEXITCODE
    $launchOutput | ForEach-Object { Write-Host $_ }
    # RenderDoc's capture command returns its target-control identifier on
    # success, rather than the conventional zero process exit code.
    $launched = [regex]::Match(($launchOutput -join "`n"), 'Launched as ID ([0-9]+)')
    if ($captureExit -ne 0 -and (!$launched.Success -or $captureExit -ne [int]$launched.Groups[1].Value)) {
        throw "RenderDoc launcher failed: $captureExit"
    }
    Write-Host "GPU capture requested. Check the RenderDoc overlay in the game, then press F8 when the defect is visible. Captures: $capturePath"
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
