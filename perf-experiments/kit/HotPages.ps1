$game = 'D:\Emulation\switch\NXDev\NxRoom\decrypted\The Legend of Zelda Tears of the Kingdom [0100F2C0115B6000][v0][US].dnsp'
Remove-Item Env:NXEMU_CPU_PLACEMENT,Env:NXEMU_READ_TRACKING_ALL -ErrorAction SilentlyContinue
Copy-Item D:\Emulation\switch\NXDev\nxemu-pr2-gpu-cache\modules\x64\video\Release\nxemu-video.dll (Join-Path $PSScriptRoot 'B-pr2\modules\video\') -Force
$i = 0
foreach ($mode in 'hot','off','hot','off') {
    $i++
    $label = "hot-{0:00}-{1}" -f $i, $mode
    if ($mode -eq 'off') { $env:NXEMU_HOT_PAGES = '0' } else { Remove-Item Env:NXEMU_HOT_PAGES -ErrorAction SilentlyContinue }
    $env:NXEMU_G2_STATS = Join-Path $PSScriptRoot "runs\$label-stats.txt"
    & (Join-Path $PSScriptRoot 'Run.ps1') -Variant B-pr2 -Game $game -Label $label -Seconds 160 -Unlock -Keys '34:67,44:67,54:67' -Shots '100,140'
    Start-Sleep -Seconds 5
}
Remove-Item Env:NXEMU_HOT_PAGES,Env:NXEMU_G2_STATS -ErrorAction SilentlyContinue
'hot pages series done'
