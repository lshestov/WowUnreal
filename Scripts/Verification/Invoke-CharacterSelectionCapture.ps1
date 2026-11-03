param([Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Name,[string]$Root='D:\Games\idle_wow',[string]$Spec,
    [ValidateRange(800,7680)][int]$ResX=1280,[ValidateRange(600,4320)][int]$ResY=900)
$ErrorActionPreference='Stop'
if(-not $Spec){$Spec=Join-Path $PSScriptRoot '26-character-selection-offline.json'}
if(@(Get-CimInstance Win32_Process|Where-Object {$_.Name -match '^WowUnreal.exe$|^UnrealEditor.*exe$|^UnrealBuildTool.exe$|^ShaderCompileWorker.exe$'}).Count){throw 'Another UE process is running; preserved'}
$output=Join-Path $Root "logs/wowunreal-character-selection/$Name"
if(Test-Path -LiteralPath $output){throw 'Use a new evidence name'}
New-Item -ItemType Directory -Path $output -Force|Out-Null
$exe=Join-Path $Root 'runtime/client-wowunreal/WowUnreal/Binaries/Win64/WowUnreal.exe';$hash=(Get-FileHash $exe).Hash
$provenance=& "$PSScriptRoot/Get-RuntimeProvenance.ps1" -Source "$Root/src/WowUnreal"
$arguments=@('/Game/Maps/WowWorld?game=/Script/WowUnreal.WowCharacterSelectionTestGameMode',"-wowdata=$Root/runtime/client-enUS-3.3.5a/Data",'-testscene=character',"-selectionsteps=$Spec","-selectionoutput=$output",
    '-windowed',"-ResX=$ResX","-ResY=$ResY",'-NoSound','-nosplash','-unattended',"-UserDir=$Root/runtime/private/wowunreal-selection-offline","-abslog=$output/client.log")
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {if(-not $process.WaitForExit(300000)){throw 'Selection capture timed out'};$process.Refresh();if($process.ExitCode -ne 0){throw "Selection capture exited $($process.ExitCode)"}}
finally {if(-not $process.HasExited){Stop-Process -Id $process.Id -Force;$process.WaitForExit()}}
$report=Get-Content "$output/runtime.json" -Raw|ConvertFrom-Json
$expected=Get-Content $Spec -Raw|ConvertFrom-Json
if(-not $report.passed -or -not $report.inputGuardsVerified -or $report.cases.Count -ne $expected.cases.Count -or $report.screenCycles -lt $expected.cases.Count){throw "Selection capture failed: $($report.failure)"}
if(Select-String "$output/client.log" -Pattern 'Authenticating with|Connecting to auth|Entering world as|Fatal error:|Assertion failed:|Failed to compile Material|invalid ShaderMap|will use default material'){throw 'Unexpected network/fatal/shader fallback'}
if(@(Get-ChildItem $output -Filter '*.png').Count -ne $expected.cases.Count){throw 'Incomplete engine captures'}
& "$Root/logs/wowunreal-ui-completion/Test-Captures.ps1" -RunPath $output -ReportPath "$output/captures.json"
$captures=@(Get-Content "$output/captures.json" -Raw|ConvertFrom-Json)
if(@($captures|Where-Object {$_.width -ne $ResX -or $_.height -ne $ResY}).Count){throw 'Captured viewport size differs from requested resolution'}
[ordered]@{timestamp=[DateTimeOffset]::Now.ToString('o');commit=$provenance.commit;runtimeInputsModified=$provenance.runtimeInputsModified;workingTreeModified=$provenance.workingTreeModified;
    localDocumentation=$provenance.localDocumentation;sha256=$hash;networkLogin=$false;databaseAccess=$false;actionsAreSpies=$true;inputGuardsVerified=$true;
    cases=$report.cases.Count;screenCycles=$report.screenCycles;resX=$ResX;resY=$ResY;spec=$Spec;specSha256=(Get-FileHash $Spec).Hash;directory=$output}|ConvertTo-Json -Depth 8|Set-Content "$output/verification.json"
Write-Host "PASS $Name : $($report.cases.Count) rendered selection cases, real Slate controls, enum appearance/equipment, safe action spies; client closed"
