# Zombie Mongolia — build хийх заавар

Бүх командыг төслийн үндсэн хавтаснаас (`C:\Dulka\ZombieMongolia`) PowerShell-ээр ажиллуулна.

| Юу | Команд | Үр дүн |
|---|---|---|
| Windows exe | `cmake --build cmake-build-release` | `cmake-build-release\ZombieMongolia.exe` |
| Windows суулгагч | `powershell -ExecutionPolicy Bypass -File tools\build_installer.ps1` | `dist\ZombieMongolia_Setup.exe` |
| Вэб | `powershell -ExecutionPolicy Bypass -File tools\build_web.ps1` | `build_web\web\` |
| Android | `powershell -ExecutionPolicy Bypass -File tools\build_android.ps1` | `build_android\ZombieMongolia.apk` |

---

## 1. Windows exe

**Шаардлага:** MSYS2 (`C:\msys64`), ucrt64 хэрэгслүүд:
```
C:\msys64\usr\bin\bash -lc "pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja"
```
`C:\msys64\ucrt64\bin`-ийг PATH-д нэмнэ.

**Анх удаа (тохиргоо):**
```
cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/c++.exe
```

**Build:**
```
cmake --build cmake-build-release
```
Exe нь `cmake-build-release\ZombieMongolia.exe`. `assets\` хавтастай хамт ажиллана.
Суулгагч `build\ZombieMongolia.exe`-г авдаг. Доорх суулгагчийн скрипт түүнийг өөрөө хуулдаг.

**Турших:** `cmake-build-release\ZombieMongolia.exe`.
Хамтарсан тоглолтыг (тулааныг) данс, сервергүйгээр нэг компьютер дээр турших:
```
cd build
.\ZombieMongolia.exe --province 8 --coop --battle        # хост
.\ZombieMongolia.exe --campaign --coop --battle          # зочин (өөр цонхонд, нэг сүлжээнд)
```

## 2. Windows суулгагч (Setup.exe)

**Шаардлага:** дээрх exe-ийн шаардлага ба Inno Setup 6:
```
winget install JRSoftware.InnoSetup
```

**Build:**
```
powershell -ExecutionPolicy Bypass -File tools\build_installer.ps1
```
Скрипт exe-г build хийж, `build\` руу хуулаад `installer\ZombieMongolia.iss`-ээр `dist\ZombieMongolia_Setup.exe` үүсгэнэ.

«EndUpdateResource failed» алдаа гарвал вирусны эсрэг програм файлыг түр барьсан байна. Дахин ажиллуулна, эсвэл `dist\` хавтсыг шалгалтаас хасна.

## 3. Вэб (хөтөч, утас)

**Шаардлага:** Emscripten SDK (`C:\emsdk`):
```
git clone https://github.com/emscripten-core/emsdk.git C:\emsdk
C:\emsdk\emsdk.bat install latest
C:\emsdk\emsdk.bat activate latest
```
Ninja нь `C:\msys64\ucrt64\bin` дотор байна.

**Build:**
```
powershell -ExecutionPolicy Bypass -File tools\build_web.ps1
```
Үр дүн нь `build_web\web\` (`index.html`, `ZombieMongolia.js`, `.wasm`, `.data`, зураг).

- **Локал турших:** `tools\build_web.ps1 -Serve` → http://localhost:8080. `file://`-ээр нээвэл ажиллахгүй.
- **Сервер дээр гаргах:** `build_web\web\` хавтасны **бүх** файлыг WinSCP-ээр `play-zombie.apps.mn` хавтас руу хуулна. Дараа нь хөтчөө refresh хийнэ.

## 4. Android (APK)

**Шаардлага** (`C:\Android` дотор; өөр газар бол `-AndroidRoot <зам>`):
```
C:\Android\jdk\                          Java 17
C:\Android\sdk\build-tools\34.0.0\       aapt, zipalign, apksigner
C:\Android\sdk\platforms\android-34\     android.jar
C:\Android\sdk\ndk\26.3.11579264\        C++ хөрвүүлэгч
```
Эдгээрийг Android Studio-ийн SDK Manager-ээр эсвэл `sdkmanager`-ээр суулгана.

**Build:**
```
powershell -ExecutionPolicy Bypass -File tools\build_android.ps1
```
Үр дүн нь `build_android\ZombieMongolia.apk` (arm64-v8a ба armeabi-v7a).

- APK нь туршилтын (debug) түлхүүрээр гарын үсэг зурагдана: `build_android\debug.keystore`, нууц үг `android`. Google Play-д гаргахад өөрийн release түлхүүр хэрэгтэй.
- Утсанд суулгах: APK-г утас руу хуулж нээнэ, эсвэл `adb install -r build_android\ZombieMongolia.apk`.

## 5. Сервер талын шинэчлэлт (тоглоомын шинэ хувилбар гаргахаас өмнө)

Тоглоомын шинэ хувилбар нь шинэ backend, relay-тэй хамт ажиллана. Сүлжээний протокол 10. Хуучин хувилбартай тоглогч шинэ хувилбартай тоглогчтой нэгдэхгүй.

### Backend (Django, `zombie-api.apps.mn`)
```
cd backend
git pull
.venv/bin/pip install -r requirements.txt
.venv/bin/python manage.py migrate        # 0004..0010: Gold, өрөө, тулаан, нэхэмжлэх, цэрэг алдах, Gold татах, онцгой дүрийн үнэ, урамшууллын Gold
```
`backend/.env` дотор relay-тэй **ижил** нууц үг бичнэ. Урт санамсаргүй мөр үүсгэх:
`python -c "import secrets; print(secrets.token_urlsafe(40))"`
```
RELAY_SECRET=<урт санамсаргүй мөр>
```
Дараа нь backend-ийн үйлчилгээг (gunicorn) дахин эхлүүлнэ.

Локал тест:
```
cd backend
DEBUG=1 DB_ENGINE=sqlite .venv/bin/python manage.py test game
```

### Relay (Node.js, `zombie-relay.apps.mn`)
```
cd server
npm install
RELAY_SECRET=<backend-ийнхтэй ижил> npm start
```
Үйлчилгээгээр (systemd, pm2) ажиллаж байвал түүний орчинд `RELAY_SECRET` нэмээд дахин эхлүүлнэ.

### Admin самбар (React)
```
cd admin
npm ci
npm run build
```
`admin\dist\` хавтсыг сервер дээрх admin хавтас руу хуулна.

### Gold цэнэглэх вэб хуудас
Backend-тэй хамт ажиллана, тусдаа суулгах шаардлагагүй: https://zombie-api.apps.mn/api/topup/

Gold-ийн багц, үнийг admin самбарын **Тохиргоо → Gold худалдах багц**-аас өөрчилнө.
