param(
    [Parameter(Mandatory)][string[]]$Runs,
    [Parameter(Mandatory)][string]$LightingRun,
    [Parameter(Mandatory)][string]$NativeName,
    [Parameter(Mandatory)][string]$Regression,
    [string]$Root='D:\Games\idle_wow',
    [switch]$Reviewed
)
$ErrorActionPreference='Stop'
if(-not $Reviewed){throw 'Individually review all final PNGs before visual acceptance'}
$source=Join-Path $Root 'src\WowUnreal'; $commit=(& git -C $source rev-parse HEAD).Trim()
& "$Root\logs\wowunreal-ui-completion\Verify-WorldLightingInstall.ps1" -Name $LightingRun -Regression $Regression -Root $Root -Reviewed
$lighting=Get-Content "$Root\logs\wowunreal-lighting\$LightingRun\install-verification.json" -Raw|ConvertFrom-Json
$all=@(); $images=0
foreach($name in $Runs){
    $path=Join-Path $Root "logs\wowunreal-performance\$name"
    $run=Get-Content "$path\verification.json" -Raw|ConvertFrom-Json
    $runtime=Get-Content "$path\runtime.json" -Raw|ConvertFrom-Json
    $summary=Get-Content "$path\summary.json" -Raw|ConvertFrom-Json
    if(-not $runtime.passed -or $run.workingTreeModified -or $run.sha256 -ne $lighting.sha256 -or $run.networkLogin){throw "Invalid performance provenance: $name"}
    & git -C $source diff --quiet $run.commit HEAD -- Source Content Config Scripts BuildSupport WowUnreal.uproject
    if($LASTEXITCODE -ne 0){throw "Runtime inputs changed since $name"}
    if($summary.queueMaxima.preparedSlots -gt 4 -or -not $summary.oneHeavyUnitPerKindPerFrame){throw 'Streaming bounds were not verified'}
    if($summary.budgetedStreamingVerified -and ($summary.queueMaxima.streamingJobsPerFrame -gt 8 -or $summary.queueMaxima.terrainUploadsPerFrame -gt 1)){throw 'Streaming time/count budget was exceeded'}
    if($summary.allWmoGroupsPreparedOnWorkers){foreach($row in $runtime.steps){if($row.missingTerrainCoverage -ne 0){throw "Missing full/mid/far source coverage in $name"}}}
    foreach($png in @(Get-Content "$path\captures.json" -Raw|ConvertFrom-Json)){
        if($png.nonBlackRatio -lt .01 -or (Get-FileHash -LiteralPath $png.file).Hash -ne $png.sha256){throw 'Empty or changed PNG'}
        ++$images
    }
    $checkpoints=@($runtime.steps|Where-Object diagnosticCollection)
    if($checkpoints.Count){
        if($checkpoints.Count -lt 7){throw 'Incomplete six-cycle memory stress route'}
        $returns=@($checkpoints|Where-Object {$_.name -like 'stress-abbey-*'})
        foreach($row in $checkpoints){if($row.worldParsedM2MiB -gt 128 -or $row.characterParsedM2MiB -gt 64 -or $row.worldParsedM2Entries -gt 2048 -or $row.characterParsedM2Entries -gt 2048 -or $row.pendingTiles -or $row.pendingWmoGroups -or $row.pendingDoodadTiles -or $row.pendingLod1){throw 'Memory/cache/queue limit failed'}}
        $late=@($returns|Select-Object -Skip 1); $range=($late|Measure-Object privateMiB -Maximum).Maximum-($late|Measure-Object privateMiB -Minimum).Minimum
        if($range -gt 256){throw "Post-collection private memory did not plateau: range=$range MiB"}
        foreach($field in @('uobjects','uniqueStaticMeshes','doodadInstances','terrainSections','wmoShadowCasters')){
            if(@($returns|ForEach-Object {$_.$field}|Select-Object -Unique).Count -ne 1){throw "Retained scene population changed across memory returns: $field"}
        }
    }
    $all+=[ordered]@{name=$name;cacheMode=$run.cacheMode;systemFileCache=$run.systemFileCache;steps=$runtime.steps;queueMaxima=$summary.queueMaxima;distributions=$summary.allFrames;scopeDistributions=$summary.scopesMs}
}
$native=Get-Content "$Root\logs\wowunreal-performance\$NativeName-native.json" -Raw|ConvertFrom-Json
if($native.workingTreeModified -or $native.sha256 -ne $lighting.sha256 -or -not $native.passed -or -not $native.ownedFixturesClosed -or -not $native.protectedFingerprintsMatch -or -not $native.credentialsRemoved){throw 'Native proof/cleanup differs'}
& git -C $source diff --quiet $native.commit HEAD -- Source Content Config Scripts BuildSupport WowUnreal.uproject
if($LASTEXITCODE -ne 0){throw 'Native runtime inputs changed'}
& "$Root\scripts\test-wowunreal-install.ps1" -RegressionReport "$Root\logs\wowunreal-ui-completion\$Regression\index.json" -NativeRuns @($native.run) -ScreenshotReports @($native.captures) -ReportPath "$Root\logs\wowunreal-performance\$NativeName-install.json"
$report=[ordered]@{timestamp=[DateTimeOffset]::Now.ToString('o');commit=$commit;published=$true;clean=$true;localOverlays=$false;
    sourceFiles=$lighting.sourceFiles;runtimeInputFiles=$lighting.runtimeInputFiles;packageFiles=$lighting.packageFiles;sha256=$lighting.sha256;
    testsPassed=$lighting.testsPassed;testsWithWarnings=$lighting.testsWithWarnings;runningClients=0;allFinalPngsIndividuallyReviewed=$true;
    offlineScreenshotCount=$images;lightingScreenshotCount=$lighting.screenshotCount;native=$native;runs=$all}
$report|ConvertTo-Json -Depth 18|Set-Content "$Root\logs\wowunreal-performance\install-verification.json"
Write-Host "PASS published installation $commit : source/stage/package/runtime, rendered performance, lighting and owned native guards"
