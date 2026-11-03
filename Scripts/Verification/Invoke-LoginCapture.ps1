param([Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Name,[string]$Root='D:\Games\idle_wow',[string]$Spec,
    [ValidateRange(800,7680)][int]$ResX=1280,[ValidateRange(600,4320)][int]$ResY=900)
$ErrorActionPreference='Stop'
if(-not $Spec){$Spec=Join-Path $PSScriptRoot '30-login-smoke.json'}
if(@(Get-CimInstance Win32_Process|Where-Object {$_.Name -match '^Wow.exe$|^WowUnreal.exe$|^UnrealEditor.*exe$|^UnrealBuildTool.exe$|^ShaderCompileWorker.exe$'}).Count){throw 'Another client or UE process is running; preserved'}
$output=Join-Path $Root "logs/wowunreal-login/$Name"
if(Test-Path -LiteralPath $output){throw 'Use a new evidence name'}
New-Item -ItemType Directory -Path $output -Force|Out-Null
$exe=Join-Path $Root 'runtime/client-wowunreal/WowUnreal/Binaries/Win64/WowUnreal.exe';$hash=(Get-FileHash $exe).Hash
$provenance=& "$PSScriptRoot/Get-RuntimeProvenance.ps1" -Source "$Root/src/WowUnreal"
$arguments=@('/Game/Maps/WowWorld?game=/Script/WowUnreal.WowLoginTestGameMode',"-wowdata=$Root/runtime/client-enUS-3.3.5a/Data",'-testscene=character',"-loginsteps=$Spec","-loginoutput=$output",
    '-windowed',"-ResX=$ResX","-ResY=$ResY",'-nosplash','-unattended',"-UserDir=$Root/runtime/private/wowunreal-login-offline","-abslog=$output/client.log")
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {if(-not $process.WaitForExit(300000)){throw 'Login capture timed out'};$process.Refresh();if($process.ExitCode -ne 0){throw "Login capture exited $($process.ExitCode)"}}
finally {if(-not $process.HasExited){Stop-Process -Id $process.Id -Force;$process.WaitForExit()}}
$report=Get-Content "$output/runtime.json" -Raw|ConvertFrom-Json
$expected=Get-Content $Spec -Raw|ConvertFrom-Json
if(-not $report.passed -or $report.steps.Count -ne $expected.steps.Count){throw "Login capture failed: $($report.failure)"}
if(Select-String "$output/client.log" -Pattern 'Authenticating with|Connecting to auth|Entering world as|Fatal error:|Assertion failed:|Failed to compile Material|invalid ShaderMap|will use default material|LOGIN_REPLAY_FAIL'){throw 'Unexpected network/fatal/shader fallback'}
if(-not (Test-Path "$output/mixer.wav")){throw 'Real mixer recording missing'}
& 'C:\Users\kredl\AppData\Local\Programs\Python\Python313\python.exe' "$PSScriptRoot/Test-LoginAudio.py" --run $output
if($LASTEXITCODE -ne 0){throw 'Actual audio signal/settings/lifecycle verification failed'}
if(@(Get-ChildItem $output -Filter '*.png').Count -ne @($expected.steps|Where-Object capture).Count){throw 'Incomplete engine captures'}
& "$Root/logs/wowunreal-ui-completion/Test-Captures.ps1" -RunPath $output -ReportPath "$output/captures.json"
$captures=@(Get-Content "$output/captures.json" -Raw|ConvertFrom-Json)
if(@($captures|Where-Object {$_.width -ne $ResX -or $_.height -ne $ResY}).Count){throw 'Captured viewport size differs from requested resolution'}
[ordered]@{timestamp=[DateTimeOffset]::Now.ToString('o');commit=$provenance.commit;runtimeInputsModified=$provenance.runtimeInputsModified;workingTreeModified=$provenance.workingTreeModified;
    localDocumentation=$provenance.localDocumentation;sha256=$hash;networkLogin=$false;databaseAccess=$false;actionsAreSpies=$true;audioEnabled=$true;
    steps=$report.steps.Count;cycles=$report.cycles;resX=$ResX;resY=$ResY;spec=$Spec;specSha256=(Get-FileHash $Spec).Hash;directory=$output}|ConvertTo-Json -Depth 8|Set-Content "$output/verification.json"
Write-Host "PASS $Name : $($report.steps.Count) menu steps, real mixer recording and engine captures; client closed"
