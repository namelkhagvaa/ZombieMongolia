# Zombie Mongolia-г хөтөч дээр тоглох хуудас (WebAssembly + WebGL2) болгон бүтээнэ.
#
# Шаардлага: Emscripten SDK (C:\emsdk; өөр газар бол -Emsdk заана), Ninja (C:\msys64\ucrt64\bin)
#   git clone https://github.com/emscripten-core/emsdk.git C:\emsdk
#   C:\emsdk\emsdk.bat install latest; C:\emsdk\emsdk.bat activate latest
#
# Хэрэглээ:  powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 [-Serve]
# Үр дүн:    build_web\web\ (index.html, ZombieMongolia.js, .wasm, .data): ямар ч статик вэб сервер дээр байршуулна.
# -Serve:    бүтээсний дараа http://localhost:8080 дээр туршихаар ажиллуулна (file:// -ээр нээвэл ажиллахгүй).
param(
    [string]$Emsdk = "C:\emsdk",
    [switch]$Serve
)

# Continue: PowerShell 5.1 нь гаралтыг дамжуулахад cmake-ийн stderr мөрийг алдаа гэж үздэг; алдааг Run шалгана.
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$build = Join-Path $root "build_web"
$env:EMSDK_QUIET = 1
& (Join-Path $Emsdk "emsdk_env.ps1") | Out-Null
$env:PATH = "$env:PATH;C:\msys64\ucrt64\bin"

function Run([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}

# Тохиргоо бүр дахин ажиллана: assets-ийн хуулбарыг шинэчилнэ (CMakeLists.txt-ийн EMSCRIPTEN хэсэг).
Run "emcmake" @("cmake", "-S", $root, "-B", $build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
Run "cmake" @("--build", $build)

# Байршуулах хавтас: index.html нэртэй
$site = Join-Path $build "web"
New-Item -ItemType Directory -Force $site | Out-Null
# index.html-ийг web\shell.html-ээс шууд үүсгэнэ: хуудсыг засахад тоглоомыг дахин холбох (link) шаардлагагүй.
$shell = [System.IO.File]::ReadAllText((Join-Path $root "web\shell.html"))
$page = $shell.Replace("{{{ SCRIPT }}}", '<script async type="text/javascript" src="ZombieMongolia.js"></script>')
[System.IO.File]::WriteAllText((Join-Path $site "index.html"), $page, (New-Object System.Text.UTF8Encoding $false))
foreach ($ext in "js", "wasm", "data") {
    Copy-Item (Join-Path $build "ZombieMongolia.$ext") $site -Force
}
# Хуудасны ачааллын дэлгэц тоглоомынхтой ижил зураг, фонт хэрэглэнэ (web/shell.html)
Copy-Item (Join-Path $root "assets\splash.png") $site -Force
Copy-Item (Join-Path $root "android\ui_font.ttf") $site -Force
$size = (Get-ChildItem $site | Measure-Object -Property Length -Sum).Sum / 1MB
Write-Host ("Done: {0} ({1:N0} MB)" -f $site, $size)

if ($Serve) {
    Write-Host "http://localhost:8080 (Ctrl+C зогсооно)"
    Run $env:EMSDK_PYTHON @("-m", "http.server", "8080", "--directory", $site)
}
