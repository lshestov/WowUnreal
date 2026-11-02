param(
    [Parameter(Mandatory)][string]$OfflineRun,
    [Parameter(Mandatory)][string[]]$NativeRuns,
    [Parameter(Mandatory)][string]$Regression,
    [Parameter(Mandatory)][string]$WorldNative,
    [string]$Root='D:\Games\idle_wow',[switch]$Reviewed
)
$ErrorActionPreference='Stop'
if(-not $Reviewed){throw 'Individually inspect every final PNG before accepting this build'}
$source=Join-Path $Root 'src/WowUnreal';$stage=Join-Path $Root 'build/wowunreal-x64/project'
$provenance=& "$PSScriptRoot/Get-RuntimeProvenance.ps1" -Source $source
if($provenance.runtimeInputsModified){throw 'Unpublished runtime/source inputs remain'}
$commit=$provenance.commit
$remote=& git -C $source ls-remote origin refs/heads/main
if($LASTEXITCODE -ne 0 -or $remote -notmatch ('^'+[regex]::Escape($commit)+'\s')){throw 'Published main differs'}
$build=Get-Content "$Root/logs/wowunreal-build-report.json" -Raw|ConvertFrom-Json
if($build.Commit -ne $commit -or $build.RuntimeInputsModified -ne $false -or $build.LocalPatchesApplied -or -not $build.RuntimeInstalled){throw 'Build is not the installed published runtime/source revision'}
$inputs=@(& git -C $source ls-files Source Content Config Scripts BuildSupport WowUnreal.uproject)
foreach($file in $inputs){if((Get-FileHash -LiteralPath (Join-Path $source $file)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $stage $file)).Hash){throw "Staged input mismatch: $file"}}
$sourceFiles=@(& git -C $source ls-files Source)
$known=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($file in $sourceFiles){[void]$known.Add($file)}
$extras=@(Get-ChildItem -LiteralPath "$stage/Source" -File -Recurse|Where-Object {$_.Extension -in @('.cpp','.h','.hpp','.cs') -and -not $known.Contains([IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/'))})
if($extras.Count){throw 'Unexpected staged source files'}
$exePaths=@('build/wowunreal-x64/project/Binaries/Win64/WowUnreal.exe','build/wowunreal-x64/package/Windows/WowUnreal/Binaries/Win64/WowUnreal.exe','runtime/client-wowunreal/WowUnreal/Binaries/Win64/WowUnreal.exe')
$hashes=@($exePaths|ForEach-Object {(Get-FileHash -LiteralPath (Join-Path $Root $_)).Hash})
if(@($hashes|Select-Object -Unique).Count -ne 1){throw 'Built/package/installed executables differ'}
$package=Join-Path $Root 'build/wowunreal-x64/package/Windows';$runtime=Join-Path $Root 'runtime/client-wowunreal'
$packageFiles=@(Get-ChildItem -LiteralPath $package -File -Recurse)
foreach($file in $packageFiles){$relative=[IO.Path]::GetRelativePath($package,$file.FullName);if((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $runtime $relative)).Hash){throw "Installed package mismatch: $relative"}}
$reg=Get-Content "$Root/logs/wowunreal-ui-completion/$Regression/index.json" -Raw|ConvertFrom-Json
if($reg.failed -or $reg.notRun -or $reg.succeededWithWarnings -or $reg.succeeded -lt 193){throw 'Regression failed, incomplete or has warnings'}
$offline=Join-Path $Root "logs/wowunreal-character-creation/$OfflineRun"
$capture=Get-Content "$offline/verification.json" -Raw|ConvertFrom-Json
$render=Get-Content "$offline/runtime.json" -Raw|ConvertFrom-Json
if($capture.runtimeInputsModified -ne $false -or $capture.sha256 -ne $hashes[0] -or -not $render.passed -or $render.screenCycles -ne 12 -or $render.cases.Count -lt 35){throw 'Incomplete/patched rendered evidence'}
& git -C $source diff --quiet $capture.commit HEAD -- Source Content Config Scripts BuildSupport WowUnreal.uproject
if($LASTEXITCODE -ne 0){throw 'Runtime inputs changed since the rendered run'}
foreach($race in @(1,2,3,4,5,6,7,8,10,11)){foreach($gender in @(0,1)){if(-not @($render.cases|Where-Object {$_.race -eq $race -and $_.gender -eq $gender -and $_.animated -and $_.liveModels -eq 1}).Count){throw 'Missing animated race/sex coverage'}}}
foreach($option in 0..4){if(-not @($render.cases|Where-Object {$_.stockAppearanceOption -eq $option}).Count){throw 'Missing original appearance-arrow coverage'}}
if(-not @($render.cases|Where-Object {$_.stockIdentityControlsInvoked}).Count){throw 'Missing original identity-button coverage'}
$reports=@("$offline/captures.json");$native=@()
foreach($name in $NativeRuns){
    $path=Join-Path $Root "logs/wowunreal-character-creation/$name"
    $proof=Get-Content "$path/native-verification.json" -Raw|ConvertFrom-Json
    $cleanup=Get-Content "$path/cleanup.json" -Raw|ConvertFrom-Json
    if(-not $proof.passed -or -not $proof.databaseAppearanceMatches -or -not $proof.enumMatchesPreview -or -not $proof.stockSlateControlsInvoked -or $proof.sha256 -ne $hashes[0] -or $proof.requestsSent -ne 4 -or $proof.shortNameResult -ne 0x5a -or $proof.deathKnightResult -ne 0x3b -or $proof.duplicateNameResult -ne 0x32){throw 'Native UI/server/appearance proof differs'}
    if(-not $cleanup.closed -or -not $cleanup.restored -or -not $cleanup.protectedFingerprintsMatch -or -not $cleanup.credentialsRemoved -or $cleanup.sha256 -ne $hashes[0]){throw 'Owned native cleanup failed'}
    if($cleanup.baselineApproval){
        $approval=Get-Content -LiteralPath $cleanup.baselineApproval.path -Raw|ConvertFrom-Json
        if(-not $approval.approved -or -not $approval.userConfirmation -or $approval.approvedHash -ne $cleanup.protectedHash -or (Get-FileHash -LiteralPath $cleanup.baselineApproval.path).Hash -ne $cleanup.baselineApproval.sha256){throw 'Explicit protected-state approval differs'}
    }
    & "$Root/logs/wowunreal-ui-completion/Test-Captures.ps1" -RunPath $path -ReportPath "$path/captures.json"
    $reports+="$path/captures.json";$native+=$path
}
if($NativeRuns.Count -lt 3){throw 'At least three owned race/class/sex native routes are required'}
$world=Get-Content "$Root/logs/wowunreal-performance/$WorldNative-native.json" -Raw|ConvertFrom-Json
if($world.commit -ne $commit -or $world.sha256 -ne $hashes[0] -or -not $world.passed -or -not $world.ownedFixturesClosed -or -not $world.protectedFingerprintsMatch -or -not $world.credentialsRemoved){throw 'World performance/native regression proof differs'}
$native+=$world.run;$reports+=$world.captures
$images=0;foreach($file in $reports){foreach($png in @(Get-Content $file -Raw|ConvertFrom-Json)){if($png.nonBlackRatio -lt .01 -or (Get-FileHash -LiteralPath $png.file).Hash -ne $png.sha256){throw 'Empty or changed capture'};++$images}}
$fixture=Get-Content "$Root/runtime/private/wowunreal-creation-native-fixture.json" -Raw|ConvertFrom-Json
if(-not $fixture.closed -or -not $fixture.restored -or $fixture.password -or $fixture.protectedHash -ne $fixture.protectedAfter.hash -or (Test-Path -LiteralPath "$($fixture.userDir)/Saved/WowCredentials.json")){throw 'Creation fixture remains open'}
$fullReport="$Root/logs/wowunreal-character-creation/$OfflineRun-install-native.json"
& "$Root/scripts/test-wowunreal-install.ps1" -RegressionReport "$Root/logs/wowunreal-ui-completion/$Regression/index.json" -NativeRuns $native -ScreenshotReports $reports -ReportPath $fullReport
$report=[ordered]@{timestamp=[DateTimeOffset]::Now.ToString('o');commit=$commit;published=$true;runtimeInputsModified=$false;
    workingTreeModified=$provenance.workingTreeModified;localDocumentation=$provenance.localDocumentation;localPatchesApplied=$false;
    sourceFiles=$sourceFiles.Count;runtimeInputFiles=$inputs.Count;packageFiles=$packageFiles.Count;sha256=$hashes[0];testsPassed=$reg.succeeded;testsWithWarnings=0;
    offlineCases=$render.cases.Count;screenCycles=$render.screenCycles;screenshotCount=$images;allFinalPngsIndividuallyReviewed=$true;
    nativeRuns=$NativeRuns;worldNative=$WorldNative;worldReportWorkingTreeModified=$world.workingTreeModified;runningClients=0;fixturesClosed=$true;nativeVerification=$fullReport}
$report|ConvertTo-Json -Depth 12|Set-Content "$Root/logs/wowunreal-character-creation/install-verification.json"
$build.Status='runtime-verified';$build|Add-Member -Force -NotePropertyName RuntimeVerified -NotePropertyValue $true
$build|Add-Member -Force -NotePropertyName VerificationReport -NotePropertyValue "$Root/logs/wowunreal-character-creation/install-verification.json"
$build|ConvertTo-Json -Depth 8|Set-Content "$Root/logs/wowunreal-build-report.json"
Write-Host "PASS published installation $commit : $($reg.succeeded) clean tests, $images reviewed captures, owned server fixtures removed"
