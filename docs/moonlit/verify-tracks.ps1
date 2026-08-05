param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

<#
.SYNOPSIS
Verifies that the four MoonLit audio tracks carry distinct signals.

.DESCRIPTION
Extracts every audio stream of a saved MKV clip to WAV with ffmpeg and
prints the RMS level of each track. Track layout produced by MoonLit:
1 mixed, 2 game only, 3 microphone only, 4 chat only. A row with a
meaningfully different RMS from the others is evidence of a distinct
signal; near-silence (very low RMS) indicates a missing source.

.INPUTS
Path to a MoonLit clip (MKV with 4 audio tracks).

.EXAMPLE
pwsh -NoProfile -File verify-tracks.ps1 -Path "C:\Users\sebas\Videos\Replay 2026-08-02 18-57-52.mkv"
#>

$ErrorActionPreference = 'Stop'

$ffprobe = Get-Command ffprobe -ErrorAction SilentlyContinue
$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if (-not $ffprobe -or -not $ffmpeg) {
    Write-Error "ffmpeg/ffprobe not found on PATH"
}

if (-not (Test-Path -LiteralPath $Path)) {
    Write-Error "File not found: $Path"
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("moonlit-tracks-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null

try {
    $info = & $ffprobe.Source -v error -show_entries stream=index,codec_type,codec_name,channels -of csv $Path
    $audioStreams = $info | Where-Object { $_ -match '^stream,' -and $_ -match ',audio,' }

    if (-not $audioStreams) {
        Write-Output "No audio streams found in $Path"
        exit 1
    }

    Write-Output "Audio streams in $([System.IO.Path]::GetFileName($Path)):"
    foreach ($line in $audioStreams) {
        Write-Output "  $line"
    }
    Write-Output ""

    $index = 0
    foreach ($line in $audioStreams) {
        $parts = $line -split ','
        $streamIndex = $parts[1]
        $wav = Join-Path $temp ("track-$streamIndex.wav")

        & $ffmpeg.Source -v error -i $Path -map "0:$streamIndex" -ac 1 -ar 48000 -c:a pcm_s16le -y $wav 2>$null
        if (-not (Test-Path $wav)) {
            Write-Output "Track $streamIndex: extraction failed"
            continue
        }

        $probe = & $ffprobe.Source -v error -show_entries stream=sample_rate,nb_frames -select_streams a:0 -of csv $wav
        $duration = & $ffprobe.Source -v error -show_entries format=duration -of csv=p=0 $wav

        # RMS via astats on the mono wav
        $astats = & $ffmpeg.Source -i $wav -af astats=metadata=1:reset=0 -f null - 2>&1
        $rmsLine = $astats | Select-String -Pattern "RMS level dB" | Select-Object -Last 1
        $rmsDb = if ($rmsLine) { ($rmsLine.Line -split '=')[-1].Trim() } else { "?" }

        $peakLine = $astats | Select-String -Pattern "Peak level dB" | Select-Object -Last 1
        $peakDb = if ($peakLine) { ($peakLine.Line -split '=')[-1].Trim() } else { "?" }

        $index++
        Write-Output ("Track {0} (stream {1}): duration={2}s RMS={3} dB Peak={4} dB" -f $index, $streamIndex,
            ([math]::Round([double]$duration, 1)), $rmsDb, $peakDb)
    }

    Write-Output ""
    Write-Output "Interpretation:"
    Write-Output "  - A track with RMS well above silence is carrying a real signal."
    Write-Output "  - If track 2 (game) and track 1 (mixed) have nearly identical RMS,"
    Write-Output "    game audio may be duplicated into the mixed track (row A2)."
    Write-Output "  - Near-silence (-60 dB or below) means that source produced no"
    Write-Output "    signal for that track (missing mic/chat/game)."
} finally {
    Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
