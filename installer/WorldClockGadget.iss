; Inno Setup script for World Clock Gadget.
; Per-user install (no admin). Packages the self-contained publish staging dir
; (gadget exe + settings app with bundled .NET runtime) into one setup.exe.
;
; Build via make_installer.cmd (which prepares the staging dir and runs ISCC).
; StageDir / AppVer can be overridden on the ISCC command line with /D.

#ifndef StageDir
  #define StageDir "..\build\stage"
#endif
#ifndef AppVer
  #define AppVer "1.0.0"
#endif

#define AppName "World Clock Gadget"
#define GadgetExe "WorldClockGadget.exe"
#define SettingsExe "WorldClockSettings.exe"

[Setup]
AppId={{B7A3F2E1-6C4D-4A9B-9E21-3F5C8D0A1B22}
AppName={#AppName}
AppVersion={#AppVer}
VersionInfoVersion={#AppVer}
AppPublisher=World Clock Gadget
DefaultDirName={autopf}\WorldClockGadget
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Per-user install: no UAC elevation.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
; Self-contained publish is win-x64.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Close a running gadget automatically on install/uninstall/upgrade.
CloseApplications=yes
RestartApplications=no
AppMutex=WorldClockGadget_SingleInstance
UninstallDisplayIcon={app}\{#GadgetExe}
UninstallDisplayName={#AppName}
SetupIconFile=..\icons\worldclock.ico
OutputDir=..\dist_installer
OutputBaseFilename=WorldClockGadget-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startupicon"; Description: "Windows 起動時に自動的に開始する"; GroupDescription: "スタートアップ:"; Flags: unchecked

[Files]
; Whole self-contained staging folder (gadget + settings + .NET runtime).
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#GadgetExe}"
Name: "{group}\World Clock 設定"; Filename: "{app}\{#SettingsExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#GadgetExe}"; Tasks: desktopicon
Name: "{userstartup}\{#AppName}"; Filename: "{app}\{#GadgetExe}"; Tasks: startupicon

[Registry]
; The in-app "launch at startup" toggle writes this HKCU\Run value; remove it on uninstall.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: none; ValueName: "WorldClockGadget"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#GadgetExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Anything the app generated inside its own folder.
Type: filesandordirs; Name: "{app}"

[Code]
// The gadget is a WS_EX_TOOLWINDOW / NOACTIVATE window, so the Restart Manager
// cannot always see it. Terminate it explicitly before installing or removing
// files, otherwise the exe stays locked and the upgrade/uninstall half-fails.
procedure StopGadget;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im WorldClockGadget.exe',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im WorldClockSettings.exe',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Sleep(300); // let handles close before we touch the files
end;

// Must run BEFORE the AppMutex check, which happens later in both flows --
// otherwise a running gadget aborts the install/uninstall outright.
function InitializeSetup(): Boolean;
begin
  StopGadget;
  Result := True;
end;

function InitializeUninstall(): Boolean;
begin
  StopGadget;
  Result := True;
end;

// Second chance, in case the gadget was relaunched while the wizard was open.
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopGadget;
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ConfigDir: String;
begin
  if CurUninstallStep = usUninstall then
    StopGadget;

  // After the files are gone, offer to remove the user's settings too.
  // SuppressibleMsgBox (not MsgBox) so an unattended /SILENT uninstall does not
  // block on a dialog; unattended defaults to IDNO = keep the user's settings.
  if CurUninstallStep = usPostUninstall then
  begin
    ConfigDir := ExpandConstant('{userappdata}\WorldClockGadget');
    if DirExists(ConfigDir) then
    begin
      if SuppressibleMsgBox('設定ファイル（都市リスト・表示設定・ウィンドウ位置）も削除しますか？' + #13#10 + #13#10 +
                ConfigDir + #13#10 + #13#10 +
                '［いいえ］を選ぶと、再インストール時に現在の設定がそのまま復元されます。',
                mbConfirmation, MB_YESNO, IDNO) = IDYES then
        DelTree(ConfigDir, True, True, True);
    end;
  end;
end;
