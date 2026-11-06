param(
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Name,
    [string]$Root='D:\Games\idle_wow',
    [string]$Spec='35-world-hud-offline.json',
    [ValidateRange(800,7680)][int]$ResX=1280,
    [ValidateRange(600,4320)][int]$ResY=900
)
$ErrorActionPreference='Stop'
$run=Join-Path $Root "logs/wowunreal-hud/$Name"
if(Test-Path -LiteralPath $run){throw 'Choose a new capture name'}
if(Get-Process -Name WowUnreal,UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue){throw 'Another client or editor is running'}
$exe=Join-Path $Root 'runtime/client-wowunreal/WowUnreal/Binaries/Win64/WowUnreal.exe'
$specPath=if([IO.Path]::IsPathRooted($Spec)){$Spec}else{Join-Path $PSScriptRoot $Spec}
$scenario=Get-Content -Raw -LiteralPath $specPath | ConvertFrom-Json
New-Item -ItemType Directory -Path $run | Out-Null
$scenario | Add-Member -NotePropertyName output -NotePropertyValue $run
$scenario | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath "$run/steps.json"
$arguments=@('/Game/Maps/WowWorld?game=/Script/WowUnreal.WowViewerGameMode','-testscene=ui',
    "-wowdata=`"$Root/runtime/client-enUS-3.3.5a/Data`"","-gameplayprobe=`"$run/steps.json`"",
    "-UserDir=`"$run/user`"","-abslog=`"$run/client.log`"",'-windowed',"-ResX=$ResX","-ResY=$ResY",'-nosplash','-nosound','-unattended')
if($scenario.offlineFixture){
    if($scenario.offlineFixture -ne 'dwarf-warrior'){throw 'Unknown offline UI fixture'}
    $arguments+="-offlinefixture=$($scenario.offlineFixture)"
}
$client=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(!$client.WaitForExit(360000)){throw 'Offline HUD capture timed out'}
    if($client.ExitCode -ne 0){throw "Offline HUD capture failed (exit $($client.ExitCode)). See $run"}
    $result=Get-Content -Raw -LiteralPath "$run/probe.json" | ConvertFrom-Json
    if(!$result.passed -or $result.completedSteps -ne $scenario.steps.Count){throw 'Incomplete offline HUD scenario'}
    foreach($step in $scenario.steps | Where-Object op -eq 'capture'){
        if((Get-Item -LiteralPath "$run/$($step.name).png").Length -le 0){throw "Empty capture $($step.name)"}
    }
    Get-Item -LiteralPath "$run/probe.json"
}
finally {
    if(!$client.HasExited){$client.Kill(); $client.WaitForExit()}
    $client.Dispose()
}
