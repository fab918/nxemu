$game = 'D:\Emulation\switch\NXDev\NxRoom\decrypted\The Legend of Zelda Tears of the Kingdom [0100F2C0115B6000][v0][US].dnsp'
Remove-Item Env:NXEMU_CPU_PLACEMENT -ErrorAction SilentlyContinue
$i = 0
foreach ($mode in 'narrow','all','narrow','all') {
    $i++
    if ($mode -eq 'all') { $env:NXEMU_READ_TRACKING_ALL = '1' } else { Remove-Item Env:NXEMU_READ_TRACKING_ALL -ErrorAction SilentlyContinue }
    & (Join-Path $PSScriptRoot 'Run.ps1') -Variant B-pr2 -Game $game -Label ("read-{0:00}-{1}" -f $i, $mode) -Seconds 160 -Unlock -Keys '34:67,44:67,54:67' -Shots '100,140'
    Start-Sleep -Seconds 5
}
Remove-Item Env:NXEMU_READ_TRACKING_ALL -ErrorAction SilentlyContinue
'read tracking series done'
