param([string]$Order = 'A-master,B-pr2,A-master,B-pr2,A-master,B-pr2,A-master,B-pr2', [string]$Prefix = 'smo', [int]$Seconds = 150)
$game = 'D:\Emulation\switch\NXDev\NxRoom\decrypted\Super Mario Odyssey Super.dxci'
$i = 0
foreach ($v in ($Order -split ',')) {
    $i++
    & (Join-Path $PSScriptRoot 'Run.ps1') -Variant $v -Game $game -Label ("{0}-{1:00}" -f $Prefix, $i) -Seconds $Seconds -Unlock -Keys '38:67,48:67,58:67,68:67' -Shots '75,145'
    Start-Sleep -Seconds 5
}
'series done'
