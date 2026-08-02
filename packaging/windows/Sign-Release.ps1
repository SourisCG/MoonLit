[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $SignTool,
    [Parameter(Mandatory = $true)][string] $CertificateThumbprint,
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string[]] $Files,
    [ValidatePattern('^https://')][string] $TimestampUrl = "https://timestamp.digicert.com"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Packaging.Common.ps1")

$CertificateThumbprint = $CertificateThumbprint.Replace(' ', '').Trim()
if ($CertificateThumbprint -notmatch '^[0-9a-fA-F]{40}$') {
    throw "A certificate thumbprint is mandatory and must be a 40-character SHA-1 thumbprint."
}
if ($TimestampUrl -notmatch '^https://[^\s/]+(?:/[^\s]*)?$') {
    throw "TimestampUrl must use HTTPS."
}

$toolCommand = Get-Command -Name $SignTool -CommandType Application -ErrorAction Stop
$toolPath = $toolCommand.Source
if ([string]::IsNullOrWhiteSpace($toolPath) -or -not (Test-Path -LiteralPath $toolPath -PathType Leaf)) {
    throw "SignTool could not be resolved to an executable: $SignTool"
}
$uniqueFiles = @{}
$resolvedFiles = @()
foreach ($file in $Files) {
    $resolved = Get-PackagingFullPath -Path $file -Label "signing input" -Kind File -MustExist
    Assert-PackagingSafePath -Path $resolved -Label "signing input"
    $extension = [IO.Path]::GetExtension($resolved).ToLowerInvariant()
    if ($extension -notin @(".exe", ".dll")) {
        throw "Only shipped Windows PE .exe and .dll files may be signed: $resolved"
    }
    $key = $resolved.ToLowerInvariant()
    if ($uniqueFiles.ContainsKey($key)) {
        throw "Duplicate signing input: $resolved"
    }
    $uniqueFiles[$key] = $true
    $resolvedFiles += $resolved
}
if ($resolvedFiles.Count -eq 0) {
    throw "No PE files were supplied for signing."
}

foreach ($resolved in $resolvedFiles) {
    & $toolPath sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $resolved
    if ($LASTEXITCODE -ne 0) {
        throw "Signing failed: $resolved"
    }
    Assert-PackagingSignature -File $resolved -SignTool $toolPath -CertificateThumbprint $CertificateThumbprint
}

Write-Output "Signed and verified $($resolvedFiles.Count) PE files."
