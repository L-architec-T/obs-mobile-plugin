; Windows installer for the OBS Mobile plugin (Inno Setup 6), built by .github/scripts/Package-Windows.ps1:
;   ISCC /DAppVersion=0.1.0 /DSourceDir=<release/RelWithDebInfo> /DOutputDir=<release> /DOutputBaseName=<name> installer.iss
; Installs into OBS's per-machine plugin folder (OBS 30+), opens the plugin's ports in the
; Windows firewall (private networks only) and registers an uninstaller that undoes both.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\release\RelWithDebInfo"
#endif
#ifndef OutputDir
  #define OutputDir "..\release"
#endif
#ifndef OutputBaseName
  #define OutputBaseName "obs-mobile-installer"
#endif

#define AppName "OBS Mobile (plugin OBS)"
#define FirewallTcp "OBS Mobile - contrôle et retour vidéo"
#define FirewallUdp "OBS Mobile - caméras"

[Setup]
AppId={{6A1F2C4E-8B3D-4E7A-9C51-0D2E7F3B9A64}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=L-architec-T
AppPublisherURL=https://github.com/L-architec-T/obs-mobile-plugin
AppSupportURL=https://github.com/L-architec-T/obs-mobile-plugin/issues
DefaultDirName={commonappdata}\obs-studio\plugins\obs-mobile
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
; OBS is closed by the check below; this only covers a DLL still held by something else.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Messages]
french.FinishedLabel=Le plugin est installé. Ouvre OBS : ton téléphone le trouvera tout seul dans l'app OBS Mobile. Le panneau « Téléphones » est dans le menu Docks d'OBS.

[Files]
Source: "{#SourceDir}\obs-mobile\*"; Excludes: "*.pdb"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Run]
; Old rules first, so a reinstall never piles up duplicates.
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#FirewallTcp}"""; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#FirewallUdp}"""; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""{#FirewallTcp}"" dir=in action=allow protocol=TCP localport=4460-4461 profile=private"; Flags: runhidden; StatusMsg: "Ouverture des ports dans le pare-feu…"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""{#FirewallUdp}"" dir=in action=allow protocol=UDP localport=9711-9726 profile=private"; Flags: runhidden

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#FirewallTcp}"""; Flags: runhidden; RunOnceId: "FirewallTcp"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#FirewallUdp}"""; Flags: runhidden; RunOnceId: "FirewallUdp"

[Code]
// OBS holds this mutex while it runs: a plugin is only loaded at OBS start, and its DLL cannot
// be replaced while loaded.
function ObsIsRunning(): Boolean;
begin
  Result := CheckForMutexes('OBSStudioCore');
end;

function WaitForObsClosed(): Boolean;
begin
  Result := True;
  while ObsIsRunning() do
    if MsgBox('OBS Studio est ouvert. Ferme-le, puis clique sur OK pour continuer.', mbInformation, MB_OKCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
end;

function InitializeSetup(): Boolean;
begin
  Result := WaitForObsClosed();
end;

function InitializeUninstall(): Boolean;
begin
  Result := WaitForObsClosed();
end;
