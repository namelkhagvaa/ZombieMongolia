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
; Админ эрх: хамтарсан тоглолтод Windows Firewall-д зөвшөөрөл нэмнэ (дотоод сүлжээнд бие биеэ олох).
PrivilegesRequired=admin
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
Source: "..\assets\home_back.png"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\assets\splash.png"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\assets\icons\*.png"; DestDir: "{app}\assets\icons"; Flags: ignoreversion
Source: "..\assets\huds\*.png"; DestDir: "{app}\assets\huds"; Flags: ignoreversion
Source: "..\assets\terrain\*.png"; DestDir: "{app}\assets\terrain"; Flags: ignoreversion
Source: "..\assets\cars\*.vmesh"; DestDir: "{app}\assets\cars"; Flags: ignoreversion
Source: "..\assets\cars\*_color.png"; DestDir: "{app}\assets\cars"; Flags: ignoreversion
Source: "..\assets\buu\*.vmesh"; DestDir: "{app}\assets\buu"; Flags: ignoreversion
Source: "..\assets\buu\*_color.png"; DestDir: "{app}\assets\buu"; Flags: ignoreversion
Source: "..\assets\sounds\*.wav"; DestDir: "{app}\assets\sounds"; Flags: ignoreversion
Source: "..\assets\sounds\soldiers\*.wav"; DestDir: "{app}\assets\sounds\soldiers"; Flags: ignoreversion
Source: "..\assets\trees\*.vmesh"; DestDir: "{app}\assets\trees"; Flags: ignoreversion
Source: "..\assets\trees\*_color.png"; DestDir: "{app}\assets\trees"; Flags: ignoreversion
Source: "..\assets\trees\*_far.bin"; DestDir: "{app}\assets\trees"; Flags: ignoreversion
Source: "..\assets\pooshig\*.vmesh"; DestDir: "{app}\assets\pooshig"; Flags: ignoreversion
Source: "..\assets\pooshig\*_color.png"; DestDir: "{app}\assets\pooshig"; Flags: ignoreversion
Source: "..\assets\city\*.bin"; DestDir: "{app}\assets\city"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\{#AppName}-г устгах"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
; Хамтарсан тоглолт: бусад компьютер энэ тоглоомыг олж холбогдохын тулд бүх сүлжээнд (Public ч) зөвшөөрнө.
; Хуучин дүрмийг эхлээд устгана (дахин суулгахад давхардахгүй).
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""Zombie Mongolia"""; Flags: runhidden waituntilterminated
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""Zombie Mongolia"" dir=in action=allow program=""{app}\{#AppExe}"" enable=yes profile=any"; Flags: runhidden waituntilterminated
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""Zombie Mongolia"" dir=in action=allow protocol=UDP localport=47810-47811 enable=yes profile=any"; Flags: runhidden waituntilterminated
Filename: "{app}\{#AppExe}"; Description: "{#AppName} тоглоомыг одоо эхлүүлэх"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""Zombie Mongolia"""; Flags: runhidden waituntilterminated; RunOnceId: "RemoveFirewallRule"
