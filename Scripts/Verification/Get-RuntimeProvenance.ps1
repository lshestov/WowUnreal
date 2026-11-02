param([string]$Source=(Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$ErrorActionPreference='Stop'
$commit=(& git -C $Source rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0){throw 'Cannot resolve source revision'}
$changes=@(& git -C $Source status --porcelain --untracked-files=all)
if($LASTEXITCODE -ne 0){throw 'Cannot inspect source checkout'}
$local=@(); $runtime=@()
foreach($change in $changes){
    # These two deliberately local documents were explicitly excluded from
    # publication. Only unstaged edits to these exact paths are exempted.
    $path=$change.Substring(3).Replace('\','/')
    if($change.Substring(0,2) -eq ' M' -and $path -in @('Docs/WorldPerformance.md','IMPLEMENTATION_PLAN.md')){
        $local+=@{path=$path;sha256=(Get-FileHash -LiteralPath (Join-Path $Source $path)).Hash}
    } else {$runtime+=$change}
}
[pscustomobject]@{commit=$commit;workingTreeModified=[bool]$changes;runtimeInputsModified=[bool]$runtime;
    runtimeChanges=$runtime;localDocumentation=$local;localPatchesApplied=$false}
