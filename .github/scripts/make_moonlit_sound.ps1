# Synthesizes the short two-tone "clip saved" sound (16-bit PCM mono 44.1 kHz)
# used as feedback when a clip is captured.
# Usage: pwsh -NoProfile -File make_moonlit_sound.ps1 [-OutFile <path>]

param(
    [string]$OutFile = (Join-Path $PSScriptRoot "..\..\frontend\data\obs-studio\sounds\moonlit-clip.wav")
)

$sampleRate = 44100

$samples = [System.Collections.Generic.List[double]]::new()
function Add-Tone([double]$Frequency, [double]$Seconds, [double]$Gain) {
    $count = [int]($Seconds * $sampleRate)
    for ($i = 0; $i -lt $count; $i++) {
        $t = $i / $sampleRate
        $envelope = [math]::Exp(-6.0 * $t / $Seconds)
        $samples.Add($Gain * $envelope * [math]::Sin(2.0 * [math]::PI * $Frequency * $t))
    }
}

Add-Tone 880.0 0.06 0.5
for ($i = 0; $i -lt [int](0.02 * $sampleRate); $i++) { $samples.Add(0.0) }
Add-Tone 1320.0 0.10 0.5

$count = $samples.Count
$bytes = [System.Collections.Generic.List[byte]]::new()
foreach ($s in $samples) {
    $value = [int][math]::Max(-32768, [math]::Min(32767, [math]::Round($s * 32767)))
    $bytes.Add([byte]($value -band 0xFF))
    $bytes.Add([byte](($value -shr 8) -band 0xFF))
}

$dataSize = $count * 2
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
$bw.Write([UInt32](36 + $dataSize))
$bw.Write([System.Text.Encoding]::ASCII.GetBytes("WAVEfmt "))
$bw.Write([UInt32]16)
$bw.Write([UInt16]1)          # PCM
$bw.Write([UInt16]1)          # mono
$bw.Write([UInt32]$sampleRate)
$bw.Write([UInt32]($sampleRate * 2))
$bw.Write([UInt16]2)          # block align
$bw.Write([UInt16]16)         # bits per sample
$bw.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
$bw.Write([UInt32]$dataSize)
$bw.Write($bytes.ToArray())
$bw.Flush()

$dir = Split-Path $OutFile
if (-not (Test-Path $dir)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}
[System.IO.File]::WriteAllBytes($OutFile, $ms.ToArray())
Write-Host "wrote $OutFile ($(($ms.ToArray()).Length) bytes)"
