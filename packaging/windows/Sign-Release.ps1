[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $SignTool,
    [Parameter(Mandatory = $true)][string] $CertificateThumbprint,
    [Parameter(Mandatory = $true)][string[]] $Files,
    [string] $TimestampUrl = "http://timestamp.digicert.com"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$tool = (Resolve-Path -LiteralPath $SignTool).Path
foreach ($file in $Files) {
    $resolved = (Resolve-Path -LiteralPath $file).Path
    & $tool sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $resolved
    if ($LASTEXITCODE -ne 0) { throw "Signing failed: $resolved" }
    & $tool verify /pa /all $resolved
    if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $resolved" }
}
