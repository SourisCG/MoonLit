# Signs all unsigned PE binaries (exe/dll) under a directory with the
# MoonLit development certificate (self-signed, SHA-256 + RFC 3161 timestamp).
# Binaries that already carry a valid signature are left untouched.
#
# Usage: pwsh -NoProfile -File sign.ps1 [-StagingDir <dir>] [-CertPath <pfx>] [-PasswordFile <txt>]
# Defaults read the development PFX from .deps/certs/ (gitignored).

param(
    [string]$StagingDir = (Join-Path (Get-Location) "build_moonlit_v1_x64\rundir\RelWithDebInfo"),
    [string]$CertPath = ".deps\certs\moonlit-dev.pfx",
    [string]$PasswordFile = ".deps\certs\moonlit-dev.pfx.txt"
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $CertPath)) {
    throw "Certificate not found: $CertPath"
}
if (-not (Test-Path -LiteralPath $StagingDir)) {
    throw "Staging directory not found: $StagingDir"
}
if (-not (Test-Path -LiteralPath $PasswordFile)) {
    throw "Password file not found: $PasswordFile"
}

$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) {
    throw "signtool.exe not found (Windows SDK required)"
}

$password = (Get-Content -LiteralPath $PasswordFile -Raw).Trim()
$binaryPatterns = @('*.exe', '*.dll')
$files = Get-ChildItem -LiteralPath $StagingDir -Recurse -File | Where-Object {
    $name = $_.Name.ToLowerInvariant()
    $binaryPatterns | Where-Object { $name -like $_ } | Select-Object -First 1
}

$signed = 0
$skipped = 0
foreach ($file in $files) {
    $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
    if ($signature.Status -ne 'NotSigned') {
        $skipped++
        continue
    }
    Write-Host "signing $($file.FullName)"
    & $signtool.FullName sign /f $CertPath /p $password /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /v $file.FullName | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "signtool failed for $($file.FullName)"
    }
    $signed++
}

Write-Host "done: $signed signed, $skipped already signed"
