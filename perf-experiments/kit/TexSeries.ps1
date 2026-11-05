$k = $PSScriptRoot
$game = 'D:\Emulation\switch\NXDev\NxRoom\decrypted\The Legend of Zelda Tears of the Kingdom [0100F2C0115B6000][v0][US].dnsp'
$cases = @(
    @{ label = 'tb-off'; tex = '0'; disp = '32' },
    @{ label = 'tb-tex'; tex = '1'; disp = '32' },
    @{ label = 'tb-d128'; tex = '1'; disp = '128' }
)
foreach ($c in $cases) {
    if (Get-Process nxemu -ErrorAction SilentlyContinue) { "$($c.label) skipped: another nxemu runs"; continue }
    $env:NXEMU_DRAW_TEX_CACHE = $c.tex
    $env:NXEMU_DISPATCH_DRAWS = $c.disp
    & "$k\Run.ps1" -Variant E-g2 -Game $game -Label $c.label -Seconds 150 -Unlock -Keys '34:67,44:67,54:67' -Shots '140'
}
Remove-Item Env:NXEMU_DRAW_TEX_CACHE, Env:NXEMU_DISPATCH_DRAWS -ErrorAction SilentlyContinue
python "$k\analyze.py" tb
