param([Parameter(Mandatory)][string]$RunPath,[string]$Baseline)
$ErrorActionPreference='Stop'
$r=Get-Content (Join-Path $RunPath 'runtime.json') -Raw|ConvertFrom-Json
if(-not $r.passed -or $r.networkLogin){throw 'Invalid offline run'}
$clocks=@('day-clock','sunset-clock','day-realtime','night-realtime');$results=@()
foreach($c in $r.cases){
    if($c.directionalLights -ne 2 -or $c.wmoShadowCasters -ne 590){throw "Changed light ownership or disabled WMO casters in $($c.name)"}
    if($clocks -contains $c.name){
        $p=$c.profile.sunStepDegrees
        if($p.samples -lt 50 -or $p.mean -le 0){throw "Missing clock motion: $($c.name)"}
        if($c.name -in @('day-clock','sunset-clock') -and $p.zeroSamples/$p.samples -gt .05){throw "Shadow direction still holds between clock updates: $($c.name)"}
        # SceneComponent suppresses sub-.0001-degree rotations. At realtime
        # speed verify their measured amplitude rather than requiring no holds.
        if($c.name -like '*-realtime' -and ($p.p95 -gt .001 -or $p.max -gt .003)){throw "Realtime shadow step too large: $($c.name)"}
        $results+=[ordered]@{name=$c.name;heldFraction=$p.zeroSamples/$p.samples;p95Degrees=$p.p95;maxDegrees=$p.max;gameMedianMs=$c.profile.gameThreadMs.median;gpuMedianMs=$c.profile.gpuMs.median}
    }
}
if($results.Count -ne 4){throw 'Missing moving-clock coverage'}
$before=@()
if($Baseline){
    $b=Get-Content (Join-Path $Baseline 'runtime.json') -Raw|ConvertFrom-Json
    foreach($c in $b.cases|Where-Object{$_.name -in @('day-clock','sunset-clock')}){
        $p=$c.profile.sunStepDegrees;$fraction=$p.zeroSamples/$p.samples
        if($fraction -lt .8){throw 'Baseline did not reproduce quantized rotation'}
        $before+=[ordered]@{name=$c.name;heldFraction=$fraction;maxDegrees=$p.max}
    }
}
[ordered]@{passed=$true;directionalLights=2;wmoShadowCasters=590;clocks=$results;baseline=$before}|
    ConvertTo-Json -Depth 7|Set-Content (Join-Path $RunPath 'shadow-acceptance.json')
Write-Host 'PASS continuous day/night shadow clocks with all WMO shadow casters retained'
