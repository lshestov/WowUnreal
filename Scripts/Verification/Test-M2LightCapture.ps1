param([Parameter(Mandatory)][string]$RunPath)
$ErrorActionPreference='Stop'
$r=Get-Content (Join-Path $RunPath 'runtime.json') -Raw|ConvertFrom-Json
if(-not $r.passed -or $r.networkLogin){throw 'Invalid offline run'}
$rows=@{}; foreach($c in $r.cases){$rows[$c.name]=$c;if($c.m2Active -gt 32 -or $c.m2MaxLoadMs -gt 1000){throw 'Light budget or load-time limit exceeded'}}
if($rows['camp-night-off'].m2Active -ne 0){throw 'Off case still has active lights'}
$on=$rows['camp-night-on']; $unloaded=$rows['camp-unloaded']; $reload=$rows['camp-reloaded']
if($on.m2Active -ne $unloaded.m2Active+1 -or $on.m2Sources -ne $unloaded.m2Sources+1 -or $reload.m2Active -ne $on.m2Active -or $reload.m2Sources -ne $on.m2Sources){throw 'Streamed fixture light leaked or failed to reload'}
$camp=@($on.authoredLights|Where-Object{[Math]::Abs($_.radiusYards-1.6666667) -lt .001})
$scaled=@($rows['camp-scaled-rotated'].authoredLights|Where-Object{[Math]::Abs($_.radiusYards-2.5) -lt .001})
if($camp.Count -ne 1 -or $scaled.Count -ne 1 -or -not $camp[0].falloffMaterial){throw 'Authored radius/scale/falloff missing'}
Add-Type -AssemblyName System.Drawing
$a=[Drawing.Bitmap]::FromFile((Join-Path $RunPath 'camp-night-off-00.png'))
$b=[Drawing.Bitmap]::FromFile((Join-Path $RunPath 'camp-night-on-00.png'))
try {
    $count=0;$changed=0;$red=0
    # Fixed ground/body ROI: catches a valid component whose shader produces no light.
    for($y=450;$y -lt 720;$y+=2){for($x=450;$x -lt 900;$x+=2){
        $p=$a.GetPixel($x,$y);$q=$b.GetPixel($x,$y)
        $difference=[Math]::Abs($q.R-$p.R)+[Math]::Abs($q.G-$p.G)+[Math]::Abs($q.B-$p.B)
        if($difference -gt 15){$changed++};$red+=($q.R-$p.R);$count++
    }}
    if($changed -lt 500 -or $red/$count -lt 1){throw 'Authored light has no measurable warm contribution'}
    [ordered]@{passed=$true;samples=$count;changedPixels=$changed;meanRedIncrease=$red/$count;activeOn=$on.m2Active;activeUnloaded=$unloaded.m2Active;activeReloaded=$reload.m2Active}|
        ConvertTo-Json|Set-Content (Join-Path $RunPath 'm2-acceptance.json')
} finally {$a.Dispose();$b.Dispose()}
Write-Host 'PASS authored M2 light, scaled radius, unload/reload and rendered contribution'
