# Verifies Authenticode signatures on all PE binaries under a directory.
# Reports any binary that is unsigned or whose signature does not validate.
#
# Usage: pwsh -NoProfile -File verify.ps1 [-StagingDir <dir>]

param(
    [string]$StagingDir = (Join-Path (Get-Location) "build_moonlit_v1_x64\rundir\RelWithDebInfo")
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $StagingDir)) {
    throw "Staging directory not found: $StagingDir"
}

$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) {
    throw "signtool.exe not found (Windows SDK required)"
}

$files = Get-ChildItem -LiteralPath $StagingDir -Recurse -File | Where-Object {
    $_.Extension -in @('.exe', '.dll')
}

$failures = @()
foreach ($file in $files) {
    & $signtool.FullName verify /pa /v $file.FullName 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $failures += $file.FullName
        Write-Host "INVALID: $($file.FullName)"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "verify failed for $($failures.Count) of $($files.Count) binaries"
    exit 1
}
Write-Host "verify ok: $($files.Count) binaries"
