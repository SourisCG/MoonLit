[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $StageRoot,
    [Parameter(Mandatory = $true)][string] $ManifestFile,
    [Parameter(Mandatory = $true)][string] $SbomFile,
    [Parameter(Mandatory = $true)][string] $SourceSha,
    [Parameter(Mandatory = $true)][string] $ProductVersion,
    [Parameter(Mandatory = $true)][string] $CertificateThumbprint,
    [string] $SourceOfferFile,
    [string] $NoticesFile,
    [string] $LicenseRoot,
    [string] $Dumpbin = "dumpbin.exe",
    [string] $SignTool = "signtool.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Packaging.Common.ps1")

Assert-PackagingSourceSha -SourceSha $SourceSha
Assert-PackagingVersion -Version $ProductVersion
$CertificateThumbprint = $CertificateThumbprint.Replace(' ', '').Trim()
if ($CertificateThumbprint -notmatch '^[0-9a-fA-F]{40}$') {
    throw "CertificateThumbprint is required for runtime verification."
}

$stage = Get-PackagingFullPath -Path $StageRoot -Label "StageRoot" -Kind Directory -MustExist
$manifestPath = Get-PackagingFullPath -Path $ManifestFile -Label "ManifestFile" -Kind File -MustExist
$sbomPath = Get-PackagingFullPath -Path $SbomFile -Label "SbomFile" -Kind File -MustExist
Assert-PackagingSafePath -Path $stage -Label "StageRoot"
Assert-PackagingSafePath -Path $manifestPath -Label "ManifestFile"
Assert-PackagingSafePath -Path $sbomPath -Label "SbomFile"
if (-not (Test-PackagingPathWithin -Path $manifestPath -Root $stage) -or
    -not (Test-PackagingPathWithin -Path $sbomPath -Root $stage)) {
    throw "Manifest and SBOM must be inside StageRoot."
}

if ([string]::IsNullOrWhiteSpace($SourceOfferFile)) {
    $SourceOfferFile = Join-Path $stage "source-offer.json"
}
if ([string]::IsNullOrWhiteSpace($NoticesFile)) {
    $NoticesFile = Join-Path $stage "THIRD_PARTY_NOTICES.txt"
}
if ([string]::IsNullOrWhiteSpace($LicenseRoot)) {
    $LicenseRoot = Join-Path $stage "licenses"
}
$sourceOfferPath = Get-PackagingFullPath -Path $SourceOfferFile -Label "SourceOfferFile" -Kind File -MustExist
$noticesPath = Get-PackagingFullPath -Path $NoticesFile -Label "NoticesFile" -Kind File -MustExist
$licenseRootPath = Get-PackagingFullPath -Path $LicenseRoot -Label "LicenseRoot" -Kind Directory -MustExist
foreach ($path in @($sourceOfferPath, $noticesPath, $licenseRootPath)) {
    Assert-PackagingSafePath -Path $path -Label "staged release metadata"
    if (-not (Test-PackagingPathWithin -Path $path -Root $stage)) {
        throw "Release metadata must be inside StageRoot: $path"
    }
}

$dumpbinCommand = Get-Command -Name $Dumpbin -CommandType Application -ErrorAction Stop
$dumpbinPath = $dumpbinCommand.Source
$signToolCommand = Get-Command -Name $SignTool -CommandType Application -ErrorAction Stop
$signToolPath = $signToolCommand.Source

$lockFiles = [ordered]@{
    "obs-runtime.lock.json" = (Join-Path $PSScriptRoot "obs-runtime.lock.json")
    "runtime.allowlist.json" = (Join-Path $PSScriptRoot "runtime.allowlist.json")
    "system-dlls.allowlist.json" = (Join-Path $PSScriptRoot "system-dlls.allowlist.json")
    "licenses.lock.json" = (Join-Path $PSScriptRoot "licenses.lock.json")
}
$lockObjects = @{}
foreach ($name in $lockFiles.Keys) {
    $lockObject = Read-PackagingJson -Path $lockFiles[$name] -Label $name
    Assert-ApprovedPackagingLock -Lock $lockObject -Label $name
    $lockObjects[$name] = $lockObject
}
$allowlist = $lockObjects["runtime.allowlist.json"]
$systemDlls = $lockObjects["system-dlls.allowlist.json"]
$allowPatterns = @($allowlist.files | ForEach-Object { [string]$_ })
$requiredFiles = @($allowlist.requiredFiles | ForEach-Object { [string]$_ })
$denyNames = @($allowlist.denyNames | ForEach-Object { [string]$_ })
$denyExtensions = @($allowlist.denyExtensions | ForEach-Object { [string]$_ })
foreach ($pattern in $allowPatterns + $requiredFiles) {
    Assert-PackagingRelativePattern -Pattern $pattern -Label "runtime allowlist"
}

$manifest = Read-PackagingJson -Path $manifestPath -Label "runtime manifest"
if ($manifest.schemaVersion -ne 2 -or $manifest.manifestType -cne "moonlit-runtime" -or
    $manifest.product -cne "MoonLit" -or $manifest.target -cne "x86_64-pc-windows-msvc") {
    throw "Runtime manifest schema, product, or target is invalid."
}
if ($manifest.source.gitSha -ine $SourceSha -or $manifest.source.worktreeStatus -cne "clean" -or
    $manifest.source.version -cne $ProductVersion) {
    throw "Runtime manifest is not bound to the exact clean source SHA and version."
}
$manifestEntries = @($manifest.files)
if ($manifestEntries.Count -eq 0) {
    throw "Runtime manifest contains no files."
}
$manifestByPath = @{}
foreach ($entry in $manifestEntries) {
    $relative = [string]$entry.path
    Assert-PackagingRelativePattern -Pattern $relative -Label "runtime manifest path"
    if ($relative -match '(?i)^(runtime-manifest\.json|sbom\.cdx\.json|source-offer\.json|third_party_notices\.txt)$' -or
        $relative.StartsWith("licenses/", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Generated/legal metadata must not be part of runtime closure: $relative"
    }
    if (-not (Test-PackagingAllowlistedPath -RelativePath $relative -Patterns $allowPatterns)) {
        throw "Manifest contains a file outside the runtime allowlist: $relative"
    }
    $key = $relative.ToLowerInvariant()
    if ($manifestByPath.ContainsKey($key)) {
        throw "Runtime manifest contains duplicate paths: $relative"
    }
    Assert-PackagingHash -Hash ([string]$entry.sha256) -Label "runtime manifest hash for $relative"
    if ([int64]$entry.bytes -lt 0) {
        throw "Runtime manifest has a negative byte count: $relative"
    }
    $manifestByPath[$key] = $entry
}

$actualPayload = @()
$actualMetadata = @{}
$metadataNames = @(
    "runtime-manifest.json",
    "sbom.cdx.json",
    "source-offer.json",
    "third_party_notices.txt",
    "licenses/moonlit-license.txt"
)
foreach ($file in @(Get-ChildItem -LiteralPath $stage -Force -Recurse -File)) {
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Staged runtime contains a reparse point: $($file.FullName)"
    }
    $relative = Get-PackagingRelativePath -Root $stage -Path $file.FullName
    $lower = $relative.ToLowerInvariant()
    $isMetadata = $metadataNames -contains $lower -or $lower.StartsWith("licenses/third-party/", [StringComparison]::Ordinal)
    if ($isMetadata) {
        if ($actualMetadata.ContainsKey($lower)) {
            throw "Duplicate staged metadata path: $relative"
        }
        $actualMetadata[$lower] = $file
        continue
    }
    if (-not $manifestByPath.ContainsKey($lower)) {
        throw "Unexpected extra file in staged runtime: $relative"
    }
    $actualPayload += $file
}

foreach ($requiredMetadata in @($metadataNames)) {
    if (-not $actualMetadata.ContainsKey($requiredMetadata)) {
        throw "Required release metadata is missing from stage: $requiredMetadata"
    }
}
if (@($actualMetadata.Keys | Where-Object { $_ -like "licenses/third-party/*" }).Count -eq 0) {
    throw "No third-party license texts were staged."
}
if ($actualPayload.Count -ne $manifestEntries.Count) {
    throw "Runtime payload file count does not match the manifest."
}

foreach ($file in $actualPayload) {
    $relative = Get-PackagingRelativePath -Root $stage -Path $file.FullName
    $entry = $manifestByPath[$relative.ToLowerInvariant()]
    $actualHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne ([string]$entry.sha256).ToLowerInvariant() -or [int64]$file.Length -ne [int64]$entry.bytes) {
        throw "Runtime manifest hash or size mismatch: $relative"
    }
}

$sortedManifestPaths = @($manifestEntries | ForEach-Object { [string]$_.path } | Sort-Object)
if ((@($manifestEntries | ForEach-Object { [string]$_.path }) -join "`n") -ne ($sortedManifestPaths -join "`n")) {
    throw "Runtime manifest file order is not deterministic."
}
$closure = Get-PackagingFileSetDigest -Entries $manifestEntries
if ($closure -ne ([string]$manifest.inputs.runtimeClosureSha256).ToLowerInvariant()) {
    throw "Runtime closure digest does not match the manifest entries."
}

$manifestLockEntries = @($manifest.inputs.locks)
foreach ($name in $lockFiles.Keys) {
    $record = @($manifestLockEntries | Where-Object { $_.name -ceq $name })
    if ($record.Count -ne 1) {
        throw "Runtime manifest does not bind exactly one lock input: $name"
    }
    $actualLockHash = (Get-FileHash -LiteralPath $lockFiles[$name] -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualLockHash -ne ([string]$record[0].sha256).ToLowerInvariant()) {
        throw "Runtime manifest lock hash mismatch: $name"
    }
}

function Test-PolicyName {
    param([Parameter(Mandatory = $true)][string] $Name, [Parameter(Mandatory = $true)][string[]] $Patterns)
    foreach ($pattern in $Patterns) {
        if ($Name -like $pattern) {
            return $true
        }
    }
    return $false
}

$peFiles = @($actualPayload | Where-Object {
    [IO.Path]::GetExtension($_.Name).ToLowerInvariant() -in @(".exe", ".dll")
})
if ($peFiles.Count -eq 0) {
    throw "Runtime closure contains no PE files."
}
$peByName = @{}
foreach ($file in $peFiles) {
    $name = $file.Name.ToLowerInvariant()
    if ($peByName.ContainsKey($name)) {
        throw "Duplicate PE basename prevents deterministic DLL resolution: $name"
    }
    $peByName[$name] = $file

    $headers = (& $dumpbinPath /headers $file.FullName 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or $headers -notmatch '(?i)(?:8664\s+machine\s+\(x64\)|machine\s+\(x64\))') {
        throw "Runtime PE is not a readable x64 image: $($file.FullName)"
    }
    Assert-PackagingSignature -File $file.FullName -SignTool $signToolPath -CertificateThumbprint $CertificateThumbprint
}

$systemImportPatterns = @($systemDlls.allowedSystemImports | ForEach-Object { [string]$_ })
$driverImportPatterns = @($systemDlls.driverProvided | ForEach-Object { [string]$_ })
foreach ($file in $peFiles) {
    $dependents = (& $dumpbinPath /dependents $file.FullName 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect PE imports: $($file.FullName)"
    }
    $imports = @($dependents -split "`r?`n" | ForEach-Object {
        if ($_ -match '^\s*([A-Za-z0-9][A-Za-z0-9._-]*\.dll)\s*$') {
            $matches[1].ToLowerInvariant()
        }
    } | Sort-Object -Unique)
    foreach ($import in $imports) {
        if (Test-PackagingDeniedPath -RelativePath $import -DenyNames $denyNames -DenyExtensions $denyExtensions) {
            throw "PE import is denylisted: $import imported by $($file.Name)"
        }
        if (Test-PolicyName -Name $import -Patterns $driverImportPatterns) {
            # Driver DLLs are a documented host dependency and must never be
            # copied into the application-local runtime.
            throw "Driver-provided import must not be bundled: $import"
        }
        if (Test-PolicyName -Name $import -Patterns $systemImportPatterns) {
            continue
        }
        if (-not $peByName.ContainsKey($import)) {
            throw "Unresolved non-system PE import: $import imported by $($file.Name)"
        }
        $resolvedRelative = Get-PackagingRelativePath -Root $stage -Path $peByName[$import].FullName
        if (-not $resolvedRelative.StartsWith("bin/64bit/", [StringComparison]::OrdinalIgnoreCase)) {
            throw "PE import resolves outside bin/64bit: $import -> $resolvedRelative"
        }
    }
}

$sbom = Read-PackagingJson -Path $sbomPath -Label "CycloneDX SBOM"
if ($sbom.bomFormat -cne "CycloneDX" -or $sbom.specVersion -cne "1.5" -or [int]$sbom.version -ne 1 -or
    $sbom.serialNumber -notmatch '^urn:uuid:[0-9a-f]{8}-[0-9a-f]{4}-5[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$') {
    throw "CycloneDX SBOM header is invalid."
}
$expectedSerial = "urn:uuid:" + (New-PackagingDeterministicUuid -Seed "$SourceSha`n$ProductVersion`n$closure`n$((Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant())")
if ($sbom.serialNumber -cne $expectedSerial) {
    throw "CycloneDX serialNumber is not deterministic for the source and runtime manifest."
}
if ($sbom.metadata.PSObject.Properties.Name -contains "timestamp") {
    throw "CycloneDX metadata.timestamp is not permitted in a deterministic SBOM."
}
$sbomProperties = @{}
foreach ($property in @($sbom.metadata.properties)) {
    $sbomProperties[[string]$property.name] = [string]$property.value
}
foreach ($property in @{
    "moonlit.source.gitSha" = $SourceSha.ToLowerInvariant()
    "moonlit.source.worktreeStatus" = "clean"
    "moonlit.runtime.closureSha256" = $closure
}.GetEnumerator()) {
    if (-not $sbomProperties.ContainsKey($property.Key) -or $sbomProperties[$property.Key] -cne $property.Value) {
        throw "CycloneDX source/runtime property mismatch: $($property.Key)"
    }
}

$sbomComponents = @($sbom.components)
if ($sbomComponents.Count -ne $manifestEntries.Count) {
    throw "CycloneDX component count does not match runtime manifest."
}
$componentByName = @{}
foreach ($component in $sbomComponents) {
    if ($component.type -cne "file" -or $null -eq $component.'bom-ref' -or $null -eq $component.name) {
        throw "CycloneDX file component is malformed."
    }
    $path = [string]$component.name
    if ($componentByName.ContainsKey($path.ToLowerInvariant())) {
        throw "CycloneDX contains duplicate file components: $path"
    }
    $componentByName[$path.ToLowerInvariant()] = $component
    if (-not $manifestByPath.ContainsKey($path.ToLowerInvariant())) {
        throw "CycloneDX contains an unmanifested component: $path"
    }
    $expectedRef = "urn:uuid:" + (New-PackagingDeterministicUuid -Seed "file|$path|$($manifestByPath[$path.ToLowerInvariant()].sha256)")
    if ([string]$component.'bom-ref' -cne $expectedRef) {
        throw "CycloneDX bom-ref is not deterministic: $path"
    }
    $hashes = @($component.hashes | Where-Object { $_.algorithm -ceq "SHA-256" })
    if ($hashes.Count -ne 1 -or [string]$hashes[0].value -ine [string]$manifestByPath[$path.ToLowerInvariant()].sha256) {
        throw "CycloneDX SHA-256 does not match the runtime manifest: $path"
    }
    $licenses = @($component.licenses | Where-Object { $null -ne $_.license -and $null -ne $_.license.name })
    if ($licenses.Count -ne 1) {
        throw "CycloneDX component has no valid license name: $path"
    }
}

$recorder = Join-Path $stage "bin/64bit/moonlit-recorder.exe"
if (-not (Test-Path -LiteralPath $recorder -PathType Leaf)) {
    throw "The staged recorder is missing."
}
$selfTestJson = (& $recorder --self-test --json --runtime-root $stage 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "Recorder self-test process failed."
}
try {
    $selfTest = $selfTestJson | ConvertFrom-Json -Depth 20
} catch {
    throw "Recorder self-test did not return valid JSON: $($_.Exception.Message)"
}
if (-not $selfTest.ready) {
    throw "Recorder self-test is not ready: $($selfTest.note)"
}

$offer = Read-PackagingJson -Path $sourceOfferPath -Label "corresponding-source offer"
Assert-PackagingConcreteValues -Value $offer -Path "corresponding-source offer"
if ($offer.schemaVersion -ne 1 -or $offer.product -cne "MoonLit" -or $offer.version -cne $ProductVersion -or
    $offer.source.gitSha -ine $SourceSha -or $offer.source.worktreeStatus -cne "clean") {
    throw "Corresponding-source metadata is not bound to the exact source SHA and version."
}
$noticeText = Get-Content -LiteralPath $noticesPath -Raw
if ([string]::IsNullOrWhiteSpace($noticeText) -or
    $noticeText -match '(?i)development baseline|not release-ready|planned runtime|design-only|runtime-selected|TODO|TBD|placeholder') {
    throw "Staged notices are not release-ready."
}

Write-Output "Verified $($manifestEntries.Count) runtime files, recursive imports, deterministic SBOM, metadata, and Authenticode signatures."
