param([Parameter(Mandatory)][string]$Before,[Parameter(Mandatory)][string]$After)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$rows=@(foreach($file in Get-ChildItem -LiteralPath $Before -Filter '*.png') {
    $other=Join-Path $After $file.Name
    if(-not (Test-Path -LiteralPath $other)){throw "Missing $other"}
    $a=[Drawing.Bitmap]::new($file.FullName); $b=[Drawing.Bitmap]::new($other)
    try {
        if($a.Size -ne $b.Size){throw 'Capture dimensions differ'}
        $sum=0L; $lower=0L; $n=0; $ln=0; $maximum=0
        for($y=0;$y -lt $a.Height;$y+=4){for($x=0;$x -lt $a.Width;$x+=4){
            $p=$a.GetPixel($x,$y); $q=$b.GetPixel($x,$y)
            $r=[Math]::Abs([int]$p.R-[int]$q.R); $g=[Math]::Abs([int]$p.G-[int]$q.G); $blue=[Math]::Abs([int]$p.B-[int]$q.B)
            $sum+=$r+$g+$blue; $n+=3; $maximum=[Math]::Max($maximum,[Math]::Max($r,[Math]::Max($g,$blue)))
            if($y -ge $a.Height/2){$lower+=$r+$g+$blue; $ln+=3}
        }}
        [pscustomobject]@{name=$file.Name;sampleStride=4;meanAbsoluteRgb=$sum/[double]$n;lowerMeanAbsoluteRgb=$lower/[double]$ln;maxSampledDifference=$maximum}
    } finally {$a.Dispose();$b.Dispose()}
})
if(-not $rows.Count){throw 'No captures'}
$rows|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $After 'visual-comparison.json')
$rows|Format-Table
