# Zombie Mongolia-гийн Windows суулгагчийг бүтээнэ: exe-г шинээр build хийж, Inno Setup-аар багцална.
#
# Шаардлага: Inno Setup 6 (winget install JRSoftware.InnoSetup), MSYS2 ucrt64 (cmake, ninja, g++)
# Хэрэглээ:  powershell -ExecutionPolicy Bypass -File tools\build_installer.ps1
# Үр дүн:    dist\ZombieMongolia_Setup.exe
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"

# exe-г cmake-build-release-д build хийж, суулгагчийн авдаг build\ хавтас руу хуулна
# (build\ хавтасны CMakeCache өөр замаас үүссэн тул тэнд шууд build хийж болохгүй).
cmake --build (Join-Path $root "cmake-build-release")
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
Copy-Item (Join-Path $root "cmake-build-release\ZombieMongolia.exe") (Join-Path $root "build\ZombieMongolia.exe") -Force

$iscc = @(
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 олдсонгүй: winget install JRSoftware.InnoSetup" }

& $iscc (Join-Path $root "installer\ZombieMongolia.iss")
if ($LASTEXITCODE -ne 0) { throw "Installer build failed" }
# Вэб сайтаас татах (тоглоом шинэ хувилбарыг download\windows-version.txt-ээс шалгаж "ТАТАХ" товч гаргана):
# build_web\web\download\ хавтсыг вэб сайттай хамт байршуулна
$download = Join-Path $root "build_web\web\download"
New-Item -ItemType Directory -Force $download | Out-Null
Copy-Item (Join-Path $root "dist\ZombieMongolia_Setup.exe") $download -Force
$stamp = [regex]::Match((Get-Content -Raw (Join-Path $root "cmake-build-release\generated\BuildStampValue.h")), '"([^"]+)"').Groups[1].Value
[System.IO.File]::WriteAllText((Join-Path $download "windows-version.txt"), $stamp, (New-Object System.Text.UTF8Encoding $false))
Get-Item (Join-Path $root "dist\ZombieMongolia_Setup.exe")
