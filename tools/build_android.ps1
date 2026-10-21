# Zombie Mongolia-г Android APK болгон бүтээнэ.
#
# Шаардлага (C:\Android дотор; өөр газар бол -AndroidRoot заана):
#   jdk\                      Java 17 (keytool, apksigner-т)
#   sdk\build-tools\34.0.0    aapt, zipalign, apksigner
#   sdk\platforms\android-34  android.jar
#   sdk\ndk\26.3.11579264     C++ хөрвүүлэгч
#
# Хэрэглээ:  powershell -ExecutionPolicy Bypass -File tools\build_android.ps1
# Үр дүн:    build_android\ZombieMongolia.apk
param(
    [string]$AndroidRoot = "C:\Android",
    [string[]]$Abis = @("arm64-v8a", "armeabi-v7a"),
    [string]$NdkVersion = "26.3.11579264",
    [string]$BuildTools = "34.0.0",
    [string]$Platform = "android-34"
)

# Continue: PowerShell 5.1 нь гаралтыг дамжуулахад cmake-ийн stderr мөрийг алдаа гэж үздэг; алдааг Run шалгана.
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$out = Join-Path $root "build_android"
$sdk = Join-Path $AndroidRoot "sdk"
$ndk = Join-Path $sdk "ndk\$NdkVersion"
$tools = Join-Path $sdk "build-tools\$BuildTools"
$androidJar = Join-Path $sdk "platforms\$Platform\android.jar"
$env:JAVA_HOME = Join-Path $AndroidRoot "jdk"
$env:PATH = "$env:JAVA_HOME\bin;C:\msys64\ucrt64\bin;$env:PATH"

foreach ($path in $ndk, $tools, $androidJar, $env:JAVA_HOME) {
    if (-not (Test-Path $path)) { throw "Missing: $path" }
}

function Run([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}

# --- 1. Native сан: ABI бүрд хөрвүүлнэ ---
$staging = Join-Path $out "apk"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force (Join-Path $staging "assets") | Out-Null

foreach ($abi in $Abis) {
    # Хөрвүүлэлтийн хавтас git-ээс гадуур (.gitignore: cmake-build-*): build_android
ative-* нь өөр замд тохируулагдсан байсан
    $build = Join-Path $root "cmake-build-android-$abi"
    Run "cmake" @("-S", $root, "-B", $build, "-G", "Ninja",
                  "-DCMAKE_TOOLCHAIN_FILE=$ndk\build\cmake\android.toolchain.cmake",
                  "-DANDROID_ABI=$abi", "-DANDROID_PLATFORM=android-24", "-DANDROID_STL=c++_static",
                  "-DCMAKE_BUILD_TYPE=Release")
    Run "cmake" @("--build", $build)

    $libDir = Join-Path $staging "lib\$abi"
    New-Item -ItemType Directory -Force $libDir | Out-Null
    Copy-Item (Join-Path $build "libZombieMongolia.so") $libDir
    Run "$ndk\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe" @("--strip-unneeded", (Join-Path $libDir "libZombieMongolia.so"))
}

# --- 2. Assets: дүрүүдийн загвар, текстур, фонт ---
Copy-Item (Join-Path $root "assets\*_mesh.glb") (Join-Path $staging "assets")
Copy-Item (Join-Path $root "assets\*_color.png") (Join-Path $staging "assets")
Copy-Item (Join-Path $root "android\ui_font.ttf") (Join-Path $staging "assets")
Copy-Item (Join-Path $root "assets\home_back.png") (Join-Path $staging "assets")
Copy-Item (Join-Path $root "assets\splash.png") (Join-Path $staging "assets")
New-Item -ItemType Directory -Force (Join-Path $staging "assets\icons") | Out-Null
Copy-Item (Join-Path $root "assets\icons\*.png") (Join-Path $staging "assets\icons")
# HUD-ийн иконууд
New-Item -ItemType Directory -Force (Join-Path $staging "assets\huds") | Out-Null
Copy-Item (Join-Path $root "assets\huds\*.png") (Join-Path $staging "assets\huds")
# Газрын текстурууд (CC0)
New-Item -ItemType Directory -Force (Join-Path $staging "assets\terrain") | Out-Null
Copy-Item (Join-Path $root "assets\terrain\*.png") (Join-Path $staging "assets\terrain")
# Унааны загварууд: бэлтгэсэн .vmesh ба текстур л (эх .glb хэт том)
New-Item -ItemType Directory -Force (Join-Path $staging "assets\cars") | Out-Null
Copy-Item (Join-Path $root "assets\cars\*.vmesh") (Join-Path $staging "assets\cars")
Copy-Item (Join-Path $root "assets\cars\*_color.png") (Join-Path $staging "assets\cars")
New-Item -ItemType Directory -Force (Join-Path $staging "assets\buu") | Out-Null
Copy-Item (Join-Path $root "assets\buu\*.vmesh") (Join-Path $staging "assets\buu")
Copy-Item (Join-Path $root "assets\buu\*_color.png") (Join-Path $staging "assets\buu")
# Улаанбаатарын төвийн газрын зураг (OpenStreetMap)
New-Item -ItemType Directory -Force (Join-Path $staging "assets\city") | Out-Null
Copy-Item (Join-Path $root "assets\city\ub.bin") (Join-Path $staging "assets\city")
New-Item -ItemType Directory -Force (Join-Path $staging "assets\sounds") | Out-Null
Copy-Item (Join-Path $root "assets\sounds\*.wav") (Join-Path $staging "assets\sounds")

# --- 3. APK: багцлах, native санг нэмэх, зэрэгцүүлэх, гарын үсэг зурах ---
$unsigned = Join-Path $out "unsigned.apk"
$aligned = Join-Path $out "aligned.apk"
$apk = Join-Path $out "ZombieMongolia.apk"
Remove-Item -Force -ErrorAction SilentlyContinue $unsigned, $aligned, $apk

Run "$tools\aapt.exe" @("package", "-f", "-M", (Join-Path $root "android\AndroidManifest.xml"),
                        "-S", (Join-Path $root "android\res"),
                        "-A", (Join-Path $staging "assets"), "-I", $androidJar, "-F", $unsigned)

Push-Location $staging
try {
    foreach ($abi in $Abis) {
        Run "$tools\aapt.exe" @("add", $unsigned, "lib/$abi/libZombieMongolia.so")
    }
} finally {
    Pop-Location
}

Run "$tools\zipalign.exe" @("-f", "-p", "4", $unsigned, $aligned)

# Туршилтын түлхүүр (нэг удаа үүснэ). Play Store-д гаргахад өөрийн нууц түлхүүрээр солино.
$keystore = Join-Path $out "debug.keystore"
if (-not (Test-Path $keystore)) {
    Run "keytool" @("-genkeypair", "-keystore", $keystore, "-storepass", "android", "-keypass", "android",
                    "-alias", "debug", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
                    "-dname", "CN=Zombie Mongolia Debug,O=ZombieMongolia,C=MN")
}
Run "$tools\apksigner.bat" @("sign", "--ks", $keystore, "--ks-pass", "pass:android", "--key-pass", "pass:android",
                             "--out", $apk, $aligned)
Run "$tools\apksigner.bat" @("verify", $apk)

Remove-Item -Force $unsigned, $aligned
"APK: $apk ($([int]((Get-Item $apk).Length / 1MB)) MB)"
