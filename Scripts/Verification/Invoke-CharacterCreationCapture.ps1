param(
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Name,
    [string]$Root='D:\Games\idle_wow',[string]$Spec
)
$ErrorActionPreference='Stop'
if(-not $Spec){$Spec=Join-Path $PSScriptRoot '22-character-creation-offline.json'}
if(@(Get-CimInstance Win32_Process|Where-Object{$_.Name -match '^WowUnreal.exe$|^UnrealEditor.*exe$|^UnrealBuildTool.exe$|^ShaderCompileWorker.exe$'}).Count){throw 'Another UE process is running; preserved'}
$output=Join-Path $Root "logs\wowunreal-character-creation\$Name"
if(Test-Path -LiteralPath $output){throw 'Use a new evidence name'}
New-Item -ItemType Directory -Path $output -Force|Out-Null
$exe=Join-Path $Root 'runtime\client-wowunreal\WowUnreal\Binaries\Win64\WowUnreal.exe'
$hash=(Get-FileHash -LiteralPath $exe).Hash
$provenance=& "$PSScriptRoot\Get-RuntimeProvenance.ps1" -Source "$Root\src\WowUnreal"
$arguments=@('/Game/Maps/WowWorld?game=/Script/WowUnreal.WowCharacterCreationTestGameMode',
    "-wowdata=$Root\runtime\client-enUS-3.3.5a\Data",'-testscene=character',"-creationsteps=$Spec","-creationoutput=$output",
    '-windowed','-ResX=1280','-ResY=900','-NoSound','-nosplash','-unattended',
    "-UserDir=$Root\runtime\private\wowunreal-creation-offline","-abslog=$output\client.log")
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit(540000)){throw 'Character creation capture timed out'}
    $process.Refresh(); if($process.ExitCode -ne 0){throw "Capture exit=$($process.ExitCode)"}
} finally {if(-not $process.HasExited){Stop-Process -Id $process.Id -Force;$process.WaitForExit()}}
$report=Get-Content "$output\runtime.json" -Raw|ConvertFrom-Json
if(-not $report.passed -or $report.screenCycles -ne 12){throw "Capture failed: $($report.failure)"}
if(Select-String "$output\client.log" -Pattern 'Authenticating with|Connecting to auth|Entering world as|Fatal error:|Assertion failed:|Failed to compile Material|invalid ShaderMap|will use default material'){throw 'Unexpected network/fatal/shader fallback'}
$expected=Get-Content $Spec -Raw|ConvertFrom-Json
if(@(Get-ChildItem -LiteralPath $output -Filter '*.png').Count -ne $expected.cases.Count){throw 'Incomplete engine captures'}
& "$Root\logs\wowunreal-ui-completion\Test-Captures.ps1" -RunPath $output -ReportPath "$output\captures.json"
$calibrations=0
foreach($case in $report.cases){
    if(-not $case.grayCardSamples){continue}
    $bitmap=[System.Drawing.Bitmap]::new((Join-Path $output ($case.name+'.png')))
    try {
        foreach($sample in $case.grayCardSamples){
            $pixel=$bitmap.GetPixel([int][Math]::Round($sample.x),[int][Math]::Round($sample.y))
            foreach($channel in @($pixel.R,$pixel.G,$pixel.B)){
                if([Math]::Abs($channel-$sample.expected) -gt 8){throw "Incorrect linear-to-display colour: $($case.name), expected $($sample.expected), received $channel"}
            }
            ++$calibrations
        }
    } finally {$bitmap.Dispose()}
}
if($calibrations -ne 4*@($expected.cases|Where-Object {$_.calibrate}).Count){throw 'Incomplete rendered grey-card calibration'}
[ordered]@{timestamp=[DateTimeOffset]::Now.ToString('o');commit=$provenance.commit;
    workingTreeModified=$provenance.workingTreeModified;runtimeInputsModified=$provenance.runtimeInputsModified;
    localDocumentation=$provenance.localDocumentation;sha256=$hash;networkLogin=$false;databaseAccess=$false;
    spec=$Spec;specSha256=(Get-FileHash -LiteralPath $Spec).Hash;cases=$report.cases.Count;screenCycles=$report.screenCycles;graySamplesVerified=$calibrations;directory=$output}|
    ConvertTo-Json -Depth 8|Set-Content "$output\verification.json"
Write-Host "PASS $Name : $($report.cases.Count) rendered cases, 12 cleaned screen cycles; client closed"
