[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $StageRoot,
    [Parameter(Mandatory = $true)][string] $OutputFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$stage = (Resolve-Path -LiteralPath $StageRoot).Path
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($stage.Length + 1).Replace("\", "/")
    [ordered]@{
        path = $relative
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$manifest = [ordered]@{
    schemaVersion = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    files = $files
}
$parent = Split-Path -Parent $OutputFile
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputFile -Encoding utf8NoBOM
