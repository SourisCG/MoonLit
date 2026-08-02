Add-Type -AssemblyName System.Drawing

function New-RoundedRectPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

$size = 256
$bmp = New-Object System.Drawing.Bitmap $size, $size
$bmp.SetResolution(96, 96)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)

$bg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 27, 30, 37))
$path = New-RoundedRectPath 8 8 240 240 56
$g.FillPath($bg, $path)

$moon = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 242, 211, 92))
$moonPath = New-Object System.Drawing.Drawing2D.GraphicsPath
$moonPath.AddArc(96, 44, 112, 168, 40, 280)
$moonPath.AddArc(120, 72, 96, 112, -140, 280)
$moonPath.CloseFigure()
$g.FillPath($moon, $moonPath)

$g.Dispose()
$pngPath = Join-Path $env:TEMP "moonlit-256.png"
$bmp.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

$pngBytes = [System.IO.File]::ReadAllBytes($pngPath)
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([UInt16]0)          # reserved
$bw.Write([UInt16]1)          # type: icon
$bw.Write([UInt16]1)          # count
$bw.Write([Byte]0)            # width 256 -> 0
$bw.Write([Byte]0)            # height 256 -> 0
$bw.Write([Byte]0)            # palette
$bw.Write([Byte]0)            # reserved
$bw.Write([UInt16]1)          # planes
$bw.Write([UInt16]32)         # bpp
$bw.Write([UInt32]$pngBytes.Length)
$bw.Write([UInt32]22)         # offset
$bw.Write($pngBytes)
$bw.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $PSScriptRoot "MoonLit.ico"), $ms.ToArray())
Write-Host "wrote MoonLit.ico ($($ms.ToArray().Length) bytes)"
