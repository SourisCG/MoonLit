# Builds the MoonLit release artifacts from the dev rundir:
#   1. Clean staging copy without PDBs or checksum files.
#   2. Release-gate audit (denylist, signatures, SHA-256 checksums).
#   3. Portable ZIP.
#   4. NSIS installer, signed.
#   5. SHA-256 of the artifacts.
#
# Usage: pwsh -NoProfile -File package.ps1 [-Version 1.0.0] [-Rundir <dir>] [-OutDir <dir>]

param(
    [string]$Version = "1.0.0",
    [string]$Rundir = (Join-Path (Get-Location) "build_moonlit_v1_x64\rundir\RelWithDebInfo"),
    [string]$OutDir = (Join-Path (Get-Location) "build_moonlit_v1_x64\package")
)

$ErrorActionPreference = 'Stop'
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")

if (-not (Test-Path -LiteralPath $Rundir)) {
    throw "Rundir not found: $Rundir"
}

$staging = Join-Path $OutDir "staging"
$zipPath = Join-Path $OutDir "MoonLit-$Version-x64.zip"
$installerPath = Join-Path $OutDir "MoonLit-$Version-Setup.exe"
$shaFile = Join-Path $OutDir "SHA256SUMS.txt"

if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
New-Item -ItemType Directory -Path $staging -Force | Out-Null
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Write-Host "== staging copy =="
Copy-Item -Path (Join-Path $Rundir "*") -Destination $staging -Recurse -Force
Get-ChildItem -LiteralPath $staging -Recurse -File | Where-Object {
    $_.Extension -in @('.pdb') -or $_.Name -eq 'SHA256SUMS.txt'
} | Remove-Item -Force
Write-Host "staged: $((Get-ChildItem -LiteralPath $staging -Recurse -File | Measure-Object).Count) files"

Write-Host "== release-gate audit =="
& (Join-Path $PSScriptRoot "audit.ps1") -PackageDir $staging

Write-Host "== portable ZIP =="
$zipStaging = Join-Path $OutDir "zip-staging"
if (Test-Path -LiteralPath $zipStaging) {
    Remove-Item -LiteralPath $zipStaging -Recurse -Force
}
New-Item -ItemType Directory -Path $zipStaging -Force | Out-Null
Copy-Item -Path (Join-Path $staging "*") -Destination $zipStaging -Recurse -Force
# The portable marker at the app root makes OBS and MoonLit keep everything
# next to the extracted ZIP (BASE_PATH is two levels above bin/64bit).
New-Item -ItemType File -Path (Join-Path $zipStaging "portable_mode") -Force | Out-Null
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Push-Location $zipStaging
try {
    tar -a -c -f $zipPath .
    if ($LASTEXITCODE -ne 0) {
        throw "tar failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}
Remove-Item -LiteralPath $zipStaging -Recurse -Force
Write-Host "zip: $([math]::Round((Get-Item $zipPath).Length / 1MB, 1)) MB"

Write-Host "== NSIS installer =="
$makensis = Get-ChildItem "C:\Program Files (x86)\NSIS","$env:LOCALAPPDATA\Programs\NSIS" -Recurse -Filter "makensis.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $makensis) {
    throw "makensis.exe not found (NSIS 3 required)"
}
$nsi = Join-Path $repoRoot "cmake\windows\moonlit.nsi"
$generatedInstaller = Join-Path (Split-Path $nsi) "MoonLit-$Version-Setup.exe"
if (Test-Path -LiteralPath $generatedInstaller) {
    Remove-Item -LiteralPath $generatedInstaller -Force
}
Push-Location (Split-Path $nsi)
try {
    & $makensis /DVERSION=$Version /DSTAGING=$staging $nsi
    if ($LASTEXITCODE -ne 0) {
        throw "makensis failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}
if (-not (Test-Path -LiteralPath $generatedInstaller)) {
    throw "installer was not produced by makensis"
}
Move-Item -LiteralPath $generatedInstaller -Destination $installerPath -Force
Write-Host "installer: $([math]::Round((Get-Item $installerPath).Length / 1MB, 1)) MB"

Write-Host "== sign installer =="
& (Join-Path $PSScriptRoot "sign.ps1") -StagingDir $OutDir -CertPath (Join-Path $repoRoot ".deps\certs\moonlit-dev.pfx") -PasswordFile (Join-Path $repoRoot ".deps\certs\moonlit-dev.pfx.txt")
& (Join-Path $PSScriptRoot "verify.ps1") -StagingDir $OutDir

Write-Host "== checksums =="
$lines = @(
    "$((Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant())  $(Split-Path $zipPath -Leaf)",
    "$((Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant())  $(Split-Path $installerPath -Leaf)"
)
Set-Content -LiteralPath $shaFile -Value $lines -Encoding ASCII
$lines
Write-Host "artifacts ready in $OutDir"
