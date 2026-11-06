; MELVILLE installer (Inno Setup 6). Built by .github/workflows/build-windows.yml:
;   iscc /DAppVersion=1.0.12 Installer\MELVILLE.iss
; Installs for the current user (no admin prompt). MELVILLE doesn't need Hawthorne Route
; or the EMERSON Link: the parts talk inside MELVILLE.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{6C0F3E52-6B39-4C8E-9E0A-5A4D2B7F8C11}
AppName=MELVILLE
AppVersion={#AppVersion}
AppVerName=MELVILLE {#AppVersion}
AppPublisher=PEQUOD
DefaultDirName={autopf}\MELVILLE
DefaultGroupName=MELVILLE
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=MELVILLE
UninstallDisplayIcon={app}\MELVILLE.exe
OutputDir=..\dist
OutputBaseFilename=MELVILLE-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=MELVILLE.ico
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Put a MELVILLE shortcut on the desktop"; GroupDescription: "Shortcuts:"

[Files]
Source: "..\build\Melville_artefacts\Release\MELVILLE.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Resources\Licences\*.txt"; DestDir: "{app}\Licences"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\MELVILLE"; Filename: "{app}\MELVILLE.exe"
Name: "{autodesktop}\MELVILLE"; Filename: "{app}\MELVILLE.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\MELVILLE.exe"; Description: "Start MELVILLE"; Flags: nowait postinstall skipifsilent
