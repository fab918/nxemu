param([switch]$ValidateOnly, [ValidateSet('Initial','ConditionalFix')][string]$Variant = 'Initial')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$baseline = Join-Path $root 'build\gpu-incident-20261021-1619\light-20261021-164005-634\symbols'
$module = Join-Path $root 'modules\x64\video\nxemu-video.dll'
$pdb = Join-Path $root 'modules\x64\video\pdb\nxemu-video.pdb'
$expectedDll = 'DEC79611C299AD3414650F5B69DFD3AE043DA347DFE10CB0F0E938A0B166C269'
$expectedPdb = '1847B34E59BC1C37892435A94AE4F98B37BD7D9F2398D472580BA0863F0CC24D'
$expectedExe = '85F5F885D41BC1007DFDCA979881E382105349F922C81D209A18FCB05B1F45FA'
if ($Variant -eq 'ConditionalFix') {
    $baseline = Join-Path $root 'build\vulkan-diagnostic\conditional-scope-candidate'
    $manifest = Get-Content -LiteralPath (Join-Path $baseline 'manifest.json') -Raw | ConvertFrom-Json
    $expectedDll = $manifest.dllSha256
    $expectedPdb = $manifest.pdbSha256
    $expectedExe = $manifest.exeSha256
}
foreach ($check in @(
    @((Join-Path $baseline 'nxemu-video.dll'), $expectedDll),
    @((Join-Path $baseline 'nxemu-video.pdb'), $expectedPdb),
    @((Join-Path $root 'bin\x64\Release\nxemu.exe'), $expectedExe)
)) {
    if ((Get-FileHash -LiteralPath $check[0] -Algorithm SHA256).Hash -ne $check[1]) {
        throw "Le binaire ne correspond plus au temoin sauvegarde : $($check[0])"
    }
}
if ($ValidateOnly) { Write-Output "PASS : module et PDB verifies ($Variant), executable identique. Aucun fichier remplace."; return }
if (Get-Process -Name nxemu -ErrorAction SilentlyContinue) { throw 'Ferme NXEmu normalement avant de lancer le temoin initial.' }
$backup = Join-Path $root ('build\vulkan-diagnostic\baseline-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
Copy-Item -LiteralPath $module -Destination (Join-Path $backup 'nxemu-video.dll')
Copy-Item -LiteralPath $pdb -Destination (Join-Path $backup 'nxemu-video.pdb')
$beforeHash = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
try {
    Copy-Item -LiteralPath (Join-Path $baseline 'nxemu-video.dll') -Destination $module
    Copy-Item -LiteralPath (Join-Path $baseline 'nxemu-video.pdb') -Destination $pdb
    if ($Variant -eq 'Initial') { Write-Host 'TEMOIN INITIAL : module exact capture lors du blocage, sans instrumentation ni validation.' }
    else { Write-Host 'CANDIDAT NON RETENU : ne corrige pas le gel, baisse de performances rapportee.' }
    Write-Host 'Garde la console ouverte. Le module precedent sera restaure a la fermeture de NXEmu.'
    & (Join-Path $PSScriptRoot 'Launch.ps1') -NoTrace:($Variant -eq 'Initial')
} finally {
    if (Get-Process -Name nxemu -ErrorAction SilentlyContinue) {
        Write-Warning "NXEmu est encore ouvert. Module precedent conserve dans $backup ; restauration differee."
    } else {
        Copy-Item -LiteralPath (Join-Path $backup 'nxemu-video.dll') -Destination $module
        Copy-Item -LiteralPath (Join-Path $backup 'nxemu-video.pdb') -Destination $pdb
        if ((Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash -ne $beforeHash) {
            throw "Verifier la restauration depuis $backup"
        }
        Write-Host 'Module precedent restaure.'
    }
}
