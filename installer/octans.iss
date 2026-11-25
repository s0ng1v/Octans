#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif

[Setup]
AppId={{8E9A3F21-4B6C-4D7E-9A12-5C8F0E3B7D41}
AppName=Octans
AppVersion={#MyAppVersion}
AppVerName=Octans v{#MyAppVersion}
AppPublisher=Octans
DefaultDirName={autopf}\Octans
DefaultGroupName=Octans
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupIconFile=..\assets\octans.ico
UninstallDisplayIcon={app}\Octans.exe
UninstallDisplayName=Octans v{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=Octans-Setup-v{#MyAppVersion}
AppMutex=Local\OctansBatteryTraySingleInstance

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
LaunchAfterInstall=立即启动 Octans
TaskAutostart=开机自动启动 Octans（静默驻留托盘）
TaskDesktopIcon=创建桌面快捷方式
UninstallShortcut=卸载 Octans

[Tasks]
Name: "autostart"; Description: "{cm:TaskAutostart}"
Name: "desktopicon"; Description: "{cm:TaskDesktopIcon}"; Flags: unchecked

[InstallDelete]
Type: files; Name: "{app}\octans-add.exe"
Type: files; Name: "{autoprograms}\Octans\Octans 添加设备向导.lnk"

[Files]
Source: "..\build\Octans.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\octans-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\fonts\AnthropicSerifWebText.ttf"; DestDir: "{app}\fonts"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\Octans\Octans"; Filename: "{app}\Octans.exe"
Name: "{autoprograms}\Octans\{cm:UninstallShortcut}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Octans"; Filename: "{app}\Octans.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Octans"; ValueData: """{app}\Octans.exe"" --silent"; Flags: uninsdeletevalue; Tasks: autostart
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "Octans"; Flags: deletevalue; Tasks: not autostart

[INI]
Filename: "{userappdata}\Octans\config.ini"; Section: "General"; Key: "AutostartInitialized"; String: "1"

[Run]
Filename: "{app}\Octans.exe"; Description: "{cm:LaunchAfterInstall}"; Flags: nowait postinstall skipifsilent

[Code]
const
  WM_CLOSE_MSG = $0010;

procedure CloseRunningOctans();
var
  Hwnd: HWND;
  WaitLoop: Integer;
begin
  Hwnd := FindWindowByClassName('OctansAppCtrlWnd');
  if Hwnd <> 0 then
  begin
    PostMessage(Hwnd, WM_CLOSE_MSG, 0, 0);
    for WaitLoop := 1 to 25 do
    begin
      Sleep(100);
      if (FindWindowByClassName('OctansAppCtrlWnd') = 0) and
         (not CheckForMutexes('Local\OctansBatteryTraySingleInstance')) then
        Break;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  CloseRunningOctans();
  Result := '';
end;

function InitializeUninstall(): Boolean;
begin
  CloseRunningOctans();
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Octans');
  end;
end;
