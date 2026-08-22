; ============================================================================
;  Скрипт установщика AdminPRO для Inno Setup.
;  Ничего программировать не нужно — просто открыть в Inno Setup Compiler
;  и нажать "Compile".
; ============================================================================

#define MyAppName "AdminPRO"
#define MyAppVersion "1.0 BETA"
#define MyAppExeName "AdminPRO.exe"

[Setup]
AppId={{B7E1B6B1-4B0A-4B3E-9C1A-ADM1NPRO0001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=setup_output
OutputBaseFilename=AdminPRO_Setup
Compression=lzma
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=AdminPRO.ico
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "Создать значок на рабочем столе"; GroupDescription: "Дополнительные значки:"

[Files]
Source: "AdminPRO.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "AdminPRO.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\AdminPRO.ico"
Name: "{group}\Удалить {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\AdminPRO.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Запустить {#MyAppName}"; Flags: nowait postinstall skipifsilent
