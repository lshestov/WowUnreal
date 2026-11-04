param([Parameter(Mandatory)][string]$RunPath)
$ErrorActionPreference='Stop'
$report=Get-Content -LiteralPath (Join-Path $RunPath 'runtime.json') -Raw | ConvertFrom-Json
if(-not $report.passed){throw 'World comparison did not pass'}
$names=@('graphics-minimum','graphics-density-maximum','graphics-maximum','graphics-density-minimum')
$rows=@(foreach($name in $names){
    $found=@($report.steps | Where-Object name -eq $name)
    if($found.Count -ne 1 -or -not $found[0].graphics){throw "Missing unique graphics snapshot: $name"}
    $found[0]
})
$baseline=$rows[0].graphics
$invariant=@('residentDoodadInstances','doodadBatches','loadedTiles','collisionBodies','collisionTriangles','wmoGroups','lightSources','activeLights','pendingLights','particleEmitters')
for($i=0;$i -lt $rows.Count;$i++){
    $g=$rows[$i].graphics
    if(-not $g.hasFrustum -or -not $g.streamingIdle -or $g.missingTerrainCoverage -ne 0){throw "Unsettled or unprojected scene: $($rows[$i].name)"}
    if($g.residentDoodadInstances -le 0 -or $g.liveParticles -le 0 -or $g.frustumEligibleDoodadInstances -le 0 -or $g.collisionBodies -le 0){throw 'Comparison scene lacks rendered objects, particles or collision'}
    foreach($field in $invariant){if($g.$field -ne $baseline.$field){throw "Changed scene invariant $field : $($rows[$i].name)"}}
    foreach($field in @('cameraUE','cameraRotation')){
        for($axis=0;$axis -lt 3;$axis++){if([Math]::Abs($g.$field[$axis]-$baseline.$field[$axis]) -gt .0001){throw "Camera changed: $field"}}
    }
}
$g0=$rows[0].graphics; $g1=$rows[1].graphics; $g2=$rows[2].graphics; $g3=$rows[3].graphics
if([Math]::Abs($g0.particleDensity-.1) -gt .00001 -or $g0.environmentDetail -ne .5 -or $g1.particleDensity -ne 1 -or $g1.environmentDetail -ne .5 -or $g2.particleDensity -ne 1 -or $g2.environmentDetail -ne 1.5 -or [Math]::Abs($g3.particleDensity-.1) -gt .00001 -or $g3.environmentDetail -ne 1.5){throw 'Wrong comparison endpoints'}
if($g1.liveParticles -le $g0.liveParticles -or $g2.liveParticles -le $g3.liveParticles){throw 'Density did not change actual live particle counts'}
if($g2.distanceEligibleDoodadInstances -le $g1.distanceEligibleDoodadInstances -or $g2.frustumEligibleDoodadInstances -le $g1.frustumEligibleDoodadInstances){throw 'Object detail did not increase distance/frustum eligible objects'}
if($g0.distanceEligibleDoodadInstances -ne $g1.distanceEligibleDoodadInstances -or $g2.distanceEligibleDoodadInstances -ne $g3.distanceEligibleDoodadInstances){throw 'Particle density changed object culling'}
Add-Type -AssemblyName System.Drawing
$width=0; $height=0
$captures=@(foreach($name in $names){
    $path=Join-Path $RunPath "$name.png"
    $bitmap=[Drawing.Bitmap]::new($path)
    try {
        if(-not $width){$width=$bitmap.Width; $height=$bitmap.Height}
        if($bitmap.Width -ne $width -or $bitmap.Height -ne $height){throw 'Capture dimensions changed'}
        $nonBlack=0; $samples=0
        for($y=0;$y -lt $bitmap.Height;$y+=4){for($x=0;$x -lt $bitmap.Width;$x+=4){
            $pixel=$bitmap.GetPixel($x,$y); ++$samples
            if([Math]::Max($pixel.R,[Math]::Max($pixel.G,$pixel.B)) -gt 10){++$nonBlack}
        }}
        $ratio=$nonBlack/[double]$samples
        if($ratio -lt .01){throw "Black/empty engine screenshot: $name"}
        [ordered]@{name=$name;width=$width;height=$height;nonBlackRatio=$ratio;sampleStride=4;sha256=(Get-FileHash -LiteralPath $path).Hash}
    } finally {$bitmap.Dispose()}
})
[ordered]@{passed=$true;visibilityCountMethod=$baseline.visibilityCountMethod;captures=$captures;steps=$rows} |
    ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $RunPath 'graphics-comparison.json') -Encoding utf8
$table=@(foreach($row in $rows){
    $g=$row.graphics
    "<tr><td>$($row.name)</td><td>$($g.particleDensity)</td><td>$($g.environmentDetail)</td><td>$($g.liveParticles)</td><td>$($g.frustumEligibleDoodadInstances)</td><td>$($g.distanceEligibleDoodadInstances)</td><td>$($g.residentDoodadInstances)</td></tr>"
}) -join "`n"
$gallery=@(foreach($name in $names){"<figure><img src='$name.png' alt='$name'><figcaption>$name</figcaption></figure>"}) -join "`n"
$html=@"
<!doctype html><html lang='ru'><meta charset='utf-8'><title>Сравнение настроек графики</title>
<style>body{font:16px system-ui;margin:24px;background:#131820;color:#e7edf5}table{border-collapse:collapse}td,th{border:1px solid #526071;padding:8px}main{display:grid;grid-template-columns:1fr 1fr;gap:16px}figure{margin:0}img{width:100%}figcaption{padding:8px}</style>
<h1>Одна сцена, одинаковая камера</h1><p>Число объектов в кадре — экземпляры M2 в пределах нативной дальности и пирамиды камеры до GPU-окклюзии. Оно не означает число объектов с видимыми пикселями. Частицы посчитаны в живой симуляции; случайная эмиссия и кадровая история не заморожены.</p>
<table><tr><th>Снимок</th><th>Плотность</th><th>Детализация</th><th>Живые частицы</th><th>Объекты в кадре до окклюзии</th><th>Объекты в пределах дальности</th><th>Загруженные объекты</th></tr>$table</table>
<p>Камера, загруженные экземпляры, геометрия коллизий, WMO и счётчики света совпали. Все четыре снимка прошли проверку на пустое изображение; визуальный осмотр выполняется отдельно.</p><main>$gallery</main></html>
"@
$html | Set-Content -LiteralPath (Join-Path $RunPath 'graphics-comparison.html') -Encoding utf8
Write-Host "PASS: fixed camera, independent density/detail, unchanged streaming/collision/light counters; inspect graphics-comparison.html"
