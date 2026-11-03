param([Parameter(Mandatory)][string[]]$Runs,[Parameter(Mandatory)][string]$Regression,
    [string]$Root='D:\Games\idle_wow',[switch]$Reviewed)
$ErrorActionPreference='Stop'
if(-not $Reviewed){throw 'Individually inspect every final PNG before accepting the rendered evidence'}
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
if($reg.failed -or $reg.notRun -or $reg.succeededWithWarnings -or $reg.succeeded -lt 195){throw 'Regression failed, incomplete or has warnings'}
$resolutions=[Collections.Generic.HashSet[string]]::new();$images=0;$cycles=0;$evidence=@()
foreach($name in $Runs){
    $path=Join-Path $Root "logs/wowunreal-login/$name"
    $proof=Get-Content "$path/verification.json" -Raw|ConvertFrom-Json
    $render=Get-Content "$path/runtime.json" -Raw|ConvertFrom-Json
    $audio=Get-Content "$path/audio.json" -Raw|ConvertFrom-Json
    if($proof.commit -ne $commit -or $proof.runtimeInputsModified -ne $false -or $proof.sha256 -ne $hashes[0] -or -not $render.passed -or $render.steps.Count -ne 34 -or $render.cycles -lt 10){throw 'Incomplete or modified rendered evidence'}
    if($proof.networkLogin -ne $false -or $proof.databaseAccess -ne $false -or -not $proof.actionsAreSpies){throw 'Offline evidence classification differs'}
    if(-not $audio.passed -or -not $audio.hardwareMixer -or -not $audio.continuedAcrossMute -or -not $audio.stoppedForWorldEntry -or -not $audio.restartedOnReturn){throw 'Actual mixer/settings/lifecycle proof missing'}
    if((Get-FileHash -LiteralPath $proof.spec).Hash -ne $proof.specSha256){throw 'Replay specification changed'}
    $captures=@(Get-Content "$path/captures.json" -Raw|ConvertFrom-Json)
    if($captures.Count -ne 16){throw 'Missing final phase/UI captures'}
    foreach($png in $captures){
        if($png.width -ne $proof.resX -or $png.height -ne $proof.resY -or $png.nonBlackRatio -lt .01 -or (Get-FileHash -LiteralPath $png.file).Hash -ne $png.sha256){throw 'Empty, changed or wrong-size capture'}
        ++$images
    }
    [void]$resolutions.Add("$($proof.resX)x$($proof.resY)");$cycles+=$render.cycles;$evidence+=$path
}
foreach($size in @('1280x900','1280x720','1024x768')){if(-not $resolutions.Contains($size)){throw "Missing resolution: $size"}}
if(@(Get-CimInstance Win32_Process|Where-Object {$_.Name -match '^Wow.exe$|^WowUnreal.exe$|^UnrealEditor.*exe$|^UnrealBuildTool.exe$|^ShaderCompileWorker.exe$'}).Count){throw 'A client or UE process remains running'}
$report=[ordered]@{timestamp=[DateTimeOffset]::Now.ToString('o');commit=$commit;published=$true;runtimeInstalled=$true;
    runtimeInputsModified=$false;workingTreeModified=$provenance.workingTreeModified;localDocumentation=$provenance.localDocumentation;localPatchesApplied=$false;
    sha256=$hashes[0];runtimeInputFiles=$inputs.Count;sourceFiles=$sourceFiles.Count;packageFiles=$packageFiles.Count;
    testsPassed=$reg.succeeded;testsWithWarnings=0;screenshotCount=$images;screenTransitions=$cycles;allFinalPngsIndividuallyReviewed=$true;
    resolutions=@($resolutions);runs=$evidence;runningClients=0;offlineLoginVerified=$true;nativeServerVerified=$false;
    limitation='Offline screen transitions use spies. Real authentication, world entry and logout require a separate owned-account server run and protected-state baseline approval.'}
$report|ConvertTo-Json -Depth 12|Set-Content "$Root/logs/wowunreal-login/install-verification.json"
Write-Host "PASS installed published login $commit : $($reg.succeeded) clean tests, $images reviewed captures; native server QA remains separate"
