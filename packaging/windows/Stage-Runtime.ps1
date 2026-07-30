[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $RuntimeInput,

    [Parameter(Mandatory = $true)]
    [string] $RecorderBinary,

    [Parameter(Mandatory = $true)]
    [string] $OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$lockPath = Join-Path $PSScriptRoot "obs-runtime.lock.json"
$allowlistPath = Join-Path $PSScriptRoot "runtime.allowlist.json"
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$allowlist = Get-Content -LiteralPath $allowlistPath -Raw | ConvertFrom-Json

if ($lock.status -ne "approved" -or $allowlist.status -ne "approved") {
    throw "The OBS runtime lock and allowlist must be approved before staging."
}

$inputRoot = (Resolve-Path -LiteralPath $RuntimeInput).Path
$recorderPath = (Resolve-Path -LiteralPath $RecorderBinary).Path
$outputParent = Split-Path -Parent $OutputRoot
if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
    throw "The output parent directory does not exist: $outputParent"
}
if (-not (Test-Path -LiteralPath $OutputRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
}
$stageRoot = Join-Path (Resolve-Path -LiteralPath $OutputRoot).Path "runtime/obs"

if (-not (Test-Path -LiteralPath $stageRoot)) {
    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
}
Get-ChildItem -LiteralPath $stageRoot -Force | Remove-Item -Recurse -Force

function Test-DeniedPath {
    param([Parameter(Mandatory = $true)][string] $RelativePath)

    $lower = $RelativePath.ToLowerInvariant().Replace("\", "/")
    foreach ($name in $allowlist.denyNames) {
        $pattern = $name.ToLowerInvariant().Replace("\", "/")
        if ($lower -like "*${pattern}*") {
            return $true
        }
    }
    foreach ($extension in $allowlist.denyExtensions) {
        if ($lower.EndsWith($extension.ToLowerInvariant())) {
            return $true
        }
    }
    return $false
}

function Test-AllowlistedPath {
    param([Parameter(Mandatory = $true)][string] $RelativePath)

    foreach ($pattern in $allowlist.files) {
        if ($RelativePath -like $pattern) {
            return $true
        }
    }
    return $false
}

foreach ($relativePath in $allowlist.files) {
    if ($relativePath.Contains("**")) {
        $matches = Get-ChildItem -LiteralPath $inputRoot -Recurse -File |
            Where-Object {
                $relative = $_.FullName.Substring($inputRoot.Length + 1).Replace("\", "/")
                $relative -like $relativePath
            }
    } else {
        $candidate = Join-Path $inputRoot $relativePath
        $matches = if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            @(Get-Item -LiteralPath $candidate)
        } else {
            @()
        }
    }

    foreach ($file in $matches) {
        $relative = $file.FullName.Substring($inputRoot.Length + 1).Replace("\", "/")
        if (Test-DeniedPath $relative -or -not (Test-AllowlistedPath $relative)) {
            throw "Runtime file is not allowed: $relative"
        }
        $destination = Join-Path $stageRoot $relative
        $destinationDirectory = Split-Path -Parent $destination
        if (-not (Test-Path -LiteralPath $destinationDirectory)) {
            New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        }
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
}

$recorderDestination = Join-Path $stageRoot "bin/64bit/moonlit-recorder.exe"
$recorderDirectory = Split-Path -Parent $recorderDestination
if (-not (Test-Path -LiteralPath $recorderDirectory)) {
    New-Item -ItemType Directory -Path $recorderDirectory -Force | Out-Null
}
Copy-Item -LiteralPath $recorderPath -Destination $recorderDestination -Force

$stagedFiles = Get-ChildItem -LiteralPath $stageRoot -Recurse -File
foreach ($file in $stagedFiles) {
    $relative = $file.FullName.Substring($stageRoot.Length + 1).Replace("\", "/")
    if (Test-DeniedPath $relative -or -not (Test-AllowlistedPath $relative)) {
        throw "Unexpected file in staged runtime: $relative"
    }
}

Write-Output "Staged $($stagedFiles.Count) runtime files under $stageRoot"
