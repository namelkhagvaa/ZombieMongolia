; Zombie Mongolia — Windows суулгагч (Inno Setup 6).
; Бүтээх: powershell -ExecutionPolicy Bypass -File tools\build_installer.ps1
; Үр дүн:  dist\ZombieMongolia_Setup.exe
;
; Exe нь static холбогдсон (MinGW-ийн DLL хэрэггүй), зөвхөн Windows 10/11-ийн системийн сангуудыг ашиглана.
; Хадгалалт: %LOCALAPPDATA%\ZombieMongolia\savegame.txt (устгахад хадгалалт үлдэнэ).

#define AppName "Zombie Mongolia"
#define AppVersion "1.0"
#define AppExe "ZombieMongolia.exe"

[Setup]
AppId={{6B8E2F4A-3C1D-4F7B-9A2E-5D0C8B1F7E63}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Lkhagvaa
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
; Админ эрхгүй хэрэглэгч ч өөрийн хавтсанд суулгаж болно.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\dist
OutputBaseFilename=ZombieMongolia_Setup
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes

[Tasks]
Name: "desktopicon"; Description: "Ширээний дэлгэц дээр товчлол үүсгэх"; GroupDescription: "Нэмэлт:"

[Files]
Source: "..\build\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
; Тоглоомд ачаалагддаг файлууд л (эх загварын том .glb файлууд орохгүй)
Source: "..\assets\*_mesh.glb"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\assets\*_color.png"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\assets\window_icon.png"; DestDir: "{app}\assets"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\{#AppName}-г устгах"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{#AppName} тоглоомыг одоо эхлүүлэх"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
