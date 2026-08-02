[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $StageRoot,
    [Parameter(Mandatory = $true)][string] $OutputFile,
    [Parameter(Mandatory = $true)][string] $SourceSha,
    [Parameter(Mandatory = $true)][ValidateSet("clean")][string] $WorktreeStatus,
    [Parameter(Mandatory = $true)][string] $ProductVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Packaging.Common.ps1")

Assert-PackagingSourceSha -SourceSha $SourceSha
Assert-PackagingVersion -Version $ProductVersion
$repositoryRoot = [IO.Directory]::GetParent([IO.Directory]::GetParent($PSScriptRoot).FullName).FullName
Assert-PackagingSourceContext -RepositoryRoot $repositoryRoot -SourceSha $SourceSha -WorktreeStatus $WorktreeStatus
$stage = Get-PackagingFullPath -Path $StageRoot -Label "StageRoot" -Kind Directory -MustExist
$output = Get-PackagingFullPath -Path $OutputFile -Label "OutputFile"
Assert-PackagingSafePath -Path $stage -Label "StageRoot"
Assert-PackagingSafePath -Path $output -Label "OutputFile"
if (-not (Test-PackagingPathWithin -Path $output -Root $stage)) {
    throw "Runtime manifest must be inside StageRoot so Tauri bundles the exact manifest."
}
if (Test-Path -LiteralPath $output) {
    throw "Refusing to overwrite an existing runtime manifest: $output"
}

$lockPaths = @(
    (Join-Path $PSScriptRoot "obs-runtime.lock.json"),
    (Join-Path $PSScriptRoot "runtime.allowlist.json"),
    (Join-Path $PSScriptRoot "system-dlls.allowlist.json"),
    (Join-Path $PSScriptRoot "licenses.lock.json")
)
$locks = @()
foreach ($lockPath in $lockPaths) {
    $lock = Read-PackagingJson -Path $lockPath -Label ([IO.Path]::GetFileName($lockPath))
    Assert-ApprovedPackagingLock -Lock $lock -Label ([IO.Path]::GetFileName($lockPath))
    $locks += [ordered]@{
        name = [IO.Path]::GetFileName($lockPath)
        sha256 = (Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$files = @(Get-ChildItem -LiteralPath $stage -Force -Recurse -File | Sort-Object {
    Get-PackagingRelativePath -Root $stage -Path $_.FullName
} | ForEach-Object {
    $relative = Get-PackagingRelativePath -Root $stage -Path $_.FullName
    if ($relative -in @("runtime-manifest.json", "sbom.cdx.json", "source-offer.json", "THIRD_PARTY_NOTICES.txt") -or
        $relative.StartsWith("licenses/", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Metadata must be staged after the runtime manifest and SBOM: $relative"
    }
    if (($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Runtime manifest input is a reparse point: $relative"
    }
    [ordered]@{
        path = $relative
        bytes = [int64]$_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})

if ($files.Count -eq 0) {
    throw "Cannot generate a runtime manifest for an empty stage."
}

$runtimeClosureSha256 = Get-PackagingFileSetDigest -Entries $files
$manifest = [ordered]@{
    schemaVersion = 2
    manifestType = "moonlit-runtime"
    product = "MoonLit"
    target = "x86_64-pc-windows-msvc"
    source = [ordered]@{
        gitSha = $SourceSha.ToLowerInvariant()
        worktreeStatus = $WorktreeStatus
        version = $ProductVersion
    }
    inputs = [ordered]@{
        locks = $locks
        runtimeClosureSha256 = $runtimeClosureSha256
    }
    files = $files
}

Write-PackagingJson -Path $output -Value $manifest
Write-Output "Generated deterministic runtime manifest with $($files.Count) files: $output"
