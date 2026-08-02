[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $RuntimeInput,

    [Parameter(Mandatory = $true)]
    [string] $RecorderBinary,

    [Parameter(Mandatory = $true)]
    [string] $OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Packaging.Common.ps1")

$lockPath = Join-Path $PSScriptRoot "obs-runtime.lock.json"
$allowlistPath = Join-Path $PSScriptRoot "runtime.allowlist.json"
$systemDllsPath = Join-Path $PSScriptRoot "system-dlls.allowlist.json"
$licensesPath = Join-Path $PSScriptRoot "licenses.lock.json"
$lock = Read-PackagingJson -Path $lockPath -Label "OBS runtime lock"
$allowlist = Read-PackagingJson -Path $allowlistPath -Label "runtime allowlist"
$systemDlls = Read-PackagingJson -Path $systemDllsPath -Label "system DLL allowlist"
$licenses = Read-PackagingJson -Path $licensesPath -Label "license lock"

# These checks intentionally reject the repository's current design-only
# inputs. A staging job must never turn a plan, placeholder, or partial audit
# into a release input by accident.
Assert-ApprovedPackagingLock -Lock $lock -Label "OBS runtime lock"
Assert-ApprovedPackagingLock -Lock $allowlist -Label "runtime allowlist"
Assert-ApprovedPackagingLock -Lock $systemDlls -Label "system DLL allowlist"
Assert-ApprovedPackagingLock -Lock $licenses -Label "license lock"

if ([string]$lock.target -cne "x86_64-pc-windows-msvc") {
    throw "The runtime lock target is not x86_64-pc-windows-msvc."
}

$allowPatterns = @($allowlist.files | ForEach-Object { [string]$_ })
$requiredFiles = @($allowlist.requiredFiles | ForEach-Object { [string]$_ })
$denyNames = @($allowlist.denyNames | ForEach-Object { [string]$_ })
$denyExtensions = @($allowlist.denyExtensions | ForEach-Object { [string]$_ })
if ($allowPatterns.Count -eq 0 -or $requiredFiles.Count -eq 0) {
    throw "The runtime allowlist must contain files and requiredFiles."
}
foreach ($pattern in $allowPatterns) {
    Assert-PackagingRelativePattern -Pattern $pattern -Label "runtime allowlist files"
}
foreach ($required in $requiredFiles) {
    Assert-PackagingRelativePattern -Pattern $required -Label "runtime allowlist requiredFiles"
    if (-not (Test-PackagingAllowlistedPath -RelativePath $required -Patterns $allowPatterns)) {
        throw "Required runtime file is not covered by the allowlist: $required"
    }
}

$inputRoot = Get-PackagingFullPath -Path $RuntimeInput -Label "RuntimeInput" -Kind Directory -MustExist
$recorderPath = Get-PackagingFullPath -Path $RecorderBinary -Label "RecorderBinary" -Kind File -MustExist
$outputRoot = Get-PackagingFullPath -Path $OutputRoot -Label "OutputRoot"
$repositoryRoot = [IO.Directory]::GetParent([IO.Directory]::GetParent($PSScriptRoot).FullName).FullName
$expectedOutputRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "target/package-stage/windows-x86_64"))
if (-not [string]::Equals($outputRoot, $expectedOutputRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be the canonical repository staging path: $expectedOutputRoot"
}
Assert-PackagingSafePath -Path $inputRoot -Label "RuntimeInput"
Assert-PackagingSafePath -Path $recorderPath -Label "RecorderBinary"
Assert-PackagingSafePath -Path $outputRoot -Label "OutputRoot"

$outputParent = [IO.Directory]::GetParent($outputRoot)
if ($null -eq $outputParent -or -not (Test-Path -LiteralPath $outputParent.FullName -PathType Container)) {
    throw "The output parent directory does not exist: $outputRoot"
}
Assert-PackagingSafePath -Path $outputParent.FullName -Label "OutputRoot parent"
Assert-PackagingDisjointPaths -Left $inputRoot -Right $outputRoot -Label "Runtime input and output"

if ((Test-PackagingPathWithin -Path $recorderPath -Root $inputRoot) -or
    (Test-PackagingPathWithin -Path $recorderPath -Root $outputRoot)) {
    throw "RecorderBinary must not be inside RuntimeInput or OutputRoot."
}
if ([IO.Path]::GetFileName($recorderPath) -ine "moonlit-recorder.exe") {
    throw "RecorderBinary must be named moonlit-recorder.exe."
}

if (-not (Test-Path -LiteralPath $outputRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
}
$stageRoot = Join-Path $outputRoot "runtime/obs"
Assert-PackagingSafePath -Path $stageRoot -Label "runtime stage root"
if (Test-Path -LiteralPath $stageRoot -PathType Leaf) {
    throw "The runtime stage root is not a directory: $stageRoot"
}
if (-not (Test-Path -LiteralPath $stageRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
}

# A stale or hostile stage must not survive into the next build. Reparse
# points are rejected before deletion so Remove-Item cannot follow a link.
foreach ($entry in @(Get-ChildItem -LiteralPath $stageRoot -Force -Recurse)) {
    if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "The runtime stage contains a reparse point: $($entry.FullName)"
    }
}
Get-ChildItem -LiteralPath $stageRoot -Force | Remove-Item -Recurse -Force

$inputFiles = @()
foreach ($entry in @(Get-ChildItem -LiteralPath $inputRoot -Force -Recurse)) {
    if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        $relative = Get-PackagingRelativePath -Root $inputRoot -Path $entry.FullName
        throw "Runtime input contains a reparse point: $relative"
    }
    if ($entry.PSIsContainer) {
        continue
    }

    $relative = Get-PackagingRelativePath -Root $inputRoot -Path $entry.FullName
    if (Test-PackagingDeniedPath -RelativePath $relative -DenyNames $denyNames -DenyExtensions $denyExtensions) {
        throw "Runtime input contains a denylisted file: $relative"
    }
    if (-not (Test-PackagingAllowlistedPath -RelativePath $relative -Patterns $allowPatterns)) {
        throw "Runtime input contains an extra file not covered by the allowlist: $relative"
    }
    if ($relative -ieq "bin/64bit/moonlit-recorder.exe") {
        throw "moonlit-recorder.exe must be supplied by RecorderBinary, not RuntimeInput."
    }
    $inputFiles += [pscustomobject]@{ Item = $entry; RelativePath = $relative }
}

foreach ($file in $inputFiles) {
    $destination = Join-Path $stageRoot $file.RelativePath
    $destinationDirectory = Split-Path -Parent $destination
    if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container)) {
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    }
    Copy-Item -LiteralPath $file.Item.FullName -Destination $destination -Force
}

$recorderDestination = Join-Path $stageRoot "bin/64bit/moonlit-recorder.exe"
$recorderDirectory = Split-Path -Parent $recorderDestination
if (-not (Test-Path -LiteralPath $recorderDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $recorderDirectory -Force | Out-Null
}
Copy-Item -LiteralPath $recorderPath -Destination $recorderDestination -Force

$stagedFiles = @(Get-ChildItem -LiteralPath $stageRoot -Force -Recurse -File)
$dllBasenames = @{}
foreach ($file in $stagedFiles) {
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        $relative = Get-PackagingRelativePath -Root $stageRoot -Path $file.FullName
        throw "Staged runtime contains a reparse point: $relative"
    }
    $relative = Get-PackagingRelativePath -Root $stageRoot -Path $file.FullName
    if (Test-PackagingDeniedPath -RelativePath $relative -DenyNames $denyNames -DenyExtensions $denyExtensions) {
        throw "Staged runtime contains a denylisted file: $relative"
    }
    if (-not (Test-PackagingAllowlistedPath -RelativePath $relative -Patterns $allowPatterns)) {
        throw "Unexpected file in staged runtime: $relative"
    }
    if ([IO.Path]::GetExtension($relative).ToLowerInvariant() -eq ".dll") {
        $basename = [IO.Path]::GetFileName($relative).ToLowerInvariant()
        if ($dllBasenames.ContainsKey($basename)) {
            throw "Duplicate DLL basename in staged runtime: $basename"
        }
        $dllBasenames[$basename] = $relative
    }
}

foreach ($required in $requiredFiles) {
    $requiredPath = Join-Path $stageRoot $required
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file is missing from the staged runtime: $required"
    }
}

Write-Output "Staged $($stagedFiles.Count) runtime files under $stageRoot"
