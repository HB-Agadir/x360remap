# Builds and runs the tests of X360Remap for Windows.
#
#   powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1
#
# 1. key table, settings round trip, folder guards (no network);
# 2. the FTP client and the safe write against tests\fake_aurora_ftp.py
#    (needs Python 3; skipped with a message if it is missing), in four server
#    variants: normal, no rename over an existing file, no NLST, cable pulled
#    in the middle of the upload.
$ErrorActionPreference = 'Stop'
$tests = $PSScriptRoot
$win   = Split-Path $tests -Parent
$root  = Split-Path $win -Parent
$out   = Join-Path $win 'obj\tests'
New-Item -ItemType Directory -Force $out | Out-Null

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ tools was not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$sources = @(
    "$tests\test_windows.cpp",
    "$win\src\console_link.cpp",
    "$win\src\live_watch.cpp",
    "$win\src\install_win.cpp",
    "$win\src\update_win.cpp",
    "$win\src\pc_devices_win.cpp",
    "$win\src\zip_win.cpp",
    "$root\application\stb_image_impl.cpp",
    "$root\application\donation_links.cpp",
    "$win\src\key_capture_win.cpp",
    "$root\application\config_writer.cpp",
    "$root\X360Remap\mapping.cpp",
    "$root\X360Remap\mouse_calibration.cpp",
    "$root\X360Remap\known_titles.cpp",
    "$root\X360Remap\known_devices.cpp",
    "$root\X360Remap\hud_layout.cpp",
    "$root\X360Remap\camera_learn.cpp",
    "$root\X360Remap\camera_known.cpp"
) | ForEach-Object { '"' + $_ + '"' }
$cl = "cl /nologo /std:c++17 /EHsc /MT /O1 /utf-8 /W3 /D_CRT_SECURE_NO_WARNINGS /D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS /DNOMINMAX " +
      "/I`"$root\X360Remap`" /Fo`"$out\\`" /Fe`"$out\test_windows.exe`" $($sources -join ' ') user32.lib"
cmd /c "`"$vcvars`" >nul 2>nul && $cl > `"$out\build.log`" 2>&1"
if ($LASTEXITCODE -ne 0) {
    Get-Content "$out\build.log" | Where-Object { $_ -match 'error' } | Select-Object -First 30
    throw 'The tests did not compile (full output: obj\tests\build.log).'
}

$exe = Join-Path $out 'test_windows.exe'
$failed = 0

Write-Host '--- local tests'
& $exe
if ($LASTEXITCODE -ne 0) { $failed++ }

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    Write-Host 'Python not found: FTP tests skipped.'
} else {
    $variants = @(
        @{ name = 'normal server';            args = @();                       mode = 'ftp' },
        @{ name = 'no rename over a file';    args = @('--no-rename-over');     mode = 'ftp' },
        @{ name = 'server without NLST';      args = @('--no-nlst');            mode = 'ftp' },
        @{ name = 'cable pulled mid-upload';  args = @('--cut-new-after', '2000'); mode = 'ftpcut' },
        @{ name = 'console freezes';          args = @('--xbdm-die-after', '3'); mode = 'watchdeath' },
        @{ name = 'console reboots';          args = @('--xbdm-die-after', '3', '--xbdm-reboot'); mode = 'watchreboot' }
    )
    $port = 2121
    foreach ($v in $variants) {
        $port++
        $ftpRoot = Join-Path $out "ftproot_$port"
        if (Test-Path $ftpRoot) { Remove-Item -Recurse -Force $ftpRoot }
        $studio = Join-Path $ftpRoot 'Hdd1\X360RemapStudio'
        New-Item -ItemType Directory -Force $studio | Out-Null
        [IO.File]::WriteAllText("$studio\X360Remap.json", "[`n    {`n        `"vid`": 5426,`n        `"pid`": 555,`n        `"mouseSensitivity`": 12000`n    }`n]")
        $server = Start-Process $python.Source -ArgumentList (@("`"$tests\fake_aurora_ftp.py`"", "`"$ftpRoot`"", '--port', $port, '--xbdm-port', ($port + 1000)) + $v.args) `
                                -PassThru -WindowStyle Hidden -RedirectStandardOutput "$out\server_$port.log"
        Start-Sleep -Milliseconds 800
        Write-Host "--- FTP: $($v.name)"
        try {
            & $exe $v.mode $port
            if ($LASTEXITCODE -ne 0) { $failed++ }
        } finally {
            Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

if ($failed -gt 0) { Write-Host "FAILED: $failed test run(s)"; exit 1 }
Write-Host 'ALL TESTS PASSED'
