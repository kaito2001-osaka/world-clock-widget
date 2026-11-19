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
  #define AppVer "1.2.1"
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
; Windows 10 1809, as README states. Time zones come from Windows' own
; registry data (#49), so the 1903 icu.dll requirement no longer applies.
MinVersion=10.0.17763
; A backstop only: [Code] closes the installed gadget, and asks the user to
; close Settings, before this Restart Manager check runs. No AppMutex: that
; check runs at startup, matches by name, and cannot tell the installed copy
; from another one (see [Code]).
CloseApplications=yes
RestartApplications=no
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

[CustomMessages]
en.DeleteSettingsPrompt=Also delete the settings file (city list, display settings, window position)?
en.DeleteSettingsHint=Choose [No] to restore your current settings when you reinstall.
ja.DeleteSettingsPrompt=設定ファイル（都市リスト・表示設定・ウィンドウ位置）も削除しますか？
ja.DeleteSettingsHint=［いいえ］を選ぶと、再インストール時に現在の設定がそのまま復元されます。
en.SettingsOpenPrompt=World Clock Settings is open. To keep changes you have not applied yet, click [OK] or [Apply] in it. Then close Settings and click [Retry].%n%nClick [Cancel] to stop without changing anything.
en.SettingsStillOpen=Stopped because World Clock Settings is still open. Nothing has been changed.
ja.SettingsOpenPrompt=World Clock 設定が開いています。まだ適用していない変更を残すには、設定画面で［OK］か［適用］を押してください。そのあと設定画面を閉じて、［再試行］を押してください。%n%n［キャンセル］を押すと、何も変更せずに中止します。
ja.SettingsStillOpen=World Clock 設定が開いたままのため、中止しました。何も変更していません。

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
; No "start with Windows" task. Autostart is the HKCU\Run value that the
; settings app's toggle and the gadget manage; an installer-made shortcut is a
; second autostart the app cannot see, so turning the toggle off left it on.

[InstallDelete]
; The Startup-folder shortcut that versions up to 1.2.0 could create.
Type: files; Name: "{userstartup}\{#AppName}.lnk"

[Files]
; Whole self-contained staging folder (gadget + settings + .NET runtime).
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#GadgetExe}"
Name: "{group}\World Clock 設定"; Filename: "{app}\{#SettingsExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#GadgetExe}"; Tasks: desktopicon

[Registry]
; The in-app "launch at startup" toggle writes this HKCU\Run value; remove it on uninstall.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: none; ValueName: "WorldClockGadget"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#GadgetExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent

; No [UninstallDelete] entry for {app}. Neither exe writes there (settings live in
; %APPDATA%\WorldClockGadget), and the normal uninstall already removes the
; installed files and the emptied folder. A wildcard delete would also take the
; user's own files when {app} is an existing folder such as C:\Tools. If the app
; ever creates files in {app}, list them here by name.

[Code]
// The running apps are closed only once the user has committed to the change:
// PrepareToInstall runs after [Install] is clicked, usAppMutexCheck after the
// uninstall is confirmed. Closing them at startup (InitializeSetup) meant that
// merely opening and cancelling Setup killed them (#82).
//
// Only the copies in {app} are touched: those are the files being replaced. A
// gadget or Settings started from another folder (a dev build, say) locks
// nothing here, so it is left running.

const
  WM_CLOSE = $0010;
  GadgetWindowClass = 'WorldClockGadgetWindow';

function GetWindowThreadProcessId(Wnd: HWND; var ProcessId: DWORD): DWORD;
  external 'GetWindowThreadProcessId@user32.dll stdcall';

// Finds the process running {app}\ExeName. Matched by full path, not by name,
// since taskkill /im would also hit copies elsewhere. If WMI fails, reports
// nothing running and leaves the Restart Manager check as the backstop.
function FindAppProcess(const ExeName: String; var Proc: Variant): Boolean;
var
  Target, Path: String;
  Locator, Service, Procs, Item: Variant;
  I: Integer;
begin
  Result := False;
  Target := ExpandConstant('{app}\' + ExeName);
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Procs := Service.ExecQuery('SELECT ProcessId, ExecutablePath FROM Win32_Process WHERE Name = ''' + ExeName + '''');
    for I := 0 to Procs.Count - 1 do
    begin
      Item := Procs.ItemIndex(I);
      // Null for processes we may not inspect; those are not ours to close.
      if not VarIsNull(Item.ExecutablePath) then
      begin
        Path := Item.ExecutablePath;
        if CompareText(Path, Target) = 0 then
        begin
          Proc := Item;
          Result := True;
          Exit;
        end;
      end;
    end;
  except
    Log('Process lookup for ' + ExeName + ' failed: ' + GetExceptionMessage);
  end;
end;

function IsAppRunning(const ExeName: String): Boolean;
var
  Proc: Variant;
begin
  Result := FindAppProcess(ExeName, Proc);
end;

// Settings saves only on [OK] / [Apply], and also discards edits on an
// ordinary close, so neither taskkill nor the Restart Manager's WM_CLOSE may
// end it. The user closes it, keeping or dropping the edits. Cancelling (also
// the unattended default) stops the install/uninstall with nothing changed.
function EnsureSettingsClosed(): Boolean;
begin
  Result := True;
  while IsAppRunning('{#SettingsExe}') do
    if SuppressibleMsgBox(CustomMessage('SettingsOpenPrompt'),
                          mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
    begin
      Result := False;
      Exit;
    end;
end;

// The gadget is a WS_EX_TOOLWINDOW / NOACTIVATE window, so the Restart Manager
// cannot always see it; close it here or its exe stays locked and the
// upgrade/uninstall half-fails. It has nothing unsaved (the position is saved
// on every move), so WM_CLOSE is enough, with Terminate if it does not exit.
procedure CloseGadget;
var
  Proc: Variant;
  Pid, WndPid: DWORD;
  Wnd: HWND;
  I: Integer;
begin
  if not FindAppProcess('{#GadgetExe}', Proc) then
    Exit;
  Pid := Proc.ProcessId;
  // One gadget per session (its mutex), so the class finds at most one window;
  // close it only if it belongs to the {app} copy.
  Wnd := FindWindowByClassName(GadgetWindowClass);
  if Wnd <> 0 then
  begin
    WndPid := 0;
    GetWindowThreadProcessId(Wnd, WndPid);
    if WndPid = Pid then
      PostMessage(Wnd, WM_CLOSE, 0, 0);
  end;
  for I := 1 to 30 do
  begin
    if not IsAppRunning('{#GadgetExe}') then
      Exit;
    Sleep(100);
  end;
  try
    Proc.Terminate();
  except
    Log('Terminating the gadget failed: ' + GetExceptionMessage);
  end;
  Sleep(300); // let handles close before we touch the files
end;

// Runs after [Install] and before the Restart Manager's files-in-use check.
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not EnsureSettingsClosed() then
  begin
    Result := CustomMessage('SettingsStillOpen');
    Exit;
  end;
  CloseGadget;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ConfigDir: String;
begin
  // After the user confirmed the uninstall, before any file is removed.
  if CurUninstallStep = usAppMutexCheck then
  begin
    if not EnsureSettingsClosed() then
      Abort;
    CloseGadget;
  end;

  // After the files are gone, offer to remove the user's settings too.
  // SuppressibleMsgBox (not MsgBox) so an unattended /SILENT uninstall does not
  // block on a dialog; unattended defaults to IDNO = keep the user's settings.
  if CurUninstallStep = usPostUninstall then
  begin
    ConfigDir := ExpandConstant('{userappdata}\WorldClockGadget');
    if DirExists(ConfigDir) then
    begin
      if SuppressibleMsgBox(CustomMessage('DeleteSettingsPrompt') + #13#10 + #13#10 +
                ConfigDir + #13#10 + #13#10 +
                CustomMessage('DeleteSettingsHint'),
                mbConfirmation, MB_YESNO, IDNO) = IDYES then
        DelTree(ConfigDir, True, True, True);
    end;
  end;
end;
