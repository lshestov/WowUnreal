param(
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Name,
    [string]$Root='D:\Games\idle_wow',
    [string]$Spec,
    [string]$ExtraCommands
)
$ErrorActionPreference='Stop'
if(-not $Spec){$Spec=Join-Path $PSScriptRoot '14-world-lighting-offline.json'}
$exe=Join-Path $Root 'runtime\client-wowunreal\WowUnreal\Binaries\Win64\WowUnreal.exe'
$output=Join-Path $Root "logs\wowunreal-lighting\$Name"
New-Item -ItemType Directory -Force -Path $output|Out-Null
$running=@(Get-CimInstance Win32_Process|Where-Object{$_.Name -match '^WowUnreal.exe$|^UnrealEditor.*exe$|^UnrealBuildTool.exe$|^ShaderCompileWorker.exe$'})
if($running.Count){throw 'Another UE process is running; preserved'}
$arguments=@('/Game/Maps/WowWorld?game=/Script/WowUnreal.WowLightingTestGameMode',
    "-wowdata=$Root\runtime\client-enUS-3.3.5a\Data","-lightingsteps=$Spec","-lightingoutput=$output",
    '-windowed','-ResX=1280','-ResY=900','-NoSound','-nosplash','-unattended',
    "-UserDir=$Root\runtime\private\wowunreal-offline-lighting","-abslog=$output\client.log")
if($ExtraCommands){$arguments+='-ExecCmds="'+$ExtraCommands+'"'}
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit(660000)){throw 'Lighting capture timed out'}
    $process.Refresh()
    if($process.ExitCode -ne 0){throw "Lighting capture exit=$($process.ExitCode)"}
} finally {
    if(-not $process.HasExited){Stop-Process -Id $process.Id -Force; $process.WaitForExit()}
}
$report=Get-Content "$output\runtime.json" -Raw|ConvertFrom-Json
if(-not $report.passed){throw "Capture failed: $($report.failure)"}
if(Select-String "$output\client.log" -Pattern 'Authenticating with|Connecting to auth|Entering world as|Fatal error:|Assertion failed:'){throw 'Unexpected network activity or fatal error'}
$expected=Get-Content $Spec -Raw|ConvertFrom-Json
$shots=0; foreach($case in $expected.cases){$shots+=if($case.shots){$case.shots}else{1}}
if(@(Get-ChildItem -LiteralPath $output -Filter '*.png').Count -ne $shots){throw "Expected $shots PNGs"}
& "$Root\logs\wowunreal-ui-completion\Test-Captures.ps1" -RunPath $output -ReportPath "$output\captures.json"
[ordered]@{timestamp=[DateTimeOffset]::Now.ToString('o');commit=(& git -C "$Root\src\WowUnreal" rev-parse HEAD).Trim();
    workingTreeModified=[bool](& git -C "$Root\src\WowUnreal" status --porcelain);sha256=(Get-FileHash -LiteralPath $exe).Hash;
    networkLogin=$false;databaseAccess=$false;serverOperations=$false;spec=$Spec;cases=$report.cases;directory=$output}|
    ConvertTo-Json -Depth 9|Set-Content "$output\verification.json"
Write-Host "PASS $Name : $($report.cases.Count) cases, $shots rendered captures; client closed"
