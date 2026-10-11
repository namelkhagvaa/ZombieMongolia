# Tripo3D-ээс гаргасан GLB загварыг тоглоомд бэлтгэнэ:
#   assets\<name>.glb  ->  assets\<name>_mesh.glb   (зөвхөн mesh ба яс; текстургүй тул хурдан ачаална)
#                          assets\<name>_color.png  (үндсэн өнгөний текстур, 1024 px хүртэл жижигрүүлсэн)
# raylib нь анхдагч тохиргоогоороо JPEG уншдаггүй тул текстурыг PNG болгож тусад нь хадгална.
#
# Хэрэглээ:  powershell -ExecutionPolicy Bypass -File tools\prepare_character.ps1 fighter1
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$TextureSize = 1024
)

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$assets = Join-Path $root "assets"
$source = Join-Path $assets "$Name.glb"
if (-not (Test-Path $source)) { throw "Not found: $source" }

$bytes = [IO.File]::ReadAllBytes($source)
if ([BitConverter]::ToUInt32($bytes, 0) -ne 0x46546C67) { throw "Not a GLB file: $source" }

$jsonLength = [BitConverter]::ToUInt32($bytes, 12)
$json = [Text.Encoding]::UTF8.GetString($bytes, 20, $jsonLength) | ConvertFrom-Json
$binStart = 20 + $jsonLength + 8

# --- Үндсэн өнгөний зураг ---
$material = $json.materials[0]
$textureIndex = $material.pbrMetallicRoughness.baseColorTexture.index
$image = $json.images[$json.textures[$textureIndex].source]
$view = $json.bufferViews[$image.bufferView]
$offset = 0
if ($null -ne $view.byteOffset) { $offset = $view.byteOffset }

Add-Type -AssemblyName System.Drawing
$stream = New-Object IO.MemoryStream($bytes, ($binStart + $offset), $view.byteLength)
$original = [Drawing.Image]::FromStream($stream)
$size = [Math]::Min($TextureSize, $original.Width)
$resized = New-Object Drawing.Bitmap($size, $size)
$graphics = [Drawing.Graphics]::FromImage($resized)
$graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$graphics.DrawImage($original, 0, 0, $size, $size)
$texturePath = Join-Path $assets "${Name}_color.png"
$resized.Save($texturePath, [Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose(); $resized.Dispose(); $original.Dispose(); $stream.Dispose()
"texture: $($image.mimeType) -> $texturePath ($size px)"

# --- Mesh ба яс: зурагт хамаарахгүй buffer view-үүдийг шинэ buffer-т хуулна ---
$imageViews = @{}
foreach ($entry in $json.images) { if ($null -ne $entry.bufferView) { $imageViews[[int]$entry.bufferView] = $true } }

$output = New-Object IO.MemoryStream
$remap = @{}
$newViews = @()
for ($i = 0; $i -lt $json.bufferViews.Count; $i++) {
    if ($imageViews.ContainsKey($i)) { continue }
    $v = $json.bufferViews[$i]
    $start = 0
    if ($null -ne $v.byteOffset) { $start = $v.byteOffset }
    while ($output.Length % 4 -ne 0) { $output.WriteByte(0) }

    $copy = [ordered]@{ buffer = 0; byteOffset = [int]$output.Length; byteLength = $v.byteLength }
    if ($null -ne $v.byteStride) { $copy.byteStride = $v.byteStride }
    if ($null -ne $v.target) { $copy.target = $v.target }
    $output.Write($bytes, $binStart + $start, $v.byteLength)
    $remap[$i] = $newViews.Count
    $newViews += $copy
}
while ($output.Length % 4 -ne 0) { $output.WriteByte(0) }

foreach ($accessor in $json.accessors) { $accessor.bufferView = $remap[[int]$accessor.bufferView] }
$json.bufferViews = $newViews
$json.buffers = @(@{ byteLength = [int]$output.Length })
$json.materials = @(@{ name = "character"; doubleSided = $true })
$json.PSObject.Properties.Remove("images")
$json.PSObject.Properties.Remove("textures")
$json.PSObject.Properties.Remove("samplers")

$jsonBytes = [Text.Encoding]::UTF8.GetBytes(($json | ConvertTo-Json -Depth 64 -Compress))
$padding = (4 - $jsonBytes.Length % 4) % 4
$bin = $output.ToArray()

$meshPath = Join-Path $assets "${Name}_mesh.glb"
$file = [IO.File]::Create($meshPath)
$writer = New-Object IO.BinaryWriter($file)
$writer.Write([uint32]0x46546C67); $writer.Write([uint32]2)
$writer.Write([uint32](12 + 8 + $jsonBytes.Length + $padding + 8 + $bin.Length))
$writer.Write([uint32]($jsonBytes.Length + $padding)); $writer.Write([uint32]0x4E4F534A)
$writer.Write($jsonBytes); for ($i = 0; $i -lt $padding; $i++) { $writer.Write([byte]0x20) }
$writer.Write([uint32]$bin.Length); $writer.Write([uint32]0x004E4942)
$writer.Write($bin)
$writer.Dispose(); $file.Dispose()
"mesh: $meshPath ($([int]((Get-Item $meshPath).Length / 1024)) KB)"
