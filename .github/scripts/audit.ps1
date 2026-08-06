# Release-gate audit for a MoonLit package directory:
#   1. Forbidden-artifact denylist (hooks, injectors, virtual camera, browser).
#   2. Signature status summary of every PE binary.
#   3. SHA-256 checksums written to <dir>/SHA256SUMS.txt.
#   4. License/notice files listed for the SBOM (binaries may add more).
#
# Usage: pwsh -NoProfile -File audit.ps1 [-PackageDir <dir>] [-AllowUnsigned]
#
# -AllowUnsigned: report signature problems without failing (CI builds that
#   produce unsigned artifacts when no signing secret is available).

param(
    [string]$PackageDir = (Join-Path (Get-Location) "build_moonlit_v1_x64\rundir\RelWithDebInfo"),
    [switch]$AllowUnsigned
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $PackageDir)) {
    throw "Package directory not found: $PackageDir"
}

$denylist = @(
    '*graphics-hook*', '*inject*', '*virtualcam*', '*virtual-cam*',
    '*obs-browser*', '*cef*', '*chromium*', '*game-capture*',
    '*obs-websocket*', '*win-capture-graphics*'
)

Write-Host "== 1. Forbidden artifacts =="
$forbidden = Get-ChildItem -LiteralPath $PackageDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
    $name = $_.Name.ToLowerInvariant()
    $denylist | Where-Object { $name -like $_ } | Select-Object -First 1
}
if ($forbidden) {
    foreach ($file in $forbidden) {
        Write-Host "FORBIDDEN: $($file.FullName)"
    }
    Write-Host "audit failed: forbidden artifacts found"
    exit 1
}
Write-Host "ok: no forbidden artifacts"

Write-Host "== 2. Signature status =="
$unsigned = 0
$invalid = 0
$total = 0
Get-ChildItem -LiteralPath $PackageDir -Recurse -File | Where-Object { $_.Extension -in @('.exe', '.dll') } | ForEach-Object {
    $total++
    $signature = Get-AuthenticodeSignature -LiteralPath $_.FullName
    if ($signature.Status -eq 'NotSigned') {
        $unsigned++
        Write-Host "UNSIGNED: $($_.FullName)"
    } elseif ($signature.Status -ne 'Valid') {
        $invalid++
        Write-Host "INVALID ($($signature.Status)): $($_.FullName)"
    }
}
Write-Host "signatures: $total binaries, $unsigned unsigned, $invalid invalid"

Write-Host "== 3. SHA-256 checksums =="
$checksumsFile = Join-Path $PackageDir "SHA256SUMS.txt"
$lines = Get-ChildItem -LiteralPath $PackageDir -Recurse -File | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $relative = $_.FullName.Substring($PackageDir.Length).TrimStart('\').Replace('\', '/')
    "$hash  $relative"
}
Set-Content -LiteralPath $checksumsFile -Value $lines -Encoding ASCII
Write-Host "wrote $checksumsFile ($($lines.Count) files)"

Write-Host "== 4. License and notices =="
Get-ChildItem -LiteralPath $PackageDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
    $_.Name -match 'licen[cs]e|notice|copying|copyr' -or $_.Name -match '\.(txt|md|html?)$'
} | ForEach-Object { Write-Host "  $($_.FullName.Substring($PackageDir.Length))" }

if ($unsigned -gt 0 -or $invalid -gt 0) {
    if ($AllowUnsigned) {
        Write-Host "audit warnings: $unsigned unsigned, $invalid invalid (allowed by -AllowUnsigned)"
    } else {
        Write-Host "audit failed: signature problems found"
        exit 1
    }
}
Write-Host "audit passed"
