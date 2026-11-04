$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output=Join-Path $repo 'Saved\GraphicsChecks'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs=(& $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if(-not $vs){throw 'MSVC C++ tools unavailable'}
$vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$source=Join-Path $PSScriptRoot 'Test-GraphicsPolicy.cpp'
$include=Join-Path $repo 'Source\WowAssets\Public'
$exe=Join-Path $output 'graphics-policy.exe'
$obj=Join-Path $output 'graphics-policy.obj'
$command='"{0}" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /I"{1}" "{2}" /Fo"{3}" /Fe"{4}"' -f $vcvars,$include,$source,$obj,$exe
& $env:ComSpec /d /s /c $command
if($LASTEXITCODE -ne 0){throw 'Graphics policy compilation failed'}
& $exe
if($LASTEXITCODE -ne 0){throw 'Graphics policy checks failed'}
