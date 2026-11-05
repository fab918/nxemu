$game = 'D:\Emulation\switch\NXDev\NxRoom\decrypted\The Legend of Zelda Tears of the Kingdom [0100F2C0115B6000][v0][US].dnsp'
$i = 0
foreach ($mode in 'off','full','off','full') {
    $i++
    $env:NXEMU_CPU_PLACEMENT = $mode
    & (Join-Path $PSScriptRoot 'Run.ps1') -Variant B-pr2 -Game $game -Label ("place2-{0:00}-{1}" -f $i, $mode) -Seconds 160 -Unlock -Keys '34:67,44:67,54:67' -Shots '120'
    Start-Sleep -Seconds 5
}
Remove-Item Env:NXEMU_CPU_PLACEMENT
'placement series done'

