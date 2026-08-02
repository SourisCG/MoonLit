Set-StrictMode -Version Latest

function Read-PackagingJson {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is missing: $Path"
    }

    try {
        return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -Depth 100
    } catch {
        throw "$Label is not valid JSON: $Path. $($_.Exception.Message)"
    }
}

function Assert-PackagingConcreteValues {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][AllowNull()][object] $Value,
        [Parameter(Mandatory = $true)][string] $Path
    )

    if ($null -eq $Value) {
        throw "$Path must not be null. Release inputs must contain concrete values."
    }

    if ($Value -is [string]) {
        $text = $Value.Trim()
        if ($text.Length -eq 0) {
            throw "$Path must not be empty."
        }

        $normalized = $text.ToLowerInvariant()
        $placeholder = @(
            "design-only",
            "runtime-selected",
            "requires-configure-audit",
            "unknown",
            "placeholder",
            "todo",
            "tbd",
            "latest",
            "template-not-for-installation"
        )
        if ($placeholder -contains $normalized -or
            $normalized -match "requires[-_ ](?:audit|configure|review)" -or
            $normalized -match "(?:design-only|runtime-selected|template-not-for-installation)" -or
            $normalized -match "(^|[/:])latest([/:?]|$)" ) {
            throw "$Path contains a release placeholder: $text"
        }
        return
    }

    if ($Value -is [System.Management.Automation.PSCustomObject]) {
        foreach ($property in $Value.PSObject.Properties) {
            Assert-PackagingConcreteValues -Value $property.Value -Path "$Path.$($property.Name)"
        }
        return
    }

    if ($Value -is [System.Collections.IEnumerable]) {
        $index = 0
        foreach ($item in $Value) {
            Assert-PackagingConcreteValues -Value $item -Path "$Path[$index]"
            $index++
        }
    }
}

function Assert-ApprovedPackagingLock {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][AllowNull()][object] $Lock,
        [Parameter(Mandatory = $true)][string] $Label
    )

    if ($null -eq $Lock -or $null -eq $Lock.status) {
        throw "$Label must declare status=approved."
    }
    if ([string]$Lock.status -cne "approved") {
        throw "$Label is not approved (status=$($Lock.status)). Release staging is fail-closed."
    }
    if ($null -eq $Lock.schemaVersion -or [int]$Lock.schemaVersion -ne 1) {
        throw "$Label has an unsupported schemaVersion."
    }

    Assert-PackagingConcreteValues -Value $Lock -Path $Label
}

function Get-PackagingFullPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Label,
        [ValidateSet("Any", "File", "Directory")][string] $Kind = "Any",
        [switch] $MustExist
    )

    if ([string]::IsNullOrWhiteSpace($Path) -or -not [IO.Path]::IsPathFullyQualified($Path)) {
        throw "$Label must be an absolute path."
    }

    $full = [IO.Path]::GetFullPath($Path)
    if ($MustExist -and -not (Test-Path -LiteralPath $full)) {
        throw "$Label does not exist: $full"
    }
    if ($Kind -eq "File" -and (Test-Path -LiteralPath $full -PathType Container)) {
        throw "$Label must be a file: $full"
    }
    if ($Kind -eq "Directory" -and (Test-Path -LiteralPath $full -PathType Leaf)) {
        throw "$Label must be a directory: $full"
    }

    if (Test-Path -LiteralPath $full) {
        return (Get-Item -LiteralPath $full -Force).FullName
    }
    return $full
}

function Assert-PackagingSafePath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Label
    )

    $full = Get-PackagingFullPath -Path $Path -Label $Label
    $root = [IO.Path]::GetPathRoot($full)
    if ([string]::Equals($full.TrimEnd('\', '/'), $root.TrimEnd('\', '/'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must not be a filesystem root: $full"
    }

    $cursor = $full
    while ($true) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Label or one of its ancestors is a reparse point: $cursor"
            }
        }

        $parent = [IO.Directory]::GetParent($cursor)
        if ($null -eq $parent -or [string]::Equals($parent.FullName, $cursor, [StringComparison]::OrdinalIgnoreCase)) {
            break
        }
        $cursor = $parent.FullName
    }
}

function Test-PackagingPathWithin {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Root
    )

    $pathFull = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    return [string]::Equals($pathFull, $rootFull, [StringComparison]::OrdinalIgnoreCase) -or
        $pathFull.StartsWith("$rootFull\", [StringComparison]::OrdinalIgnoreCase) -or
        $pathFull.StartsWith("$rootFull/", [StringComparison]::OrdinalIgnoreCase)
}

function Assert-PackagingDisjointPaths {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $Left,
        [Parameter(Mandatory = $true)][string] $Right,
        [Parameter(Mandatory = $true)][string] $Label
    )

    if ((Test-PackagingPathWithin -Path $Left -Root $Right) -or
        (Test-PackagingPathWithin -Path $Right -Root $Left)) {
        throw "$Label paths must be disjoint: $Left and $Right"
    }
}

function Get-PackagingRelativePath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $Path
    )

    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $pathFull = [IO.Path]::GetFullPath($Path)
    if (-not (Test-PackagingPathWithin -Path $pathFull -Root $rootFull) -or
        [string]::Equals($pathFull.TrimEnd('\', '/'), $rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside its packaging root: $Path"
    }

    $relative = $pathFull.Substring($rootFull.Length).TrimStart('\', '/').Replace('\', '/')
    if ([string]::IsNullOrWhiteSpace($relative) -or $relative -match "(^|/)\.\.?(/|$)" -or
        $relative.StartsWith("/", [StringComparison]::Ordinal)) {
        throw "Unsafe relative packaging path: $relative"
    }
    return $relative
}

function Assert-PackagingRelativePattern {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $Pattern,
        [Parameter(Mandatory = $true)][string] $Label
    )

    $normalized = $Pattern.Replace('\', '/')
    if ([string]::IsNullOrWhiteSpace($normalized) -or $normalized.StartsWith('/') -or
        $normalized.Contains(':') -or $normalized -match "(^|/)\.\.?(/|$)") {
        throw "$Label contains an unsafe relative pattern: $Pattern"
    }
}

function Test-PackagingAllowlistedPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $RelativePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]] $Patterns
    )

    $normalized = $RelativePath.Replace('\', '/')
    foreach ($pattern in $Patterns) {
        if ($normalized -like $pattern.Replace('\', '/')) {
            return $true
        }
    }
    return $false
}

function Test-PackagingDeniedPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $RelativePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]] $DenyNames,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]] $DenyExtensions
    )

    $normalized = $RelativePath.Replace('\', '/').ToLowerInvariant()
    foreach ($name in $DenyNames) {
        $pattern = $name.Replace('\', '/').ToLowerInvariant()
        if ($normalized -like "*$pattern*") {
            return $true
        }
    }
    foreach ($extension in $DenyExtensions) {
        if ($normalized.EndsWith($extension.ToLowerInvariant(), [StringComparison]::Ordinal)) {
            return $true
        }
    }
    return $false
}

function Get-PackagingFileSetDigest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]] $Entries
    )

    $canonical = ($Entries | ForEach-Object {
        "$($_.path)|$($_.bytes)|$($_.sha256.ToLowerInvariant())"
    }) -join "`n"
    $bytes = [Text.Encoding]::UTF8.GetBytes($canonical)
    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        return [Convert]::ToHexString($hash.ComputeHash($bytes)).ToLowerInvariant()
    } finally {
        $hash.Dispose()
    }
}

function Get-PackagingStringSha256 {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string] $Value)

    $bytes = [Text.Encoding]::UTF8.GetBytes($Value)
    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        return [Convert]::ToHexString($hash.ComputeHash($bytes)).ToLowerInvariant()
    } finally {
        $hash.Dispose()
    }
}

function New-PackagingDeterministicUuid {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string] $Seed)

    $bytes = [Text.Encoding]::UTF8.GetBytes("moonlit-cyclonedx-v1:`n$Seed")
    $hash = [Security.Cryptography.SHA1]::Create()
    try {
        $digest = $hash.ComputeHash($bytes)
    } finally {
        $hash.Dispose()
    }

    # RFC 4122 version 5 and variant bits make the deterministic value a
    # standards-shaped UUID rather than an opaque project-specific token.
    $digest[6] = ($digest[6] -band 0x0f) -bor 0x50
    $digest[8] = ($digest[8] -band 0x3f) -bor 0x80
    $hex = [Convert]::ToHexString($digest, 0, 16).ToLowerInvariant()
    return "$($hex.Substring(0, 8))-$($hex.Substring(8, 4))-$($hex.Substring(12, 4))-$($hex.Substring(16, 4))-$($hex.Substring(20, 12))"
}

function Write-PackagingJson {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][AllowNull()][object] $Value
    )

    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $json = $Value | ConvertTo-Json -Depth 100
    [IO.File]::WriteAllText($Path, "$json`n", [Text.UTF8Encoding]::new($false))
}

function Assert-PackagingHash {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $Hash,
        [Parameter(Mandatory = $true)][string] $Label
    )

    if ($Hash -notmatch '^[0-9a-fA-F]{64}$') {
        throw "$Label is not a SHA-256 value: $Hash"
    }
}

function Assert-PackagingVersion {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string] $Version)

    if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$') {
        throw "Release version is not a deterministic semantic version: $Version"
    }
}

function Assert-PackagingSourceSha {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string] $SourceSha)

    if ($SourceSha -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Source SHA must be the full 40-character Git SHA: $SourceSha"
    }
}

function Assert-PackagingSourceContext {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $RepositoryRoot,
        [Parameter(Mandatory = $true)][string] $SourceSha,
        [Parameter(Mandatory = $true)][ValidateSet("clean")][string] $WorktreeStatus
    )

    Assert-PackagingSourceSha -SourceSha $SourceSha
    $gitCommand = Get-Command -Name "git" -CommandType Application -ErrorAction Stop
    $gitPath = $gitCommand.Source
    $actualSha = (& $gitPath -C $RepositoryRoot rev-parse HEAD 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $actualSha -ine $SourceSha) {
        throw "Manifest source SHA does not match the repository HEAD: expected $SourceSha, got $actualSha"
    }
    $status = (& $gitPath -C $RepositoryRoot status --porcelain --untracked-files=all 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to determine repository worktree status."
    }
    if ($WorktreeStatus -ceq "clean" -and -not [string]::IsNullOrWhiteSpace($status)) {
        throw "A clean release manifest cannot be generated from a dirty worktree."
    }
}

function Assert-PackagingSignature {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $File,
        [Parameter(Mandatory = $true)][string] $SignTool,
        [Parameter(Mandatory = $true)][string] $CertificateThumbprint
    )

    $normalizedThumbprint = $CertificateThumbprint.Replace(' ', '').Trim()
    if ($normalizedThumbprint -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Certificate thumbprint must be a 40-character SHA-1 thumbprint."
    }

    & $SignTool verify /pa /all /q $File *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "Authenticode verification failed: $File"
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $File
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "Authenticode status is not Valid for ${File}: $($signature.Status)"
    }
    if ($null -eq $signature.SignerCertificate) {
        throw "Authenticode signer certificate is missing: $File"
    }
    $actual = $signature.SignerCertificate.Thumbprint.Replace(' ', '').ToUpperInvariant()
    if ($actual -ne $normalizedThumbprint.ToUpperInvariant()) {
        throw "Unexpected Authenticode signer for $File. Expected thumbprint $CertificateThumbprint, got $actual."
    }
}
