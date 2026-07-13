# Generates and runs the real compile/link steps for ffb_validate.exe
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$VcVars,
    [Parameter(Mandatory = $true)][string]$SdkInc,
    [Parameter(Mandatory = $true)][string]$SdkLib,
    [Parameter(Mandatory = $true)][string]$OutExe,
    [Parameter(Mandatory = $true)][string]$ObjDir
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null

$buildCmd = Join-Path $ObjDir "_compile.cmd"
$cflags = "/nologo /O2 /W3 /EHsc /MD /DUNICODE /D_UNICODE /DDIRECTINPUT_VERSION=0x0800 /I`"$SdkInc\um`" /I`"$SdkInc\shared`" /I`"$SdkInc\ucrt`" /I`"$Root\replacement\DragonRiseFFB`""

$lines = @(
    "@echo off",
    "setlocal EnableExtensions",
    "call `"$VcVars`"",
    "if errorlevel 1 exit /b 1",
    "cd /d `"$Root`"",
    "",
    "echo [1/6] ffb_validate.cpp",
    "cl $cflags /c /Fo`"$ObjDir\ffb_validate.obj`" `"validation\ffb_validate.cpp`"",
    "if errorlevel 1 exit /b 1",
    "echo [2/6] effect_map.cpp",
    "cl $cflags /c /Fo`"$ObjDir\effect_map.obj`" `"replacement\DragonRiseFFB\effect_map.cpp`"",
    "if errorlevel 1 exit /b 1",
    "echo [3/6] effect_driver.cpp",
    "cl $cflags /c /Fo`"$ObjDir\effect_driver.obj`" `"replacement\DragonRiseFFB\effect_driver.cpp`"",
    "if errorlevel 1 exit /b 1",
    "echo [4/6] hid_rumble.cpp",
    "cl $cflags /c /Fo`"$ObjDir\hid_rumble.obj`" `"replacement\DragonRiseFFB\hid_rumble.cpp`"",
    "if errorlevel 1 exit /b 1",
    "echo [5/6] ffb_log.cpp",
    "cl $cflags /c /Fo`"$ObjDir\ffb_log.obj`" `"replacement\DragonRiseFFB\ffb_log.cpp`"",
    "if errorlevel 1 exit /b 1",
    "",
    "echo [6/6] link",
    "link /nologo /OUT:`"$OutExe`" `"$ObjDir\ffb_validate.obj`" `"$ObjDir\effect_map.obj`" `"$ObjDir\effect_driver.obj`" `"$ObjDir\hid_rumble.obj`" `"$ObjDir\ffb_log.obj`" /LIBPATH:`"$SdkLib\um\x64`" /LIBPATH:`"$SdkLib\ucrt\x64`" hid.lib setupapi.lib advapi32.lib ole32.lib uuid.lib",
    "if errorlevel 1 exit /b 1",
    "exit /b 0"
)

Set-Content -Path $buildCmd -Value $lines -Encoding ASCII
Write-Host "Helper: $buildCmd"
$p = Start-Process -FilePath "cmd.exe" -ArgumentList @("/c", "`"$buildCmd`"") -Wait -PassThru -NoNewWindow
if ($p.ExitCode -ne 0) {
    Write-Error "compile/link failed with exit $($p.ExitCode)"
    exit $p.ExitCode
}
if (-not (Test-Path $OutExe)) {
    Write-Error "Output missing: $OutExe"
    exit 1
}
Write-Host "OK: $OutExe"
exit 0
