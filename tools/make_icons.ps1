# assets\icon.png-ээс програмын иконуудыг үүсгэнэ:
#   android\res\mipmap-*\ic_launcher.png  - Android launcher икон (48..192 px), булан нь тунгалаг
#   assets\app.ico                         - Windows exe-ийн икон (16..256 px)
#   assets\window_icon.png                 - тоглоомын цонхны икон (128 px)
#
# Хэрэглээ:  powershell -ExecutionPolicy Bypass -File tools\make_icons.ps1
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$source = [Drawing.Image]::FromFile((Join-Path $root "assets\icon.png"))

# Эх зураг нь бөөрөнхий дөрвөлжин хүрээтэй, гадна талын булан нь хар: хүрээний гадна талыг тунгалаг болгоно.
# (Хүрээний ирмэг эх зургийн 1.6% -д, булангийн радиус 9.5% орчим.)
function New-Icon([int]$size) {
    $bitmap = New-Object Drawing.Bitmap($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([Drawing.Color]::Transparent)

    $inset = $size * 0.016
    $extent = $size - 2 * $inset
    $radius = $size * 0.095
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $d = 2 * $radius
    $path.AddArc($inset, $inset, $d, $d, 180, 90)
    $path.AddArc($inset + $extent - $d, $inset, $d, $d, 270, 90)
    $path.AddArc($inset + $extent - $d, $inset + $extent - $d, $d, $d, 0, 90)
    $path.AddArc($inset, $inset + $extent - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    $g.SetClip($path)
    $g.DrawImage($source, 0, 0, $size, $size)
    $g.Dispose()
    $path.Dispose()
    return $bitmap
}

# --- Android launcher иконууд ---
$densities = [ordered]@{ "mdpi" = 48; "hdpi" = 72; "xhdpi" = 96; "xxhdpi" = 144; "xxxhdpi" = 192 }
foreach ($density in $densities.Keys) {
    $dir = Join-Path $root "android\res\mipmap-$density"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $icon = New-Icon $densities[$density]
    $icon.Save((Join-Path $dir "ic_launcher.png"), [Drawing.Imaging.ImageFormat]::Png)
    $icon.Dispose()
}
"android: $($densities.Count) launcher icons"

# --- Цонхны икон ---
$window = New-Icon 128
$window.Save((Join-Path $root "assets\window_icon.png"), [Drawing.Imaging.ImageFormat]::Png)
$window.Dispose()

# --- Windows .ico: хэмжээ бүрийг PNG-ээр шахаж нэг файлд нэгтгэнэ (Vista+ дэмждэг) ---
$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = @()
foreach ($size in $sizes) {
    $icon = New-Icon $size
    $stream = New-Object IO.MemoryStream
    $icon.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
    $images += , $stream.ToArray()
    $icon.Dispose()
    $stream.Dispose()
}
$out = New-Object IO.MemoryStream
$writer = New-Object IO.BinaryWriter($out)
$writer.Write([UInt16]0); $writer.Write([UInt16]1); $writer.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dim = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $writer.Write([Byte]$dim); $writer.Write([Byte]$dim); $writer.Write([Byte]0); $writer.Write([Byte]0)
    $writer.Write([UInt16]1); $writer.Write([UInt16]32)
    $writer.Write([UInt32]$images[$i].Length); $writer.Write([UInt32]$offset)
    $offset += $images[$i].Length
}
foreach ($bytes in $images) { $writer.Write($bytes) }
[IO.File]::WriteAllBytes((Join-Path $root "assets\app.ico"), $out.ToArray())
$writer.Dispose()
"windows: assets\app.ico ($($sizes.Count) sizes), assets\window_icon.png"

$source.Dispose()
