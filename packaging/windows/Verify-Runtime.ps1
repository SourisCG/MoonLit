[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $StageRoot,
    [Parameter(Mandatory = $true)][string] $ManifestFile,
    [string] $Dumpbin = "dumpbin.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$stage = (Resolve-Path -LiteralPath $StageRoot).Path
$manifest = Get-Content -LiteralPath $ManifestFile -Raw | ConvertFrom-Json
$dumpbinPath = (Get-Command $Dumpbin -ErrorAction Stop).Source

foreach ($entry in $manifest.files) {
    $path = Join-Path $stage $entry.path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Manifest file is missing: $($entry.path)"
    }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $entry.sha256) {
        throw "Manifest hash mismatch: $($entry.path)"
    }
    if ([IO.Path]::GetExtension($path).ToLowerInvariant() -in @(".exe", ".dll")) {
        $headers = & $dumpbinPath /headers $path | Out-String
        if ($headers -notmatch "machine \(x64\)") {
            throw "Runtime PE is not x64: $($entry.path)"
        }
    }
}

$recorder = Join-Path $stage "bin/64bit/moonlit-recorder.exe"
$selfTestJson = & $recorder --self-test --json --runtime-root $stage
$selfTest = $selfTestJson | ConvertFrom-Json
if (-not $selfTest.ready) {
    throw "Recorder self-test is not ready: $($selfTest.note)"
}

Write-Output "Verified $($manifest.files.Count) runtime files."
