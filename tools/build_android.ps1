# Zombie Mongolia-г Android APK болгон бүтээнэ.
#
# Шаардлага (C:\Android дотор; байхгүй бол Android Studio-ийн SDK ба jbr-ийг хэрэглэнэ):
#   jdk\                      Java 17+ (keytool, apksigner-т)
#   sdk\build-tools\<хувилбар> aapt, zipalign, apksigner (хамгийн шинийг сонгоно)
#   sdk\platforms\android-<N>  android.jar (хамгийн шинийг сонгоно)
#   sdk\ndk\26.3.11579264     C++ хөрвүүлэгч
#
# Хэрэглээ:  powershell -ExecutionPolicy Bypass -File tools\build_android.ps1
# Үр дүн:    build_android\ZombieMongolia.apk
param(
    [string]$AndroidRoot = "C:\Android",
    [string[]]$Abis = @("arm64-v8a", "armeabi-v7a"),
    [string]$NdkVersion = "26.3.11579264",
    [string]$BuildTools = "",  # хоосон: хамгийн шинэ
    [string]$Platform = "",    # хоосон: хамгийн шинэ
    [string]$Sdk = "",         # хоосон: C:\Android\sdk, байхгүй бол Android Studio-ийнх (%LOCALAPPDATA%\Android\Sdk)
    [string]$Jdk = ""          # хоосон: C:\Android\jdk, байхгүй бол Android Studio-ийн jbr
)

# Continue: PowerShell 5.1 нь гаралтыг дамжуулахад cmake-ийн stderr мөрийг алдаа гэж үздэг; алдааг Run шалгана.
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$out = Join-Path $root "build_android"
$sdk = if ($Sdk) { $Sdk } elseif (Test-Path (Join-Path $AndroidRoot "sdk")) { Join-Path $AndroidRoot "sdk" } else { Join-Path $env:LOCALAPPDATA "Android\Sdk" }
$jdk = if ($Jdk) { $Jdk } elseif (Test-Path (Join-Path $AndroidRoot "jdk")) { Join-Path $AndroidRoot "jdk" } else { "C:\Program Files\Android\Android Studio\jbr" }
if (-not $BuildTools) {
    $BuildTools = (Get-ChildItem (Join-Path $sdk "build-tools") -Directory | Sort-Object { [version]($_.Name -replace '[^0-9.]', '') } | Select-Object -Last 1).Name
}
if (-not $Platform) {
    $Platform = (Get-ChildItem (Join-Path $sdk "platforms") -Directory | Sort-Object { [double](($_.Name -replace 'android-', '') -replace '[^0-9.]', '') } | Select-Object -Last 1).Name
}
$ndk = Join-Path $sdk "ndk\$NdkVersion"
$tools = Join-Path $sdk "build-tools\$BuildTools"
$androidJar = Join-Path $sdk "platforms\$Platform\android.jar"
$env:JAVA_HOME = $jdk
"SDK: $sdk (build-tools $BuildTools, $Platform), NDK $NdkVersion, Java: $jdk"
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
    # \native-* нь өөр замд тохируулагдсан байсан
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
# Байшин, зам, байгалийн бүтэц (CC0, саарал JPG: tools/prep_surfaces.py)
New-Item -ItemType Directory -Force (Join-Path $staging "assets\surfaces") | Out-Null
Copy-Item (Join-Path $root "assets\surfaces\*.jpg") (Join-Path $staging "assets\surfaces")
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
# Мод, торх (ойрын зайд): бэлтгэсэн .vmesh ба текстур л
foreach ($sub in @("trees", "pooshig")) {
    New-Item -ItemType Directory -Force (Join-Path $staging "assets\$sub") | Out-Null
    Copy-Item (Join-Path $root "assets\$sub\*.vmesh") (Join-Path $staging "assets\$sub")
    Copy-Item (Join-Path $root "assets\$sub\*_color.png") (Join-Path $staging "assets\$sub")
    Copy-Item (Join-Path $root "assets\$sub\*_far.bin") (Join-Path $staging "assets\$sub") -ErrorAction SilentlyContinue
}
# Улаанбаатарын төвийн газрын зураг (OpenStreetMap)
New-Item -ItemType Directory -Force (Join-Path $staging "assets\city") | Out-Null
Copy-Item (Join-Path $root "assets\city\*.bin") (Join-Path $staging "assets\city")
New-Item -ItemType Directory -Force (Join-Path $staging "assets\sounds") | Out-Null
Copy-Item (Join-Path $root "assets\sounds\*.wav") (Join-Path $staging "assets\sounds")
# Мэдэгдлийн дуу хоолой (tools/gen_narration.py)
New-Item -ItemType Directory -Force (Join-Path $staging "assets\sounds\narration") | Out-Null
Copy-Item (Join-Path $root "assets\sounds\narration\*.wav") (Join-Path $staging "assets\sounds\narration") -ErrorAction SilentlyContinue
# Том текстурууд PNG -> JPG (APK ~70 MB багасна; huds, icons PNG хэвээр)
Run "python" @((Join-Path $root "tools\native_jpg.py"), (Join-Path $staging "assets"))

# --- 3. APK: багцлах, native санг нэмэх, зэрэгцүүлэх, гарын үсэг зурах ---
$unsigned = Join-Path $out "unsigned.apk"
$aligned = Join-Path $out "aligned.apk"
$apk = Join-Path $out "ZombieMongolia.apk"
Remove-Item -Force -ErrorAction SilentlyContinue $unsigned, $aligned, $apk

# Java: утасны гар (android\java\...\GameActivity.java) -> classes.dex
$javaOut = Join-Path $out "java"
if (Test-Path $javaOut) { Remove-Item -Recurse -Force $javaOut }
New-Item -ItemType Directory -Force (Join-Path $javaOut "classes"), (Join-Path $javaOut "dex") | Out-Null
$javaSources = @(Get-ChildItem (Join-Path $root "android\java") -Recurse -Filter *.java | ForEach-Object { $_.FullName })
Run "javac" (@("-source", "8", "-target", "8", "-nowarn", "-encoding", "UTF-8", "-classpath", $androidJar,
               "-d", (Join-Path $javaOut "classes")) + $javaSources)
$classFiles = @(Get-ChildItem (Join-Path $javaOut "classes") -Recurse -Filter *.class | ForEach-Object { $_.FullName })
Run "$tools\d8.bat" (@("--min-api", "24", "--lib", $androidJar, "--output", (Join-Path $javaOut "dex")) + $classFiles)

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
Push-Location (Join-Path $javaOut "dex")
try {
    Run "$tools\aapt.exe" @("add", $unsigned, "classes.dex")
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
# Вэб сайтаас татах (тоглоом шинэ хувилбарыг download\android-version.txt-ээс шалгаж "ТАТАХ" товч гаргана):
# build_web\web\download\ хавтсыг вэб сайттай хамт байршуулна
$download = Join-Path $root "build_web\web\download"
New-Item -ItemType Directory -Force $download | Out-Null
Copy-Item $apk $download -Force
$stamp = [regex]::Match((Get-Content -Raw (Join-Path $root "cmake-build-android-arm64-v8a\generated\BuildStampValue.h")), '"([^"]+)"').Groups[1].Value
[System.IO.File]::WriteAllText((Join-Path $download "android-version.txt"), $stamp, (New-Object System.Text.UTF8Encoding $false))
"APK: $apk ($([int]((Get-Item $apk).Length / 1MB)) MB)"
