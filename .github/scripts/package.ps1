# Builds the MoonLit release artifacts from the dev rundir:
#   1. Clean staging copy without PDBs or checksum files.
#   2. Sign the staging binaries (unless -SkipSign).
#   3. Release-gate audit (denylist, signatures, SHA-256 checksums).
#   4. Portable ZIP from the signed staging.
#   5. NSIS installer, then signed.
#   6. SHA-256 of the artifacts.
#
# Usage: pwsh -NoProfile -File package.ps1 [-Version 0.1.1] [-Rundir <dir>] [-OutDir <dir>]
#        [-SkipSign] [-CertPath <pfx>] [-PasswordFile <txt>]
#
# -SkipSign: produce unsigned artifacts (CI without a signing secret). The
#   audit then reports signatures without failing on them.
# -CertPath/-PasswordFile: sign with a specific certificate. Defaults to the
#   local development PFX under .deps/certs/ (gitignored).

param(
    [string]$Version = "0.1.1",
    [string]$Rundir = (Join-Path (Get-Location) "build_moonlit_v1_x64\rundir\RelWithDebInfo"),
    [string]$OutDir = (Join-Path (Get-Location) "build_moonlit_v1_x64\package"),
    [switch]$SkipSign,
    [string]$CertPath = ".deps\certs\moonlit-dev.pfx",
    [string]$PasswordFile = ".deps\certs\moonlit-dev.pfx.txt"
)

$ErrorActionPreference = 'Stop'
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")

if (-not (Test-Path -LiteralPath $Rundir)) {
    throw "Rundir not found: $Rundir"
}

# Absolute paths: the portable ZIP is created from a staging subdirectory,
# so a relative OutDir would resolve against the wrong location.
$Rundir = [System.IO.Path]::GetFullPath($Rundir)
$OutDir = [System.IO.Path]::GetFullPath($OutDir)

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
# Make sure the clip feedback sound is always in the package even when the
# rundir predates it.
$soundSource = Join-Path $repoRoot "frontend\data\obs-studio\sounds\moonlit-clip.wav"
if (Test-Path -LiteralPath $soundSource) {
    New-Item -ItemType Directory -Path (Join-Path $staging "data\obs-studio\sounds") -Force | Out-Null
    Copy-Item -LiteralPath $soundSource -Destination (Join-Path $staging "data\obs-studio\sounds\moonlit-clip.wav") -Force
}
Write-Host "staged: $((Get-ChildItem -LiteralPath $staging -Recurse -File | Measure-Object).Count) files"

if (-not $SkipSign) {
    Write-Host "== sign binaries =="
    if (-not (Test-Path -LiteralPath $CertPath)) {
        throw "Certificate not found: $CertPath (use -SkipSign or pass -CertPath)"
    }
    $LASTEXITCODE = 0
    & (Join-Path $PSScriptRoot "sign.ps1") -StagingDir $staging -CertPath $CertPath -PasswordFile $PasswordFile
    if ($LASTEXITCODE -ne 0) {
        throw "signing failed with exit code $LASTEXITCODE"
    }
}

Write-Host "== release-gate audit =="
$auditArgs = @{ PackageDir = $staging }
if ($SkipSign) {
    $auditArgs += @{ AllowUnsigned = $true }
}
$LASTEXITCODE = 0
& (Join-Path $PSScriptRoot "audit.ps1") @auditArgs
if ($LASTEXITCODE -ne 0) {
    throw "audit failed with exit code $LASTEXITCODE"
}

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
if ($SkipSign) {
    Write-Host "skipped (unsigned artifacts)"
} else {
    $LASTEXITCODE = 0
    & (Join-Path $PSScriptRoot "sign.ps1") -StagingDir $OutDir -CertPath $CertPath -PasswordFile $PasswordFile
    if ($LASTEXITCODE -ne 0) {
        throw "installer signing failed with exit code $LASTEXITCODE"
    }
    $LASTEXITCODE = 0
    & (Join-Path $PSScriptRoot "verify.ps1") -StagingDir $OutDir
    if ($LASTEXITCODE -ne 0) {
        throw "signature verification failed with exit code $LASTEXITCODE"
    }
}

Write-Host "== checksums =="
$lines = @(
    "$((Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant())  $(Split-Path $zipPath -Leaf)",
    "$((Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant())  $(Split-Path $installerPath -Leaf)"
)
Set-Content -LiteralPath $shaFile -Value $lines -Encoding ASCII
$lines
Write-Host "artifacts ready in $OutDir"
