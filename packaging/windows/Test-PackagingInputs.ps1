[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Packaging.Common.ps1")

function Read-RepoJson {
    param([Parameter(Mandatory = $true)][string] $Path, [Parameter(Mandatory = $true)][string] $Label)
    return Read-PackagingJson -Path (Join-Path $PSScriptRoot $Path) -Label $Label
}

$lock = Read-RepoJson -Path "obs-runtime.lock.json" -Label "OBS runtime lock"
$allowlist = Read-RepoJson -Path "runtime.allowlist.json" -Label "runtime allowlist"
$systemDlls = Read-RepoJson -Path "system-dlls.allowlist.json" -Label "system DLL allowlist"
$licenses = Read-RepoJson -Path "licenses.lock.json" -Label "license lock"

foreach ($record in @(
    [pscustomobject]@{ Value = $lock; Label = "OBS runtime lock" },
    [pscustomobject]@{ Value = $allowlist; Label = "runtime allowlist" },
    [pscustomobject]@{ Value = $systemDlls; Label = "system DLL allowlist" },
    [pscustomobject]@{ Value = $licenses; Label = "license lock" }
)) {
    if ($record.Value.schemaVersion -ne 1) {
        throw "$($record.Label) has an unsupported schemaVersion."
    }
    if ([string]$record.Value.status -notin @("design-only", "approved")) {
        throw "$($record.Label) has an unknown status: $($record.Value.status)"
    }
    if ([string]$record.Value.status -ceq "approved") {
        Assert-ApprovedPackagingLock -Lock $record.Value -Label $record.Label
    }
}

if ([string]$lock.target -cne "x86_64-pc-windows-msvc") {
    throw "OBS runtime lock target must be x86_64-pc-windows-msvc."
}
foreach ($url in @($lock.obsStudio.sourceUrl, $lock.obsStudio.portableReferenceUrl, $lock.obsDependencies.url)) {
    if ([string]$url -notmatch '^https://[^\s]+$' -or [string]$url -match '(?i)(^|/)latest(/|$)') {
        throw "Runtime lock contains a mutable or non-HTTPS URL: $url"
    }
}
foreach ($hash in @($lock.obsStudio.sourceSha256, $lock.obsStudio.portableReferenceSha256, $lock.obsDependencies.sha256)) {
    Assert-PackagingHash -Hash ([string]$hash) -Label "runtime lock hash"
}

$allowPatterns = @($allowlist.files | ForEach-Object { [string]$_ })
$requiredFiles = @($allowlist.requiredFiles | ForEach-Object { [string]$_ })
if ($allowPatterns.Count -eq 0 -or $requiredFiles.Count -eq 0) {
    throw "Runtime allowlist must contain files and requiredFiles."
}
$seenPatterns = @{}
foreach ($pattern in $allowPatterns) {
    Assert-PackagingRelativePattern -Pattern $pattern -Label "runtime allowlist"
    $key = $pattern.ToLowerInvariant()
    if ($seenPatterns.ContainsKey($key)) {
        throw "Runtime allowlist contains a duplicate pattern: $pattern"
    }
    $seenPatterns[$key] = $true
}
foreach ($required in $requiredFiles) {
    Assert-PackagingRelativePattern -Pattern $required -Label "runtime required file"
    if (-not (Test-PackagingAllowlistedPath -RelativePath $required -Patterns $allowPatterns)) {
        throw "Required runtime file is not covered by the allowlist: $required"
    }
}
foreach ($denyExtension in @($allowlist.denyExtensions)) {
    if ([string]$denyExtension -notmatch '^\.[A-Za-z0-9]+$') {
        throw "Runtime deny extension is malformed: $denyExtension"
    }
}
foreach ($driver in @($systemDlls.driverProvided)) {
    if ([string]$driver -notmatch '^[A-Za-z0-9*._-]+\.dll$') {
        throw "Driver-provided import name is malformed: $driver"
    }
}

$expectedSource = "../target/package-stage/windows-x86_64/runtime/obs/"
$expectedWorkflowSource = "target/package-stage/windows-x86_64/runtime/obs/"
foreach ($configName in @("..\..\src-tauri\tauri.windows.release.conf.json", "..\..\src-tauri\tauri.windows.offline.conf.json")) {
    $config = Read-RepoJson -Path $configName -Label $configName
    $resource = @($config.bundle.resources.PSObject.Properties | Where-Object { $_.Name -eq $expectedSource })
    if ($resource.Count -ne 1 -or [string]$resource[0].Value -cne "runtime/obs/") {
        throw "$configName does not map the canonical runtime staging path."
    }
}

$releaseWorkflowPath = Join-Path $PSScriptRoot "..\..\.github\workflows\release.yml"
$releaseWorkflow = Get-Content -LiteralPath $releaseWorkflowPath -Raw
if ($releaseWorkflow -notmatch [regex]::Escape($expectedWorkflowSource) -or
    $releaseWorkflow -notmatch '-OutputRoot\s+\$stageRoot' -or
    $releaseWorkflow -match 'if:\s*\$\{\{\s*secrets\.WINDOWS_CERTIFICATE_THUMBPRINT') {
    throw "Release workflow does not enforce the canonical stage path and mandatory signing."
}

Write-Output "Packaging policy inputs and canonical staging paths are structurally valid; release status remains governed by the design-only locks."
