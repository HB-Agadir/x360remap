# Builds the Release exe and makes the portable zip:
#   windows\dist\X360RemapStudio-<version>-win64.zip
#     X360RemapStudio.exe   single file, no runtime to install
#     portable.txt          settings and working copy stay in "data" next to the exe
#     README.md
#
#   powershell -ExecutionPolicy Bypass -File package.ps1
$ErrorActionPreference = 'Stop'
$win = $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild was not found.' }
& $msbuild "$win\X360RemapWindows.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:m
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$version = (Select-String -Path "$win\src\model_win.cpp" -Pattern 'kWinAppVersion = "([^"]+)"').Matches[0].Groups[1].Value
$stage = Join-Path $win "dist\X360RemapStudio-$version-win64"
$zip = "$stage.zip"
if (Test-Path -LiteralPath $stage) { Remove-Item -Recurse -Force -LiteralPath $stage }
if (Test-Path -LiteralPath $zip) { Remove-Item -Force -LiteralPath $zip }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item "$win\bin\X360RemapStudio.exe" $stage
Copy-Item "$win\README.md" $stage
Set-Content -Path "$stage\portable.txt" -Encoding ascii -Value 'This file makes X360Remap Studio keep its settings in the "data" folder next to the exe.'
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Remove-Item -Recurse -Force -LiteralPath $stage
Write-Host "Done: $zip"
