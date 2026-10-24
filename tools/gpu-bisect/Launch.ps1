param([switch]$ValidateOnly)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$state = Get-Content -LiteralPath (Join-Path $root 'build\gpu-bisect\state.json') -Raw | ConvertFrom-Json
if ($state.buildStatus -ne 'ready') { throw 'Le build de cette etape n est pas encore pret.' }
$head = (& git -C $state.worktree rev-parse HEAD | Select-Object -First 1)
if ($head -ne $state.currentCommit) { throw 'Le checkout de bissection a change. Mettre a jour le manifeste avant de tester.' }
foreach ($binary in $state.binaries) {
    if ((Get-FileHash -LiteralPath $binary.path -Algorithm SHA256).Hash -ne $binary.sha256) {
        throw "Le binaire ne correspond plus a cette etape : $($binary.path)"
    }
}
Write-Host "Bissection : $($state.currentCommit.Substring(0,8)) - $($state.currentSubject)"
Write-Host 'Build Release sans trace Vulkan ni validation. Capture de threads en cas de gel.'
if ($state.gpuCheckpoints) { Write-Host 'Marqueurs GPU NVIDIA actifs : variante de diagnostic, cout a mesurer.' }
Write-Host 'Repasse en Unlocked a chaque relance du jeu. Garde cette console ouverte.'
if ($state.purpose -eq 'performance') {
    Write-Host 'Bissection FPS : rapporte tes mesures pour ce commit. Un gel GPU ne constitue pas un verdict de performance.'
} else {
    Write-Host 'Note le nombre de relances et le temps de jeu. Une absence de gel reste provisoire.'
}
& (Join-Path $state.worktree 'tools\vulkan-diagnostic\Launch.ps1') -NoTrace -ValidateOnly:$ValidateOnly -RetainCommandHistory:([bool]$state.retainCommandHistory) -GpuCheckpoints:([bool]$state.gpuCheckpoints)

