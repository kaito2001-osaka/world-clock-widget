# Packs icons/worldclock-{16,32,48,256}.png into icons/worldclock.ico (#59).
#
# The 16 / 32 / 48 px frames are stored as uncompressed 32-bit DIBs and only
# the 256 px frame as PNG, the usual layout for Vista-and-later icons. When
# LoadImageW picks a PNG frame, user32 decodes it through WIC, which leaves
# WindowsCodecs.dll and IconCodecService.dll loaded for the life of the
# gadget. The gadget asks for SM_CXICON / SM_CXSMICON, which are always
# closer to one of the DIB frames than to 256 px.
#
# Usage: powershell -File tools\build-icon.ps1
# Re-run after changing any of the PNGs; commit the .ico.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$icons = Join-Path (Split-Path -Parent $PSScriptRoot) 'icons'

# One uncompressed frame: BITMAPINFOHEADER, BGRA rows bottom-up, then the
# 1 bpp AND mask. The header's height covers both (XOR + AND), hence 2h.
function ConvertTo-DibFrame([string]$path) {
    $bmp = [System.Drawing.Bitmap]::new($path)
    try {
        $w = $bmp.Width; $h = $bmp.Height
        $rect = [System.Drawing.Rectangle]::new(0, 0, $w, $h)
        # Format32bppArgb is straight (not premultiplied) alpha in BGRA order,
        # which is what an icon's XOR bitmap holds.
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                              [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $pixels = [byte[]]::new($w * $h * 4)
            for ($y = 0; $y -lt $h; $y++) {
                [Runtime.InteropServices.Marshal]::Copy(
                    [IntPtr]::Add($data.Scan0, $y * $data.Stride), $pixels, $y * $w * 4, $w * 4)
            }
        } finally { $bmp.UnlockBits($data) }
    } finally { $bmp.Dispose() }

    $maskStride = [int][Math]::Floor(($w + 31) / 32) * 4
    $ms = [IO.MemoryStream]::new()
    $bw = [IO.BinaryWriter]::new($ms)
    $bw.Write([uint32]40); $bw.Write([int32]$w); $bw.Write([int32]($h * 2))
    $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]0)   # BI_RGB
    $bw.Write([uint32]0); $bw.Write([int32]0); $bw.Write([int32]0)
    $bw.Write([uint32]0); $bw.Write([uint32]0)
    for ($y = $h - 1; $y -ge 0; $y--) { $bw.Write($pixels, $y * $w * 4, $w * 4) }
    # The mask is ignored for 32 bpp frames on current Windows, but older
    # consumers use it: set a bit (transparent) wherever alpha is 0.
    for ($y = $h - 1; $y -ge 0; $y--) {
        $row = [byte[]]::new($maskStride)
        for ($x = 0; $x -lt $w; $x++) {
            if ($pixels[($y * $w + $x) * 4 + 3] -eq 0) { $row[$x -shr 3] = $row[$x -shr 3] -bor (0x80 -shr ($x -band 7)) }
        }
        $bw.Write($row)
    }
    $bw.Flush()
    return , $ms.ToArray()
}

$frames = @(
    @{ Size = 16;  Data = ConvertTo-DibFrame (Join-Path $icons 'worldclock-16.png') }
    @{ Size = 32;  Data = ConvertTo-DibFrame (Join-Path $icons 'worldclock-32.png') }
    @{ Size = 48;  Data = ConvertTo-DibFrame (Join-Path $icons 'worldclock-48.png') }
    @{ Size = 256; Data = [IO.File]::ReadAllBytes((Join-Path $icons 'worldclock-256.png')) }
)

$ms = [IO.MemoryStream]::new()
$bw = [IO.BinaryWriter]::new($ms)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$frames.Count)   # ICONDIR
$offset = 6 + 16 * $frames.Count
foreach ($f in $frames) {                                                      # ICONDIRENTRY
    $dim = if ($f.Size -ge 256) { 0 } else { $f.Size }                         # 0 means 256
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$f.Data.Length); $bw.Write([uint32]$offset)
    $offset += $f.Data.Length
}
foreach ($f in $frames) { $bw.Write($f.Data) }
$bw.Flush()

$out = Join-Path $icons 'worldclock.ico'
[IO.File]::WriteAllBytes($out, $ms.ToArray())
"Wrote $out ($($ms.Length) bytes)"
