#define AppName "Frame"
#define AppVersion GetEnv("FRAME_VERSION")
#define StageDir "..\build\stage\Frame " + AppVersion

[Setup]
AppId={{FB829B86-44D9-4F2D-BAF2-49D54D793E15}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=VahamCooKey
AppPublisherURL=https://github.com/VahamCooKey/Frame
AppSupportURL=https://github.com/VahamCooKey/Frame/issues
DefaultDirName={autopf}\Frame
DefaultGroupName=Frame
UninstallDisplayIcon={app}\Frame.ico
OutputDir=..\build
OutputBaseFilename=Frame-Setup-{#AppVersion}
ArchitecturesInstallIn64BitMode=x64
WizardStyle=modern
PrivilegesRequired=lowest
LicenseFile={#StageDir}\LICENSE
Compression=lzma2/ultra64
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
ArchitecturesAllowed=x64
ChangesAssociations=yes
SetupIconFile=..\assets\app.ico
UsePreviousTasks=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "defaultvideo"; Description: "Use Frame for all supported video files"
Name: "defaultaudio"; Description: "Also use Frame for audio files"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion
Source: "..\build\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall
; The folder is versioned because Explorer caches icons by path and index;
; a redrawn icon at the same path would keep showing the old artwork.
Source: "..\assets\file-icons\*.ico"; DestDir: "{app}\icons\3"; Flags: ignoreversion
Source: "..\assets\file-icons\Inter-OFL.txt"; DestDir: "{app}\icons\3"; Flags: ignoreversion
Source: "..\assets\app.ico"; DestDir: "{app}"; DestName: "Frame.ico"; Flags: ignoreversion

[InstallDelete]
Type: files; Name: "{app}\icons\*.ico"
Type: files; Name: "{app}\icons\Inter-OFL.txt"
Type: filesandordirs; Name: "{app}\icons\2"

[Registry]
#include "MediaAssociations.iss"

[Icons]
Name: "{group}\Frame"; Filename: "{app}\frame.exe"; IconFilename: "{app}\Frame.ico"
Name: "{autodesktop}\Frame"; Filename: "{app}\frame.exe"; IconFilename: "{app}\Frame.ico"; Check: NeedsDesktopShortcut

[Run]
Filename: "{app}\frame.exe"; Description: "Open Frame"; Flags: postinstall nowait skipifsilent
Filename: "ms-settings:defaultapps?registeredAppUser=Frame"; Description: "Manage file types in Windows settings"; Flags: postinstall shellexec unchecked skipifsilent

[UninstallRun]
Filename: "{app}\frame-associations.exe"; Parameters: "--restore"; Flags: runhidden waituntilterminated; RunOnceId: "RestoreFrameFileDefaults"

[Code]
function NeedsVCRedist: Boolean; forward;

procedure SHChangeNotify(EventId: Integer; Flags: Cardinal; Item1, Item2: Integer);
  external 'SHChangeNotify@shell32.dll stdcall';

procedure RefreshShellIcons;
begin
  { SHCNE_ASSOCCHANGED: Explorer re-reads DefaultIcon for the registered types. }
  SHChangeNotify($08000000, 0, 0, 0);
end;

function NeedsDesktopShortcut: Boolean;
begin
  Result := WizardIsTaskSelected('desktopicon') or FileExists(ExpandConstant('{autodesktop}\Frame.lnk'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Selection: String;
  ResultCode: Integer;
begin
  if CurStep = ssDone then RefreshShellIcons;
  if CurStep <> ssPostInstall then Exit;
  { The native association helper needs the runtime too. }
  if NeedsVCRedist then begin
    WizardForm.StatusLabel.Caption := 'Installing the Microsoft Visual C++ runtime...';
    if (not Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart',
        '', SW_HIDE, ewWaitUntilTerminated, ResultCode))
        or ((ResultCode <> 0) and (ResultCode <> 3010)) then
      RaiseException('Microsoft Visual C++ runtime installation did not complete.');
  end;
  Selection := '';
  if WizardSilent then begin
    { Silent deployments must explicitly request changing user defaults. }
    Selection := ExpandConstant('{param:MAKEDEFAULTS|}');
    if (Selection <> 'video') and (Selection <> 'audio') and (Selection <> 'all') then Exit;
  end else begin
    if WizardIsTaskSelected('defaultvideo') then Selection := 'video';
    if WizardIsTaskSelected('defaultaudio') then begin
      if Selection = 'video' then Selection := 'all' else Selection := 'audio';
    end;
  end;
  if Selection = '' then Exit;
  WizardForm.StatusLabel.Caption := 'Setting Frame as your media player...';
  if (not Exec(ExpandConstant('{app}\frame-associations.exe'),
      '--set ' + Selection + ' --user-consent', '', SW_HIDE, ewWaitUntilTerminated, ResultCode))
      or (ResultCode <> 0) then begin
    if not WizardSilent then begin
      MsgBox('Windows did not accept the complete default-player change. Your previous choices have been kept where possible. Windows settings will open so you can review them.', mbInformation, MB_OK);
      ShellExec('', 'ms-settings:defaultapps?registeredAppUser=Frame', '', '', SW_SHOWNORMAL, ewNoWait, ResultCode);
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then RefreshShellIcons;
end;

function NeedsVCRedist: Boolean;
var
  Version: String;
begin
  Result := not RegQueryStringValue(HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', Version);
end;
