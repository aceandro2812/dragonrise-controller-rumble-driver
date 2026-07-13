@echo off
setlocal EnableExtensions

REM Build and run bin\ffb_validate.exe
REM Uses a generated helper script so paths with "Program Files (x86)" are safe.
REM Compiles each translation unit separately, then links (avoids cl D8018).

pushd "%~dp0.."
set "ROOT=%CD%"
popd

set "OUTDIR=%ROOT%\bin"
set "OBJDIR=%ROOT%\validation\obj"
set "SRC=%ROOT%\replacement\DragonRiseFFB"
set "OUT=%OUTDIR%\ffb_validate.exe"
set "GEN=%ROOT%\validation\_gen_build.ps1"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

set "VCVARS="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS (
  echo ERROR: vcvars64.bat not found
  exit /b 1
)

set "SDKVER=10.0.26100.0"
set "SDKINC=%ProgramFiles(x86)%\Windows Kits\10\Include\%SDKVER%"
set "SDKLIB=%ProgramFiles(x86)%\Windows Kits\10\Lib\%SDKVER%"
if not exist "%SDKINC%\um\dinput.h" (
  echo ERROR: SDK headers not found
  exit /b 1
)

echo Using: %VCVARS%
echo Root:  %ROOT%

powershell -NoProfile -ExecutionPolicy Bypass -File "%GEN%" ^
  -Root "%ROOT%" ^
  -VcVars "%VCVARS%" ^
  -SdkInc "%SDKINC%" ^
  -SdkLib "%SDKLIB%" ^
  -OutExe "%OUT%" ^
  -ObjDir "%OBJDIR%"
if errorlevel 1 (
  echo.
  echo VALIDATE BUILD FAILED
  exit /b 1
)

echo.
echo Built: %OUT%
echo Running...
echo.
"%OUT%"
exit /b %ERRORLEVEL%
