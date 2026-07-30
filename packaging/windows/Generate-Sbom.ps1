[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $StageRoot,
    [Parameter(Mandatory = $true)][string] $OutputFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$stage = (Resolve-Path -LiteralPath $StageRoot).Path
$components = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($stage.Length + 1).Replace("\", "/")
    [ordered]@{
        type = "file"
        name = $relative
        version = "unknown"
        hashes = @([ordered]@{ algorithm = "sha256"; value = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() })
        licenses = @([ordered]@{ license = "SEE_THIRD_PARTY_NOTICES" })
    }
})
$sbom = [ordered]@{
    bomFormat = "CycloneDX"
    specVersion = "1.5"
    serialNumber = "urn:uuid:moonlit-runtime-manifest"
    version = 1
    metadata = [ordered]@{ timestamp = [DateTime]::UtcNow.ToString("o"); component = [ordered]@{ type = "application"; name = "MoonLit runtime" } }
    components = $components
}
$parent = Split-Path -Parent $OutputFile
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
$sbom | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $OutputFile -Encoding utf8NoBOM
