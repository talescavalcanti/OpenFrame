; Instalador do OpenFrame para Windows (Inno Setup 6).
; Compilado pela CI (.github/scripts/pacote-windows.sh):
;   iscc /DAppVersion=0.6.0 /DPayloadDir=<pasta do app> /DOutputDir=<saída> OpenFrame.iss
; <pasta do app> é a saída do windeployqt, com o FFmpeg e as licenças.

#ifndef AppVersion
  #error Passe /DAppVersion=x.y.z
#endif
#ifndef PayloadDir
  #error Passe /DPayloadDir=<pasta do app>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{70E442D6-7D51-44F3-81EA-E36C02E40550}
AppName=OpenFrame
AppVersion={#AppVersion}
AppVerName=OpenFrame {#AppVersion}
AppPublisher=OpenFrame
AppPublisherURL=https://github.com/talescavalcanti/OpenFrame
AppSupportURL=https://github.com/talescavalcanti/OpenFrame/issues
AppUpdatesURL=https://github.com/talescavalcanti/OpenFrame/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\OpenFrame
DefaultGroupName=OpenFrame
DisableProgramGroupPage=yes
; Sem administrador instala só para o usuário; com, para todos.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#PayloadDir}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=OpenFrame-{#AppVersion}-windows-x64-instalador
SetupIconFile=OpenFrame.ico
UninstallDisplayIcon={app}\OpenFrame.exe
UninstallDisplayName=OpenFrame
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
ChangesAssociations=yes
CloseApplications=yes

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\OpenFrame"; Filename: "{app}\OpenFrame.exe"
Name: "{autodesktop}\OpenFrame"; Filename: "{app}\OpenFrame.exe"; Tasks: desktopicon

[Registry]
; Projetos .openframe abrem no OpenFrame.
Root: HKA; Subkey: "Software\Classes\.openframe"; ValueType: string; ValueName: ""; ValueData: "OpenFrame.Projeto"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.openframe\OpenWithProgids"; ValueType: string; ValueName: "OpenFrame.Projeto"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\OpenFrame.Projeto"; ValueType: string; ValueName: ""; ValueData: "Projeto do OpenFrame"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\OpenFrame.Projeto\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\OpenFrame.exe,0"
Root: HKA; Subkey: "Software\Classes\OpenFrame.Projeto\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\OpenFrame.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\OpenFrame.exe\SupportedTypes"; ValueType: string; ValueName: ".openframe"; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\OpenFrame.exe"; Description: "{cm:LaunchProgram,OpenFrame}"; Flags: nowait postinstall skipifsilent
