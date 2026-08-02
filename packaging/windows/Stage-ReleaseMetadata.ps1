[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $StageRoot,
    [Parameter(Mandatory = $true)][string] $NoticesFile,
    [Parameter(Mandatory = $true)][string] $LicenseRoot,
    [Parameter(Mandatory = $true)][string] $SourceOfferFile,
    [Parameter(Mandatory = $true)][string] $ApplicationLicenseFile,
    [Parameter(Mandatory = $true)][string] $SourceSha,
    [Parameter(Mandatory = $true)][string] $ProductVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Packaging.Common.ps1")

Assert-PackagingSourceSha -SourceSha $SourceSha
Assert-PackagingVersion -Version $ProductVersion
$stage = Get-PackagingFullPath -Path $StageRoot -Label "StageRoot" -Kind Directory -MustExist
$notices = Get-PackagingFullPath -Path $NoticesFile -Label "NoticesFile" -Kind File -MustExist
$licensesRoot = Get-PackagingFullPath -Path $LicenseRoot -Label "LicenseRoot" -Kind Directory -MustExist
$sourceOffer = Get-PackagingFullPath -Path $SourceOfferFile -Label "SourceOfferFile" -Kind File -MustExist
$applicationLicense = Get-PackagingFullPath -Path $ApplicationLicenseFile -Label "ApplicationLicenseFile" -Kind File -MustExist
foreach ($path in @($stage, $notices, $licensesRoot, $sourceOffer, $applicationLicense)) {
    Assert-PackagingSafePath -Path $path -Label "release metadata path"
}
foreach ($sourcePath in @($notices, $licensesRoot, $sourceOffer, $applicationLicense)) {
    Assert-PackagingDisjointPaths -Left $stage -Right $sourcePath -Label "Runtime stage and metadata input"
}

$noticeText = Get-Content -LiteralPath $notices -Raw
if ([string]::IsNullOrWhiteSpace($noticeText) -or
    $noticeText -match '(?i)development baseline|not release-ready|planned runtime|design-only|runtime-selected|TODO|TBD|placeholder') {
    throw "The notices file is a development or placeholder notice and cannot ship."
}

$offer = Read-PackagingJson -Path $sourceOffer -Label "corresponding-source offer"
Assert-PackagingConcreteValues -Value $offer -Path "corresponding-source offer"
if ($offer.schemaVersion -ne 1 -or $offer.product -cne "MoonLit" -or $offer.version -cne $ProductVersion) {
    throw "Corresponding-source metadata has the wrong product, schema, or version."
}
if ($offer.source.gitSha -ine $SourceSha -or $offer.source.worktreeStatus -cne "clean") {
    throw "Corresponding-source metadata is not bound to the exact clean source SHA."
}

$licensesLockPath = Join-Path $PSScriptRoot "licenses.lock.json"
$licensesLock = Read-PackagingJson -Path $licensesLockPath -Label "license lock"
Assert-ApprovedPackagingLock -Lock $licensesLock -Label "license lock"
$licenseComponents = @($licensesLock.components)
if ($licenseComponents.Count -eq 0) {
    throw "The approved license lock contains no components."
}

# The lock must identify the exact shipped license text and corresponding
# source for every component. A name-only or runtime-selected record is not
# sufficient legal metadata.
foreach ($component in $licenseComponents) {
    foreach ($propertyName in @("name", "version", "license", "source", "licenseFile", "sourceSha256", "filePatterns")) {
        if (-not ($component.PSObject.Properties.Name -contains $propertyName) -or $null -eq $component.$propertyName) {
            throw "License record is missing ${propertyName}: $($component.name)"
        }
    }
    Assert-PackagingHash -Hash ([string]$component.sourceSha256) -Label "license source hash for $($component.name)"
    $patterns = @($component.filePatterns | ForEach-Object { [string]$_ })
    if ($patterns.Count -eq 0) {
        throw "License record has no filePatterns: $($component.name)"
    }
}

$licenseFiles = @(Get-ChildItem -LiteralPath $licensesRoot -Force -Recurse -File)
if ($licenseFiles.Count -eq 0) {
    throw "LicenseRoot contains no license texts."
}
foreach ($file in $licenseFiles) {
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "LicenseRoot contains a reparse point: $($file.FullName)"
    }
    if ($file.Length -eq 0) {
        throw "License text is empty: $($file.FullName)"
    }
}

$metadataTargets = @(
    [pscustomobject]@{ Source = $notices; Destination = (Join-Path $stage "THIRD_PARTY_NOTICES.txt") },
    [pscustomobject]@{ Source = $sourceOffer; Destination = (Join-Path $stage "source-offer.json") },
    [pscustomobject]@{ Source = $applicationLicense; Destination = (Join-Path $stage "licenses/MoonLit-LICENSE.txt") }
)
$stagedLicenseContainer = Join-Path $stage "licenses"
if (Test-Path -LiteralPath $stagedLicenseContainer) {
    throw "The runtime stage already contains a licenses directory: $stagedLicenseContainer"
}
New-Item -ItemType Directory -Path $stagedLicenseContainer -Force | Out-Null
foreach ($target in $metadataTargets) {
    if (Test-Path -LiteralPath $target.Destination) {
        throw "Refusing to overwrite staged release metadata: $($target.Destination)"
    }
    Copy-Item -LiteralPath $target.Source -Destination $target.Destination -Force
}

$stagedLicenseRoot = Join-Path $stage "licenses/third-party"
if (Test-Path -LiteralPath $stagedLicenseRoot) {
    throw "Refusing to overwrite staged third-party licenses: $stagedLicenseRoot"
}
New-Item -ItemType Directory -Path $stagedLicenseRoot -Force | Out-Null
foreach ($file in $licenseFiles) {
    $relative = Get-PackagingRelativePath -Root $licensesRoot -Path $file.FullName
    Assert-PackagingRelativePattern -Pattern $relative -Label "license text path"
    $destination = Join-Path $stagedLicenseRoot $relative
    $destinationDirectory = Split-Path -Parent $destination
    if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container)) {
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    }
    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
}

Write-Output "Staged notices, corresponding-source metadata, and $($licenseFiles.Count) license texts under $stage"
