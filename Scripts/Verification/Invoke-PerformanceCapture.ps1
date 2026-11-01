param(
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Name,
    [string]$Root='D:\Games\idle_wow',
    [string]$Spec,
    [ValidateSet('fresh-process-existing-project-cache','fresh-process-isolated-project-cache')]
    [string]$CacheMode='fresh-process-existing-project-cache'
)
$ErrorActionPreference='Stop'
if(-not $Spec){$Spec=Join-Path $PSScriptRoot '17-performance-world.json'}
$exe=Join-Path $Root 'runtime\client-wowunreal\WowUnreal\Binaries\Win64\WowUnreal.exe'
$output=Join-Path $Root "logs\wowunreal-performance\$Name"
if(Test-Path -LiteralPath $output){throw 'Use a new name to preserve previous evidence'}
$running=@(Get-CimInstance Win32_Process|Where-Object{$_.Name -match '^WowUnreal.exe$|^UnrealEditor.*exe$|^UnrealBuildTool.exe$|^ShaderCompileWorker.exe$|^AutomationTool.exe$'})
if($running.Count){throw 'Another UE process is running; preserved'}
New-Item -ItemType Directory -Force -Path $output|Out-Null
$userDir=Join-Path $Root 'runtime\private\wowunreal-offline-performance'
if($CacheMode -eq 'fresh-process-isolated-project-cache'){$userDir=Join-Path $output 'isolated-user'}
$arguments=@('/Game/Maps/WowWorld?game=/Script/WowUnreal.WowPerformanceTestGameMode',
    "-wowdata=$Root\runtime\client-enUS-3.3.5a\Data","-perfsteps=$Spec","-perfoutput=$output",'-wowstreamprofile',
    '-windowed','-ResX=1280','-ResY=900','-NoSound','-nosplash','-unattended','-novsync',
    "-UserDir=$userDir","-abslog=$output\client.log")
$start=[DateTimeOffset]::UtcNow
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit(1260000)){throw 'Performance route timed out'}
    $process.Refresh(); if($process.ExitCode -ne 0){throw "Performance exit=$($process.ExitCode)"}
} finally {
    if(-not $process.HasExited){Stop-Process -Id $process.Id -Force; $process.WaitForExit()}
}
$elapsed=([DateTimeOffset]::UtcNow-$start).TotalSeconds
$report=Get-Content "$output\runtime.json" -Raw|ConvertFrom-Json
if(-not $report.passed){throw "Route failed: $($report.failure)"}
if(Select-String "$output\client.log" -Pattern 'Authenticating with|Connecting to auth|Entering world as|Fatal error:|Assertion failed:|Failed to compile Material|invalid ShaderMap|Invalid shader map|will use default material'){throw 'Network/fatal/shader failure invalidates run'}
$expected=Get-Content $Spec -Raw|ConvertFrom-Json
if(@(Get-ChildItem -LiteralPath $output -Filter '*.png').Count -ne $expected.steps.Count){throw 'Incomplete captures'}
& "$Root\logs\wowunreal-ui-completion\Test-Captures.ps1" -RunPath $output -ReportPath "$output\captures.json"
[ordered]@{timestamp=$start.ToString('o');commit=(& git -C "$Root\src\WowUnreal" rev-parse HEAD).Trim();
    workingTreeModified=[bool](& git -C "$Root\src\WowUnreal" status --porcelain);sha256=(Get-FileHash -LiteralPath $exe).Hash;
    spec=$Spec;specSha256=(Get-FileHash -LiteralPath $Spec).Hash;cacheMode=$CacheMode;
    process='new';systemFileCache='uncontrolled; not cleared; potentially warm';projectUserDir=$userDir;
    networkLogin=$false;databaseAccess=$false;serverOperations=$false;wallSeconds=$elapsed;steps=$report.steps;directory=$output}|
    ConvertTo-Json -Depth 12|Set-Content "$output\verification.json"
Write-Host "PASS $Name : $($report.steps.Count) steps, real rendered captures; client closed"
