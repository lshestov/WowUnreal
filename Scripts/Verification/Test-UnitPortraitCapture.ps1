[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunPath,
    [Parameter(Mandatory)][string]$ReportPath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Read-Crop([Drawing.Bitmap]$Bitmap, [int]$CenterX, [int]$CenterY) {
    # Inside the circular portrait; the border, level and moving world are outside this disk.
    $rgb = [Collections.Generic.List[int]]::new()
    $lit = 0
    for ($y = -26; $y -le 26; ++$y) {
        for ($x = -26; $x -le 26; ++$x) {
            if ($x*$x + $y*$y -gt 26*26) { continue }
            $pixel = $Bitmap.GetPixel($CenterX + $x, $CenterY + $y)
            $rgb.Add($pixel.R)
            $rgb.Add($pixel.G)
            $rgb.Add($pixel.B)
            if ([Math]::Max($pixel.R,[Math]::Max($pixel.G,$pixel.B)) -gt 32) { ++$lit }
        }
    }
    [pscustomobject]@{ RGB = $rgb.ToArray(); LitRatio = $lit / ($rgb.Count / 3) }
}

function Measure-Crop($First, $Second) {
    $sum = 0
    $changed = 0
    for ($i = 0; $i -lt $First.RGB.Length; $i += 3) {
        $r = [Math]::Abs($First.RGB[$i] - $Second.RGB[$i])
        $g = [Math]::Abs($First.RGB[$i+1] - $Second.RGB[$i+1])
        $b = [Math]::Abs($First.RGB[$i+2] - $Second.RGB[$i+2])
        $sum += $r + $g + $b
        if ([Math]::Max($r,[Math]::Max($g,$b)) -gt 16) { ++$changed }
    }
    [ordered]@{ meanRgbDifference = [Math]::Round($sum / $First.RGB.Length,2); changedPixels = $changed }
}

$names = @('01-willem-required','02-willem-self-target','03-willem-rewards','04-rabbit',
    '05-player-helm','06-player-hide-helm','07-player-show-helm','08-other-player',
    '09-other-player-helm','10-spider-web-portrait','11-restored-player')
$checks = [ordered]@{}
$measurements = [ordered]@{}
$frames = @{}
foreach ($name in $names) {
    $path = Join-Path $RunPath "$name.png"
    $bitmap = [Drawing.Bitmap]::new((Resolve-Path -LiteralPath $path).Path)
    try {
        if ($bitmap.Width -ne 1024 -or $bitmap.Height -ne 768) { throw "Expected a 1024x768 capture in $path" }
        # PlayerFrame and TargetFrame have unequal pixel-rounded anchors. The quest
        # portrait has its own native inset, with the target sample shifted accordingly.
        $frame = @{ player = Read-Crop $bitmap 55 48; target = Read-Crop $bitmap 408 48 }
        if ($name -in $names[0..2]) { $frame.quest = Read-Crop $bitmap 37 139 }
        if ($name -eq $names[0]) { $frame.targetForQuest = Read-Crop $bitmap 407 47 }
        $frames[$name] = $frame
        $checks["$name/player-visible"] = $frame.player.LitRatio -gt .25
        $checks["$name/target-visible"] = $frame.target.LitRatio -gt .25
        $measurements["$name/player-lit-fraction"] = [Math]::Round($frame.player.LitRatio,3)
        $measurements["$name/target-lit-fraction"] = [Math]::Round($frame.target.LitRatio,3)
    } finally { $bitmap.Dispose() }
}

$first = $frames[$names[0]]
$bare = $frames[$names[5]].player
$hatted = $frames[$names[4]].player
$willem = Measure-Crop $first.quest $first.targetForQuest
$measurements['target-and-questgiver'] = $willem
$checks['target-and-questgiver'] = $first.quest.LitRatio -gt .25 -and $willem.meanRgbDifference -lt 29
foreach ($name in $names[0..3]) {
    $difference = Measure-Crop $frames[$name].player $bare
    $measurements["$name/bare-player"] = $difference
    $checks["$name/bare-player"] = $difference.meanRgbDifference -lt 3
}
foreach ($name in $names[1,2]) {
    $difference = Measure-Crop $first.quest $frames[$name].quest
    $measurements["$name/questgiver-persisted"] = $difference
    $checks["$name/questgiver-persisted"] = $difference.meanRgbDifference -lt 3
}
foreach ($name in $names[1,2,4,5,6,9,10]) {
    $difference = Measure-Crop $frames[$name].player $frames[$name].target
    $measurements["$name/self-target"] = $difference
    $checks["$name/self-target"] = $difference.meanRgbDifference -lt 22
}
foreach ($name in $names[5,6]) {
    $difference = Measure-Crop $frames[$name].player $hatted
    $measurements["$name/head-equipment"] = $difference
    $checks["$name/head-equipment"] = if ($name -eq $names[5]) { $difference.changedPixels -gt 200 } else { $difference.meanRgbDifference -lt 3 }
}
$peerChange = Measure-Crop $frames[$names[7]].target $frames[$names[8]].target
$measurements['other-player-equipped'] = $peerChange
$checks['other-player-equipped'] = $peerChange.changedPixels -gt 200
foreach ($name in $names[7,8]) {
    $difference = Measure-Crop $hatted $frames[$name].player
    $measurements["$name/local-player-persisted"] = $difference
    $checks["$name/local-player-persisted"] = $difference.meanRgbDifference -lt 3
}
$restored = Measure-Crop $hatted $frames[$names[10]].player
$measurements['restored-after-morph'] = $restored
$checks['restored-after-morph'] = $restored.meanRgbDifference -lt 3

$failed = @($checks.Keys | Where-Object { -not $checks[$_] })
[ordered]@{ passed = $failed.Count -eq 0; failed = $failed; checks = $checks; measurements = $measurements } |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
if ($failed.Count) { throw "Portrait capture mismatch ($($failed -join ', ')). See $ReportPath" }
