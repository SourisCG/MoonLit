[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $StageRoot,
    [Parameter(Mandatory = $true)][string] $RuntimeManifestFile,
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
$manifestPath = Get-PackagingFullPath -Path $RuntimeManifestFile -Label "RuntimeManifestFile" -Kind File -MustExist
$output = Get-PackagingFullPath -Path $OutputFile -Label "OutputFile"
Assert-PackagingSafePath -Path $stage -Label "StageRoot"
Assert-PackagingSafePath -Path $manifestPath -Label "RuntimeManifestFile"
Assert-PackagingSafePath -Path $output -Label "OutputFile"
if (-not (Test-PackagingPathWithin -Path $manifestPath -Root $stage) -or
    -not (Test-PackagingPathWithin -Path $output -Root $stage)) {
    throw "The runtime manifest and SBOM must be inside StageRoot."
}
if (Test-Path -LiteralPath $output) {
    throw "Refusing to overwrite an existing SBOM: $output"
}

$manifest = Read-PackagingJson -Path $manifestPath -Label "runtime manifest"
if ($manifest.schemaVersion -ne 2 -or $manifest.manifestType -cne "moonlit-runtime") {
    throw "The runtime manifest schema is unsupported."
}
if ($manifest.source.gitSha -ine $SourceSha -or $manifest.source.worktreeStatus -cne $WorktreeStatus -or
    $manifest.source.version -cne $ProductVersion) {
    throw "Runtime manifest source, worktree status, or version does not match the SBOM inputs."
}
if ($manifest.files.Count -eq 0) {
    throw "The runtime manifest contains no files."
}

$licensesPath = Join-Path $PSScriptRoot "licenses.lock.json"
$licenses = Read-PackagingJson -Path $licensesPath -Label "license lock"
Assert-ApprovedPackagingLock -Lock $licenses -Label "license lock"
$licenseComponents = @($licenses.components)
if ($licenseComponents.Count -eq 0) {
    throw "The approved license lock contains no components."
}

$manifestSha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$runtimeClosureSha256 = [string]$manifest.inputs.runtimeClosureSha256
Assert-PackagingHash -Hash $runtimeClosureSha256 -Label "runtime closure hash"

$componentList = @()
$bomRefs = @()
foreach ($entry in @($manifest.files)) {
    $path = [string]$entry.path
    Assert-PackagingRelativePattern -Pattern $path -Label "runtime manifest path"
    Assert-PackagingHash -Hash ([string]$entry.sha256) -Label "runtime manifest file hash"

    $matches = @()
    foreach ($licenseComponent in $licenseComponents) {
        $patterns = @()
        if ($licenseComponent.PSObject.Properties.Name -contains "filePatterns") {
            $patterns = @($licenseComponent.filePatterns | ForEach-Object { [string]$_ })
        } elseif ($licenseComponent.PSObject.Properties.Name -contains "files") {
            $patterns = @($licenseComponent.files | ForEach-Object { [string]$_ })
        }
        foreach ($pattern in $patterns) {
            Assert-PackagingRelativePattern -Pattern $pattern -Label "license component file pattern"
            if ($path -like $pattern.Replace('\', '/')) {
                $matches += $licenseComponent
                break
            }
        }
    }
    if ($matches.Count -ne 1) {
        throw "Runtime file must match exactly one license record: $path (matches=$($matches.Count))"
    }
    $licenseComponent = $matches[0]
    foreach ($propertyName in @("name", "version", "license", "source", "licenseFile", "sourceSha256")) {
        if ($null -eq $licenseComponent.$propertyName) {
            throw "License record for $path is missing $propertyName."
        }
    }
    Assert-PackagingHash -Hash ([string]$licenseComponent.sourceSha256) -Label "license source hash for $path"

    $bomRef = "urn:uuid:" + (New-PackagingDeterministicUuid -Seed "file|$path|$($entry.sha256)")
    $bomRefs += $bomRef
    $componentList += [ordered]@{
        type = "file"
        "bom-ref" = $bomRef
        name = $path
        hashes = @([ordered]@{
            algorithm = "SHA-256"
            value = ([string]$entry.sha256).ToLowerInvariant()
        })
        licenses = @([ordered]@{
            license = [ordered]@{
                # `name` is valid for project-specific or non-SPDX legal
                # notices; unlike the old string form it matches CycloneDX.
                name = [string]$licenseComponent.license
            }
        })
        properties = @(
            [ordered]@{ name = "moonlit.license.component"; value = [string]$licenseComponent.name },
            [ordered]@{ name = "moonlit.license.version"; value = [string]$licenseComponent.version },
            [ordered]@{ name = "moonlit.license.source"; value = [string]$licenseComponent.source },
            [ordered]@{ name = "moonlit.license.file"; value = [string]$licenseComponent.licenseFile }
        )
    }
}

$applicationRef = "urn:uuid:" + (New-PackagingDeterministicUuid -Seed "application|MoonLit|$ProductVersion")
$serialSeed = "$SourceSha`n$ProductVersion`n$runtimeClosureSha256`n$manifestSha256"
$serialNumber = "urn:uuid:" + (New-PackagingDeterministicUuid -Seed $serialSeed)
$sbom = [ordered]@{
    bomFormat = "CycloneDX"
    specVersion = "1.5"
    serialNumber = $serialNumber
    version = 1
    metadata = [ordered]@{
        component = [ordered]@{
            type = "application"
            "bom-ref" = $applicationRef
            name = "MoonLit"
            version = $ProductVersion
        }
        properties = @(
            [ordered]@{ name = "moonlit.source.gitSha"; value = $SourceSha.ToLowerInvariant() },
            [ordered]@{ name = "moonlit.source.worktreeStatus"; value = $WorktreeStatus },
            [ordered]@{ name = "moonlit.runtime.manifestSha256"; value = $manifestSha256 },
            [ordered]@{ name = "moonlit.runtime.closureSha256"; value = $runtimeClosureSha256 },
            [ordered]@{ name = "moonlit.license.lockSha256"; value = (Get-FileHash -LiteralPath $licensesPath -Algorithm SHA256).Hash.ToLowerInvariant() }
        )
    }
    components = @($componentList)
    dependencies = @([ordered]@{
        ref = $applicationRef
        dependsOn = @($bomRefs)
    })
}

Write-PackagingJson -Path $output -Value $sbom
Write-Output "Generated deterministic CycloneDX 1.5 SBOM with $($componentList.Count) components: $output"
