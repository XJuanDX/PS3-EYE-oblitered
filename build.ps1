# build.ps1 — builds ps3eye-probe.exe and ps3eye-grab.exe with MSVC 2019.
#
# No CMake, no Ninja: the toolchain here is VS 2019 Build Tools 16.11 with the
# Windows 10 SDK 19041 only. vcvars64.bat is sourced into this process so cl.exe
# and the SDK/lib paths are correct.
#
# Usage:
#   .\build.ps1
#   .\build.ps1 -Clean

[CmdletBinding()]
param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$root   = $PSScriptRoot
$build  = Join-Path $root 'build'
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat'

if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "vcvars64.bat not found at:`n  $vcvars`nInstall VS 2019 Build Tools with the C++ x64 workload."
}

if ($Clean -and (Test-Path -LiteralPath $build)) {
    Remove-Item -LiteralPath $build -Recurse -Force
}

foreach ($d in @($build, (Join-Path $build 'probe'), (Join-Path $build 'grab'), (Join-Path $build 'audio'), (Join-Path $build 'tests'), (Join-Path $build 'dshow'))) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}

# Import the MSVC environment from vcvars64.bat into this PowerShell process.
$env:VSCMD_SKIP_SENDTELEMETRY = '1'
$env:VSCMD_ARG_TGT_ARCH       = 'x64'

Write-Host 'sourcing vcvars64.bat...'
$envLines = cmd /c "`"$vcvars`" >nul 2>&1 && set"
if (-not $envLines) {
    throw 'vcvars64.bat produced no environment. Run it from a plain shell to see the error.'
}
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path ("env:" + $matches[1]) -Value $matches[2]
    }
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'cl.exe is not on PATH after sourcing vcvars64.bat.'
}

$common = @(
    'src\ps3eye_usb.c',
    'src\ov534.c',
    'src\ps3eye_frame.c',
    'src\capture_session.c',
    'src\image_out.c'
)

$cflags = @(
    '/nologo',
    '/W4',
    '/O2',
    '/std:c11',
    '/D_CRT_SECURE_NO_WARNINGS',
    '/wd5105',
    '/I', 'src'
)

$libs = @('winusb.lib', 'setupapi.lib', 'advapi32.lib')

function Build-Target {
    param(
        [string]$Name,
        [string]$Main,
        [string]$ObjDir
    )

    # Relative paths on purpose: the workspace path contains a space, and
    # cl.exe mangles quoted /Fo arguments that end in a backslash.
    $out = "build\$Name.exe"
    $clargs = $cflags + @("/Fo$ObjDir\", "/Fe$out", $Main) + $common + $libs

    Write-Host "building $out"
    & cl.exe @clargs
    if ($LASTEXITCODE -ne 0) {
        throw "compilation failed for $Name (exit $LASTEXITCODE)"
    }
}

function Build-DShowDll {
    $out = "build\ps3eye_dshow.dll"
    $objDir = "build\dshow\"
    $dshowSources = @(
        'src\dshow\dll_main.cpp',
        'src\dshow\ps3eye_filter.cpp',
        'src\capture_session.c',
        'src\ps3eye_usb.c',
        'src\ov534.c',
        'src\ps3eye_frame.c'
    )
    $dshowFlags = @(
        '/nologo',
        '/W4',
        '/O2',
        '/std:c++17',
        '/EHsc',
        '/LD',
        '/D_CRT_SECURE_NO_WARNINGS',
        '/wd5105',
        '/I', 'src',
        '/I', 'src\dshow',
        "/Fo$objDir",
        "/Fe$out"
    )
    $dshowLibs = @(
        'winusb.lib',
        'setupapi.lib',
        'advapi32.lib',
        'ole32.lib',
        'oleaut32.lib',
        'strmiids.lib'
    )
    $linkArgs = @(
        '/link',
        '/DEF:src\dshow\ps3eye_dshow.def'
    )

    Write-Host "building $out"
    & cl.exe @dshowFlags @dshowSources @dshowLibs @linkArgs
    if ($LASTEXITCODE -ne 0) {
        throw "compilation failed for ps3eye_dshow.dll (exit $LASTEXITCODE)"
    }
}

Push-Location $root
try {
    Build-Target -Name 'ps3eye-probe' -Main 'src\probe_main.c' -ObjDir 'build\probe'
    Build-Target -Name 'ps3eye-grab'  -Main 'src\grab_main.c'  -ObjDir 'build\grab'
    Build-Target -Name 'ps3eye-audio' -Main 'src\audio_main.c' -ObjDir 'build\audio'
    Build-Target -Name 'test_capture_session' -Main 'tests\test_capture_session.c' -ObjDir 'build\tests'
    Build-DShowDll

    # DShow Tests
    $testObj = "build\tests\"
    $testFlags = @('/nologo', '/W4', '/O2', '/std:c++17', '/EHsc', '/D_CRT_SECURE_NO_WARNINGS', '/wd5105', '/I', 'src', '/I', 'src\dshow', "/Fo$testObj")
    $testLibs = @('ole32.lib', 'oleaut32.lib', 'strmiids.lib')

    $filterTestOut = "build\test_dshow_filter.exe"
    Write-Host "building $filterTestOut"
    & cl.exe @testFlags tests\test_dshow_filter.cpp @testLibs "/Fe$filterTestOut"
    if ($LASTEXITCODE -ne 0) {
        throw "compilation failed for test_dshow_filter.exe (exit $LASTEXITCODE)"
    }

    $renderOut = "build\test_dshow_render.exe"
    Write-Host "building $renderOut"
    & cl.exe @testFlags tests\test_dshow_render.cpp @testLibs "/Fe$renderOut"
    if ($LASTEXITCODE -ne 0) {
        throw "compilation failed for test_dshow_render.exe (exit $LASTEXITCODE)"
    }

    $compatTestOut = "build\test_dshow_browser_compat.exe"
    Write-Host "building $compatTestOut"
    & cl.exe @testFlags tests\test_dshow_browser_compat.cpp @testLibs "/Fe$compatTestOut"
    if ($LASTEXITCODE -ne 0) {
        throw "compilation failed for test_dshow_browser_compat.exe (exit $LASTEXITCODE)"
    }

    Write-Host ''
    Write-Host 'artifacts:'
    Get-ChildItem -LiteralPath $build -Filter *.* | Where-Object { $_.Extension -in '.exe', '.dll' } | ForEach-Object {
        Write-Host ("  {0}  ({1:N0} bytes)" -f $_.FullName, $_.Length)
    }
}
finally {
    Pop-Location
}
