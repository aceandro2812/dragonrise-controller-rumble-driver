# Register DragonRiseFFB COM server and optionally retarget OEMForceFeedback CLSID.
# Requires elevation (Administrator).
#
# Usage:
#   .\register.ps1                 # register COM only (x64+x86 if present)
#   .\register.ps1 -ActivateOem    # also point VID_0079&PID_0006 OEMForceFeedback at new CLSID
#   .\register.ps1 -RestoreOem     # restore stock GenericFFB CLSID
#   .\register.ps1 -Unregister     # unregister COM + restore OEM if we changed it

param(
    [switch]$ActivateOem,
    [switch]$RestoreOem,
    [switch]$Unregister
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bin = Join-Path $root "bin"
$dll64 = Join-Path $bin "DragonRiseFFB64.dll"
$dll32 = Join-Path $bin "DragonRiseFFB32.dll"

$NewClsid = "{B1D0F8A2-3C4E-4F61-9A7B-2E5C8D1F0A94}"
$StockClsid = "{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}"
$OemKey = "HKLM:\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_0079&PID_0006\OEMForceFeedback"

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "Run this script from an elevated PowerShell (Administrator)."
    }
}

function Register-Dll([string]$path) {
    if (-not (Test-Path $path)) {
        Write-Warning "Missing: $path"
        return
    }
    Write-Host "regsvr32 /s $path"
    $p = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList @("/s", $path) -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "regsvr32 failed for $path (exit $($p.ExitCode))" }
}

function Unregister-Dll([string]$path) {
    if (-not (Test-Path $path)) { return }
    Write-Host "regsvr32 /u /s $path"
    Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList @("/u", "/s", $path) -Wait | Out-Null
}

Assert-Admin

if ($Unregister) {
    Unregister-Dll $dll64
    Unregister-Dll $dll32
    if (Test-Path $OemKey) {
        $cur = (Get-ItemProperty $OemKey -Name CLSID -ErrorAction SilentlyContinue).CLSID
        if ($cur -eq $NewClsid) {
            Set-ItemProperty -Path $OemKey -Name CLSID -Value $StockClsid
            Write-Host "Restored OEMForceFeedback CLSID to stock $StockClsid"
        }
    }
    Write-Host "Unregistered."
    exit 0
}

if ($RestoreOem) {
    if (-not (Test-Path $OemKey)) { throw "OEM key missing: $OemKey" }
    Set-ItemProperty -Path $OemKey -Name CLSID -Value $StockClsid
    Write-Host "OEMForceFeedback CLSID restored to $StockClsid"
    exit 0
}

# Prefer architecture-correct regsvr32 for each DLL.
if (Test-Path $dll64) {
    Write-Host "Registering x64..."
    $p = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList @("/s", $dll64) -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "regsvr32 x64 failed ($($p.ExitCode))" }
} else {
    Write-Warning "No x64 DLL at $dll64 — run build.bat first"
}

if (Test-Path $dll32) {
    Write-Host "Registering x86..."
    $reg32 = "$env:SystemRoot\SysWOW64\regsvr32.exe"
    if (-not (Test-Path $reg32)) { $reg32 = "$env:SystemRoot\System32\regsvr32.exe" }
    $p = Start-Process -FilePath $reg32 -ArgumentList @("/s", $dll32) -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "regsvr32 x86 failed ($($p.ExitCode))" }
} else {
    Write-Warning "No x86 DLL at $dll32"
}

if ($ActivateOem) {
    if (-not (Test-Path $OemKey)) {
        throw "OEM key missing (is the Speedlink OEM registration installed?): $OemKey"
    }
    $prev = (Get-ItemProperty $OemKey -Name CLSID).CLSID
    Set-ItemProperty -Path $OemKey -Name CLSID -Value $NewClsid
    Write-Host "OEMForceFeedback CLSID: $prev -> $NewClsid"
    Write-Host "Backup of previous value is also in registry_backup\ if exported earlier."
}

Write-Host ""
Write-Host "COM registration done."
Write-Host "  CLSID: $NewClsid"
Write-Host "  ProgID: DragonRiseFFB.FFBDriver"
if (-not $ActivateOem) {
    Write-Host ""
    Write-Host "OEM still points at stock GenericFFB. To activate replacement:"
    Write-Host "  .\register.ps1 -ActivateOem"
}
