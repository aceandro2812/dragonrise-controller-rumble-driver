# Prepare PC for x360ce Force Feedback (DragonRise / Speedlink).
$ErrorActionPreference = "Continue"
$Root = Split-Path -Parent $PSScriptRoot

Write-Host "=== 1) Close apps that hold the pad ===" -ForegroundColor Cyan
foreach ($n in @("x360ce", "steam", "GameOverlayUI")) {
    Get-Process $n -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Seconds 1

Write-Host "=== 2) Re-register FFB COM (32 + 64) ===" -ForegroundColor Cyan
$dll32 = Join-Path $env:SystemRoot "GenericFFBDriver\GenericFFBDriver32.dll"
$dll64 = Join-Path $env:SystemRoot "GenericFFBDriver\GenericFFBDriver64.dll"
if (-not (Test-Path $dll32) -or -not (Test-Path $dll64)) {
    Write-Host "Missing DLLs under SystemRoot\GenericFFBDriver - run Setup.exe first." -ForegroundColor Red
    exit 1
}
Write-Host ("32: {0}  size={1}" -f (Get-Item $dll32).LastWriteTime, (Get-Item $dll32).Length)
Write-Host ("64: {0}  size={1}" -f (Get-Item $dll64).LastWriteTime, (Get-Item $dll64).Length)
& (Join-Path $env:SystemRoot "SysWOW64\regsvr32.exe") /s $dll32
Write-Host "regsvr32 32 exit $LASTEXITCODE"
& (Join-Path $env:SystemRoot "System32\regsvr32.exe") /s $dll64
Write-Host "regsvr32 64 exit $LASTEXITCODE"

Write-Host "=== 3) OEM CLSID check ===" -ForegroundColor Cyan
$oemPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_0079&PID_0006\OEMForceFeedback'
$want = '{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}'
$clsid = $null
try { $clsid = (Get-ItemProperty -LiteralPath $oemPath -ErrorAction Stop).CLSID } catch {}
Write-Host "OEMForceFeedback CLSID = $clsid"
if ($clsid -ne $want) {
    Write-Host "Fixing OEM CLSID..." -ForegroundColor Yellow
    New-Item -LiteralPath $oemPath -Force | Out-Null
    Set-ItemProperty -LiteralPath $oemPath -Name CLSID -Value $want
}

Write-Host "=== 4) DirectInput path test (x360ce-style) ===" -ForegroundColor Cyan
$sim = Join-Path $Root "bin\X360cePathTest.exe"
if (-not (Test-Path $sim) -and (Test-Path (Join-Path $env:TEMP "x360_sim.exe"))) {
    Copy-Item (Join-Path $env:TEMP "x360_sim.exe") $sim -Force
}
if (Test-Path $sim) {
    Write-Host "Running path test - pad should rumble about 3 seconds..." -ForegroundColor Green
    & $sim
    Write-Host "Test exit code: $LASTEXITCODE"
} else {
    $hid = Join-Path $Root "bin\HidRumbleTest.exe"
    if (Test-Path $hid) { & $hid both 1500 0xFE }
}

Write-Host ""
Write-Host "=== 5) x360ce settings for SpeedLink / DragonRise ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Start: C:\Users\jatin\Downloads\x360ce\x360ce.exe"
Write-Host "1. Issues: install ViGEmBus if asked"
Write-Host "2. Controller 1 -> Add -> USB Joystick (real pad, not virtual Xbox)"
Write-Host "3. Enable mapped device"
Write-Host "4. Force Feedback:"
Write-Host "   - Enable Force Feedback = ON"
Write-Host "   - Force Type = Type 2  (required for SpeedLink separate motors)"
Write-Host "   - Overall strength 100"
Write-Host "   - Left/Right motor strength 100"
Write-Host "   - Left/Right direction = Positive (+1), not zero"
Write-Host "   - Swap motors = OFF"
Write-Host "5. Move Test Left / Test Right motor sliders"
Write-Host "Close Steam fully before testing."
Write-Host ""
Write-Host "Done." -ForegroundColor Green
