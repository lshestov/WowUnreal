param([Parameter(Mandatory=$true)][string]$LuaChecker)
$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$checker=(Resolve-Path $LuaChecker).Path
$chunks=[System.Collections.Generic.List[string]]::new()
$count=0
foreach($file in Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.json') {
    $scenario=Get-Content -Raw -LiteralPath $file.FullName | ConvertFrom-Json
    if($scenario.width -ne 1024 -or $scenario.height -ne 768 -or !$scenario.steps) { throw "Invalid layout scenario: $($file.Name)" }
    foreach($step in $scenario.steps) {
        if($step.op -notin @('wait_world','lua','lua_wait','drag_frame','capture')) {throw "Unknown operation: $($step.op)"}
        if($step.op -eq 'lua') {$chunks.Add("do`n$($step.code)`nend"); $count++}
        if($step.op -eq 'lua_wait') {$chunks.Add("do local ready=not not ($($step.condition)) end"); $count++}
        if($step.op -eq 'drag_frame' -and (!$step.name -or $null -eq $step.dx -or $null -eq $step.dy)) {throw 'Drag requires a name and exact deltas'}
    }
}
[xml]$xml=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'IdleWowLayoutProbe/Probe.xml')
if($xml.Ui.Frame.dontSavePosition -ne 'true') {throw 'Missing XML persistence opt-out fixture'}
$toc=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'IdleWowLayoutProbe/IdleWowLayoutProbe.toc')
if($toc -match '(?im)^##\s*SavedVariables(?:PerCharacter)?\s*:') {throw 'The layout probe must not save its own placement'}
$api=Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'Source/WowUI/Private/LuaApi/LuaFrameApi.cpp')
foreach($method in @('SetUserPlaced','IsUserPlaced','SetDontSavePosition','GetDontSavePosition')) {
    if([regex]::Matches($api,'\{"'+$method+'",').Count -ne 1) {throw "Missing or duplicate registration: $method"}
}
$testSource=Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'Source/WowUI/Private/Tests/IdleWowUserPlacedTests.cpp')
foreach($match in [regex]::Matches($testSource,'(?s)ExecuteString\(TEXT\(R"LUA\((.*?)\)LUA"\)\)')) {
    $chunks.Add("do`n$($match.Groups[1].Value)`nend"); $count++
}
foreach($match in [regex]::Matches($testSource,'ExecuteString\(TEXT\("([^"\\]*)"\)\)')) {
    $chunks.Add("do`n$($match.Groups[1].Value)`nend"); $count++
}
$temporary=Join-Path ([System.IO.Path]::GetTempPath()) ('layout-probe-'+[guid]::NewGuid().ToString('N')+'.lua')
try {
    [System.IO.File]::WriteAllText($temporary,($chunks -join "`n"),[System.Text.UTF8Encoding]::new($false))
    & $checker $temporary
    if($LASTEXITCODE -ne 0) {throw 'A native scenario contains invalid Lua 5.1 code'}
} finally {
    if(Test-Path -LiteralPath $temporary) {Remove-Item -LiteralPath $temporary}
}
Write-Output "PASS: 3 JSON scenarios and UE test source, $count Lua 5.1 chunks, XML/TOC fixture and four unique API registrations"
