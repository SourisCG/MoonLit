[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $StageRoot,
    [Parameter(Mandatory = $true)][string] $RuntimeManifestFile,
    [Parameter(Mandatory = $true)][string] $SbomFile,
    [Parameter(Mandatory = $true)][string] $SourceOfferFile,
    [Parameter(Mandatory = $true)][string] $NoticesFile,
    [Parameter(Mandatory = $true)][string] $LicenseRoot,
    [Parameter(Mandatory = $true)][string[]] $InstallerFiles,
    [Parameter(Mandatory = $true)][string] $OutputFile,
    [Parameter(Mandatory = $true)][string] $SourceSha,
    [Parameter(Mandatory = $true)][ValidateSet("clean")][string] $WorktreeStatus,
    [Parameter(Mandatory = $true)][string] $ProductVersion,
    [Parameter(Mandatory = $true)][ValidateSet("standard", "offline")][string] $WebviewMode,
    [Parameter(Mandatory = $true)][string] $CertificateThumbprint,
    [string] $SignTool = "signtool.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Packaging.Common.ps1")

Assert-PackagingSourceSha -SourceSha $SourceSha
Assert-PackagingVersion -Version $ProductVersion
$CertificateThumbprint = $CertificateThumbprint.Replace(' ', '').Trim()
if ($CertificateThumbprint -notmatch '^[0-9a-fA-F]{40}$') {
    throw "CertificateThumbprint is mandatory for a release manifest."
}

$stage = Get-PackagingFullPath -Path $StageRoot -Label "StageRoot" -Kind Directory -MustExist
$runtimeManifest = Get-PackagingFullPath -Path $RuntimeManifestFile -Label "RuntimeManifestFile" -Kind File -MustExist
$sbom = Get-PackagingFullPath -Path $SbomFile -Label "SbomFile" -Kind File -MustExist
$sourceOffer = Get-PackagingFullPath -Path $SourceOfferFile -Label "SourceOfferFile" -Kind File -MustExist
$notices = Get-PackagingFullPath -Path $NoticesFile -Label "NoticesFile" -Kind File -MustExist
$licenseRoot = Get-PackagingFullPath -Path $LicenseRoot -Label "LicenseRoot" -Kind Directory -MustExist
$output = Get-PackagingFullPath -Path $OutputFile -Label "OutputFile"
foreach ($path in @($stage, $runtimeManifest, $sbom, $sourceOffer, $notices, $licenseRoot)) {
    Assert-PackagingSafePath -Path $path -Label "release manifest input"
}
Assert-PackagingSafePath -Path $output -Label "release manifest output"
if (Test-PackagingPathWithin -Path $output -Root $stage) {
    throw "Release manifest must remain outside the bundled runtime stage."
}
foreach ($path in @($runtimeManifest, $sbom, $sourceOffer, $notices, $licenseRoot)) {
    if (-not (Test-PackagingPathWithin -Path $path -Root $stage)) {
        throw "Release metadata is outside StageRoot: $path"
    }
}
if (Test-Path -LiteralPath $output) {
    throw "Refusing to overwrite a release manifest: $output"
}

$signToolCommand = Get-Command -Name $SignTool -CommandType Application -ErrorAction Stop
$signToolPath = $signToolCommand.Source
$installers = @()
$seenInstallers = @{}
foreach ($file in $InstallerFiles) {
    $installer = Get-PackagingFullPath -Path $file -Label "InstallerFile" -Kind File -MustExist
    Assert-PackagingSafePath -Path $installer -Label "InstallerFile"
    if ([IO.Path]::GetExtension($installer).ToLowerInvariant() -ne ".exe") {
        throw "NSIS installer input must be an .exe: $installer"
    }
    $key = $installer.ToLowerInvariant()
    if ($seenInstallers.ContainsKey($key)) {
        throw "Duplicate installer input: $installer"
    }
    $seenInstallers[$key] = $true
    Assert-PackagingSignature -File $installer -SignTool $signToolPath -CertificateThumbprint $CertificateThumbprint
    $installers += $installer
}
if ($installers.Count -eq 0) {
    throw "No signed NSIS installer was supplied."
}

$manifest = Read-PackagingJson -Path $runtimeManifest -Label "runtime manifest"
if ($manifest.schemaVersion -ne 2 -or $manifest.source.gitSha -ine $SourceSha -or
    $manifest.source.worktreeStatus -cne $WorktreeStatus -or $manifest.source.version -cne $ProductVersion) {
    throw "Runtime manifest does not match release source inputs."
}
$sbomObject = Read-PackagingJson -Path $sbom -Label "CycloneDX SBOM"
if ($sbomObject.bomFormat -cne "CycloneDX" -or $sbomObject.specVersion -cne "1.5") {
    throw "Release SBOM is not CycloneDX 1.5."
}
$offer = Read-PackagingJson -Path $sourceOffer -Label "corresponding-source offer"
if ($offer.schemaVersion -ne 1 -or $offer.product -cne "MoonLit" -or $offer.version -cne $ProductVersion -or
    $offer.source.gitSha -ine $SourceSha -or $offer.source.worktreeStatus -cne $WorktreeStatus) {
    throw "Release corresponding-source metadata does not match source inputs."
}

$runtimeManifestHash = (Get-FileHash -LiteralPath $runtimeManifest -Algorithm SHA256).Hash.ToLowerInvariant()
$sbomHash = (Get-FileHash -LiteralPath $sbom -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceOfferHash = (Get-FileHash -LiteralPath $sourceOffer -Algorithm SHA256).Hash.ToLowerInvariant()
$noticesHash = (Get-FileHash -LiteralPath $notices -Algorithm SHA256).Hash.ToLowerInvariant()
$licenseFiles = @(Get-ChildItem -LiteralPath $licenseRoot -Force -Recurse -File | Sort-Object {
    Get-PackagingRelativePath -Root $licenseRoot -Path $_.FullName
} | ForEach-Object {
    if (($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Release license tree contains a reparse point: $($_.FullName)"
    }
    $relative = Get-PackagingRelativePath -Root $licenseRoot -Path $_.FullName
    [ordered]@{
        path = "licenses/$relative"
        bytes = [int64]$_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
if ($licenseFiles.Count -eq 0) {
    throw "Release manifest cannot omit license texts."
}

$artifactEntries = @($installers | Sort-Object | ForEach-Object {
    $name = [IO.Path]::GetFileName($_)
    [ordered]@{
        kind = "nsis-installer"
        webview = $WebviewMode
        path = $name
        bytes = [int64](Get-Item -LiteralPath $_).Length
        sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$runtimeClosure = [string]$manifest.inputs.runtimeClosureSha256
Assert-PackagingHash -Hash $runtimeClosure -Label "runtime closure hash"
$release = [ordered]@{
    schemaVersion = 1
    manifestType = "moonlit-release"
    product = "MoonLit"
    version = $ProductVersion
    target = "x86_64-pc-windows-msvc"
    webview = $WebviewMode
    source = [ordered]@{
        gitSha = $SourceSha.ToLowerInvariant()
        worktreeStatus = $WorktreeStatus
    }
    runtime = [ordered]@{
        closureSha256 = $runtimeClosure
        manifestSha256 = $runtimeManifestHash
        sbomSha256 = $sbomHash
        fileCount = @($manifest.files).Count
    }
    legal = [ordered]@{
        noticesSha256 = $noticesHash
        sourceOfferSha256 = $sourceOfferHash
        licenseFiles = $licenseFiles
    }
    installers = $artifactEntries
}

$canonical = $release | ConvertTo-Json -Depth 100 -Compress
$release.artifactSetSha256 = Get-PackagingStringSha256 -Value $canonical
Write-PackagingJson -Path $output -Value $release
Write-Output "Generated release manifest for $WebviewMode with $($artifactEntries.Count) signed installer(s): $output"
