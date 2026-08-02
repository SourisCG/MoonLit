# Replicates upstream SQLite tool/mkfts5c.tcl (3.53.4) to assemble the
# single-file fts5.c amalgamation from the split fts5 sources.
# Usage: pwsh -NoProfile -File make_fts5c.ps1 -SourceDir <fts5 dir> -OutFile <fts5.c>
# The generated fts5.c is committed so the build needs no tclsh.

param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$OutFile
)

$ErrorActionPreference = 'Stop'

$sourceId = 'fts5: 2026-07-24 19:02:57 bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc'

$files = @(
    'fts5.h',
    'fts5Int.h',
    'fts5parse.h',
    'fts5parse.c',
    'fts5_aux.c',
    'fts5_buffer.c',
    'fts5_config.c',
    'fts5_expr.c',
    'fts5_hash.c',
    'fts5_index.c',
    'fts5_main.c',
    'fts5_storage.c',
    'fts5_tokenize.c',
    'fts5_unicode2.c',
    'fts5_varint.c',
    'fts5_vocab.c'
)

$header = @'
/*
** This, the "fts5.c" source file, is a composite file that is itself
** assembled from the following files:
**
**    fts5.h
**    fts5Int.h
**    fts5parse.h          <--- Generated from fts5parse.y by Lemon
**    fts5parse.c          <--- Generated from fts5parse.y by Lemon
**    fts5_aux.c
**    fts5_buffer.c
**    fts5_config.c
**    fts5_expr.c
**    fts5_hash.c
**    fts5_index.c
**    fts5_main.c
**    fts5_storage.c
**    fts5_tokenize.c
**    fts5_unicode2.c
**    fts5_varint.c
**    fts5_vocab.c
*/
#if !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS5) 

#if !defined(NDEBUG) && !defined(SQLITE_DEBUG) 
# define NDEBUG 1
#endif
#if defined(NDEBUG) && defined(SQLITE_DEBUG)
# undef NDEBUG
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
'@

$footer = @'
/* Here ends the fts5.c composite file. */
#endif /* !defined(SQLITE_CORE) || defined(SQLITE_ENABLE_FTS5) */
'@

function Get-Fts5Lines([string]$Path, [string]$Tail, [string]$SourceId) {
    $lines = Get-Content -LiteralPath $Path
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^#include.*fts5') {
            $line = "/* $line */"
        } elseif ($line -notmatch ' sqlite3Fts5Init\(' -and $line -match '^(const )?[a-zA-Z][a-zA-Z0-9]* [*]?sqlite3Fts5') {
            $line = "static $line"
        }
        $line = $line.Replace('--FTS5-SOURCE-ID--', $SourceId)
        if ($Tail -eq 'fts5parse.c') {
            $line = $line.Replace('yy', 'fts5yy').Replace('YY', 'fts5YY').Replace('TOKEN', 'FTS5TOKEN')
        }
        $line
    }
}

$lines = [System.Collections.Generic.List[string]]::new()
foreach ($part in $header -split "`r?`n") { $lines.Add($part) }
foreach ($file in $files) {
    $lines.Add("#line 1 `"$file`"")
    foreach ($line in Get-Fts5Lines -Path (Join-Path $SourceDir $file) -Tail $file -SourceId $sourceId) {
        $lines.Add($line)
    }
}
foreach ($part in $footer -split "`r?`n") { $lines.Add($part) }

Set-Content -LiteralPath $OutFile -Value $lines -Encoding utf8NoBOM
Write-Host "wrote $OutFile ($($lines.Count) lines)"
