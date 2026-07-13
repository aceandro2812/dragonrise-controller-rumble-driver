# Build drop-in GenericFFBDriver DLLs, patch MSI, assemble installable package.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

Write-Host "=== 0/4 Build MSI stream helper ===" -ForegroundColor Cyan
$vcvars = $null
foreach ($c in @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)) { if (Test-Path $c) { $vcvars = $c; break } }
if (-not $vcvars) { throw "vcvars64.bat not found" }
$helperCmd = @"
@echo off
call "$vcvars"
cd /d "$Root\tools"
cl /nologo /O2 /EHsc msi_replace_stream.cpp /Fe:msi_replace_stream.exe ole32.lib
"@
$tmp = Join-Path $env:TEMP "build_msi_helper.cmd"
Set-Content -Path $tmp -Value $helperCmd -Encoding ascii
& cmd /c $tmp
if ($LASTEXITCODE -ne 0) { throw "msi_replace_stream build failed" }

Write-Host "=== 1/4 Build FFB DLLs ===" -ForegroundColor Cyan
& cmd /c "replacement\DragonRiseFFB\build.bat all"
if ($LASTEXITCODE -ne 0) { throw "DLL build failed" }

$dll32 = Join-Path $Root "bin\GenericFFBDriver32.dll"
$dll64 = Join-Path $Root "bin\GenericFFBDriver64.dll"
if (-not (Test-Path $dll32) -or -not (Test-Path $dll64)) {
    throw "Missing built DLLs in bin\"
}

Write-Host "=== 2/4 Locate original MSI ===" -ForegroundColor Cyan
$origMsi = $null
foreach ($c in @(
    (Join-Path $Root "analysis\GenericUSBGamepadVibration.msi"),
    "C:\WINDOWS\Installer\27d46.msi"
)) {
    if (Test-Path $c) { $origMsi = $c; break }
}
if (-not $origMsi) {
    throw "Original MSI not found. Copy Generic USB Gamepad Vibration Driver MSI to analysis\GenericUSBGamepadVibration.msi"
}
Write-Host "Using MSI: $origMsi"

Write-Host "=== 3/4 Patch MSI cabinet ===" -ForegroundColor Cyan
$outMsi = Join-Path $Root "dist\patched\GenericUSBGamepadVibration.msi"
New-Item -ItemType Directory -Force -Path (Split-Path $outMsi) | Out-Null
python (Join-Path $Root "tools\patch_ffb_msi.py") `
    --original-msi $origMsi `
    --dll32 $dll32 `
    --dll64 $dll64 `
    --out-msi $outMsi
if ($LASTEXITCODE -ne 0) { throw "MSI patch failed" }

Write-Host "=== 4/4 Build Setup.exe + package tree ===" -ForegroundColor Cyan
$csc = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) { $csc = "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe" }
$pkg = Join-Path $Root "dist\SPEED-LINK-SL-6535-BK_Driver_V1.0_FFB-Patched"
if (Test-Path $pkg) { Remove-Item $pkg -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "FFB") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "Driver") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "Simplicheck") | Out-Null

$setupOut = Join-Path $pkg "Setup.exe"
$manifest = Join-Path $Root "tools\app.manifest"
$setupSrc = Join-Path $Root "tools\SetupBootstrap.cs"
if (-not (Test-Path $manifest)) { throw "Missing manifest: $manifest" }
if (-not (Test-Path $setupSrc)) { throw "Missing Setup source: $setupSrc" }

# Pass args as an array so PowerShell does not eat /win32manifest:path
$cscArgs = @(
    "/nologo",
    "/optimize+",
    "/target:winexe",
    "/platform:anycpu",
    "/win32manifest:$manifest",
    "/reference:System.Windows.Forms.dll",
    "/reference:System.Drawing.dll",
    "/out:$setupOut",
    $setupSrc
)
Write-Host "csc $($cscArgs -join ' ')"
& $csc @cscArgs
if ($LASTEXITCODE -ne 0) { throw "Setup.exe build failed" }
if (-not (Test-Path $setupOut)) { throw "Setup.exe was not produced at $setupOut" }

Copy-Item $outMsi (Join-Path $pkg "FFB\GenericUSBGamepadVibration.msi") -Force
if (Test-Path (Join-Path $Root "Driver\Setup.exe")) {
    Copy-Item (Join-Path $Root "Driver\Setup.exe") (Join-Path $pkg "Driver\Setup.exe") -Force
}
if (Test-Path (Join-Path $Root "Simplicheck\Simplicheck.exe")) {
    Copy-Item (Join-Path $Root "Simplicheck\Simplicheck.exe") (Join-Path $pkg "Simplicheck\Simplicheck.exe") -Force
}
# Keep original outer Setup as reference only
if (Test-Path (Join-Path $Root "Setup.exe")) {
    Copy-Item (Join-Path $Root "Setup.exe") (Join-Path $pkg "Setup.original.exe") -Force
}

Copy-Item (Join-Path $Root "docs\PATCHED_INSTALLER.md") (Join-Path $pkg "README.txt") -ErrorAction SilentlyContinue
if (-not (Test-Path (Join-Path $pkg "README.txt"))) {
    Copy-Item (Join-Path $Root "docs\PATCHED_INSTALLER.md") (Join-Path $pkg "README.txt") -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "Package ready:" -ForegroundColor Green
Write-Host "  $pkg"
Get-ChildItem $pkg -Recurse | Select-Object FullName, Length | Format-Table -AutoSize
