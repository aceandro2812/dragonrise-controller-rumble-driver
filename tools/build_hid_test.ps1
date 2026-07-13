$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $root) { $root = (Resolve-Path "$PSScriptRoot\..").Path }
$src = Join-Path $PSScriptRoot "HidRumbleTest.cs"
$outDir = Join-Path $root "bin"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$out = Join-Path $outDir "HidRumbleTest.exe"

$cscCandidates = @(
    "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe",
    "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe"
)
$csc = $cscCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $csc) { throw "csc.exe not found" }

& $csc /nologo /optimize+ /target:exe /platform:anycpu /out:$out $src
if ($LASTEXITCODE -ne 0) { throw "csc failed" }
Write-Host "Built: $out"
Write-Host "Try: $out list"
Write-Host "     $out sequence   # after plugging in the pad"
