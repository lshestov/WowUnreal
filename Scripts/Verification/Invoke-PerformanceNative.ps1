param([Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Name,[string]$Root='D:\Games\idle_wow',[string]$Spec)
$ErrorActionPreference='Stop'
if(-not $Spec){$Spec=Join-Path $PSScriptRoot '17-performance-native.json'}
$exe=Join-Path $Root 'runtime\client-wowunreal\WowUnreal\Binaries\Win64\WowUnreal.exe'
$output=Join-Path $Root 'logs\wowunreal-performance'
$record=Join-Path $output "$Name-native.json"
if(Test-Path -LiteralPath $record){throw 'Use a new evidence name'}
$hash=(Get-FileHash -LiteralPath $exe).Hash
$started=[DateTimeOffset]::UtcNow
& 'C:\Users\kredl\AppData\Local\Programs\Python\Python313\python.exe' "$Root\scripts\test-wowunreal-ui-primary.py" --steps $Spec --name $Name *> "$output\$Name-wrapper.log"
if($LASTEXITCODE -ne 0){throw "Native route failed; inspect $Name-wrapper.log"}
$fixture=Get-Content "$Root\runtime\private\wowunreal-ui-primary-fixture.json" -Raw|ConvertFrom-Json
if(-not $fixture.closed -or -not $fixture.restored -or $fixture.password -or $fixture.protectedHash -ne $fixture.protectedAfter.hash){throw 'Owned QA cleanup/fingerprint guard failed'}
if((Get-FileHash -LiteralPath $exe).Hash -ne $hash){throw 'Executable changed during route'}
$run=$fixture.lastRun
$probe=Get-Content "$run\probe.json" -Raw|ConvertFrom-Json
if(-not $probe.passed){throw 'Native probe did not pass'}
if(Test-Path -LiteralPath (Join-Path $fixture.userDir 'Saved\WowCredentials.json')){throw 'Owned credentials remain'}
& "$Root\logs\wowunreal-ui-completion\Test-Captures.ps1" -RunPath $run -ReportPath "$output\$Name-captures.json"
[ordered]@{timestamp=$started.ToString('o');commit=(& git -C "$Root\src\WowUnreal" rev-parse HEAD).Trim();
    workingTreeModified=[bool](& git -C "$Root\src\WowUnreal" status --porcelain);sha256=$hash;spec=$Spec;specSha256=(Get-FileHash -LiteralPath $Spec).Hash;
    passed=$true;run=$run;completedSteps=$probe.completedSteps;networkLogin=$true;ownedFixturesClosed=$true;protectedFingerprintsMatch=$true;credentialsRemoved=$true;
    captures="$output\$Name-captures.json"}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $record
Write-Host "PASS $Name : native UI/network route, owned fixtures removed, protected fingerprints match"
