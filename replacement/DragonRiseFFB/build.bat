@echo off
setlocal EnableExtensions

REM Build drop-in GenericFFBDriver32/64.dll
REM   build.bat           -> release x64+x86
REM   build.bat debug     -> debug-log enabled x64+x86
REM   build.bat x64
REM   build.bat x86

set "ROOT=%~dp0"
set "OUTDIR=%ROOT%..\..\bin"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set "VCVARS="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
  set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
  set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
)

if not defined VCVARS (
  echo ERROR: vcvarsall.bat not found.
  exit /b 1
)

set "SDKVER=10.0.26100.0"
set "SDKINC=%ProgramFiles(x86)%\Windows Kits\10\Include\%SDKVER%"
set "SDKLIB=%ProgramFiles(x86)%\Windows Kits\10\Lib\%SDKVER%"
set "DEBUGDEF="
set "SRCS=dllmain.cpp effect_driver.cpp hid_rumble.cpp effect_map.cpp ffb_log.cpp"

set "ARG1=%~1"
if /I "%ARG1%"=="debug" (
  set "DEBUGDEF=/DDRFFB_DEBUG=1"
  set "ARG1=%~2"
)
if "%ARG1%"=="" set "ARG1=all"

if /I "%ARG1%"=="all" (
  call :build_one x64
  if errorlevel 1 exit /b 1
  call :build_one x86
  if errorlevel 1 exit /b 1
  echo.
  echo Built:
  dir /b "%OUTDIR%\GenericFFBDriver*.dll"
  exit /b 0
)
if /I "%ARG1%"=="x64" (
  call :build_one x64
  exit /b %ERRORLEVEL%
)
if /I "%ARG1%"=="x86" (
  call :build_one x86
  exit /b %ERRORLEVEL%
)
echo Usage: build.bat [debug] [x64^|x86^|all]
exit /b 1

:build_one
set "TARGET=%~1"
echo.
echo ===== Building %TARGET% %DEBUGDEF% =====
if /I "%TARGET%"=="x64" (
  set "VCTARGET=x64"
  set "OUTDLL=%OUTDIR%\GenericFFBDriver64.dll"
  set "LIBUM=%SDKLIB%\um\x64"
  set "LIBUCRT=%SDKLIB%\ucrt\x64"
) else (
  set "VCTARGET=x86"
  set "OUTDLL=%OUTDIR%\GenericFFBDriver32.dll"
  set "LIBUM=%SDKLIB%\um\x86"
  set "LIBUCRT=%SDKLIB%\ucrt\x86"
)

set "TMPBAT=%TEMP%\drffb_build_%TARGET%.cmd"
(
  echo @echo off
  echo call "%VCVARS%" %VCTARGET%
  echo if errorlevel 1 exit /b 1
  echo cd /d "%ROOT%"
  echo cl /nologo /O2 /W3 /EHsc /LD /MD /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DDIRECTINPUT_VERSION=0x0800 %DEBUGDEF% /I"%SDKINC%\um" /I"%SDKINC%\shared" /I"%SDKINC%\ucrt" %SRCS% /Fe"%OUTDLL%" /link /DEF:"DragonRiseFFB.def" /LIBPATH:"%LIBUM%" /LIBPATH:"%LIBUCRT%" hid.lib setupapi.lib advapi32.lib ole32.lib uuid.lib kernel32.lib user32.lib msi.lib
  echo exit /b %%ERRORLEVEL%%
) > "%TMPBAT%"
cmd /c "%TMPBAT%"
set "ERR=%ERRORLEVEL%"
del "%TMPBAT%" 2>nul
if not "%ERR%"=="0" (
  echo BUILD FAILED for %TARGET%
  exit /b %ERR%
)
echo OK: %OUTDLL%
exit /b 0
