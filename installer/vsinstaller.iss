#define AppName = 'VapourSynth'
#define Version = 'R17'

[Setup]
OutputDir=Compiled
OutputBaseFilename=vapoursynth_installer
Compression=lzma2/max
InternalCompressLevel=max
SolidCompression=yes
LZMAUseSeparateProcess=yes
VersionInfoDescription={#= AppName} {#= Version} Installer
AppId={#= AppName}
AppName={#= AppName}
AppVersion={#= Version}
AppVerName={#= AppName} {#= Version}
AppPublisher=Fredrik Mellbin
AppPublisherURL=http://www.vapoursynth.com/
AppSupportURL=http://www.vapoursynth.com/
AppUpdatesURL=http://www.vapoursynth.com/
VersionInfoVersion=0.9.0.2
DefaultDirName={pf}\VapourSynth
DefaultGroupName=VapourSynth
AllowCancelDuringInstall=no
AllowNoIcons=yes
AllowUNCPath=no
MinVersion=0,5.1
PrivilegesRequired=poweruser
FlatComponentsList=yes

[Types]
Name: Full; Description: Full installation

[Components]
Name: Core; Description: {#= AppName} {#= Version}; Types: Full; Flags: fixed

[Tasks]
Name: newvpyfile; Description: "Add 'New VapourSynth Python Script' option to shell context menu"; GroupDescription: "New File Shortcuts:"

[Files]
;core binaries
Source: vapoursynth.dll; DestDir: {code:GetPythonPath}; Flags: ignoreversion uninsrestartdelete restartreplace
Source: vapoursynth.dll; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace
Source: vapoursynth.pyd; DestDir: {code:GetPythonPath}; Flags: ignoreversion uninsrestartdelete restartreplace
Source: QtCore4.dll; DestDir: {code:GetPythonPath}; Flags: ignoreversion uninsrestartdelete restartreplace
Source: QtCore4.dll; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace
Source: vsfs.dll; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace
Source: template.vpy; DestDir: {app}; Flags: ignoreversion uninsrestartdelete restartreplace
Source: vsvfw.dll; DestDir: {sys}; Flags: ignoreversion uninsrestartdelete restartreplace
;vs2010 runtime
Source: msvcr100.dll; DestDir: {sys}; Flags: restartreplace uninsneveruninstall sharedfile
Source: msvcp100.dll; DestDir: {sys}; Flags: restartreplace uninsneveruninstall sharedfile
;sdk
Source: ..\include\VapourSynth.h; DestDir: {app}\sdk\include; Flags: ignoreversion uninsrestartdelete restartreplace
Source: ..\include\VSHelper.h; DestDir: {app}\sdk\include; Flags: ignoreversion uninsrestartdelete restartreplace
Source: ..\sdk\filter_skeleton.c; DestDir: {app}\sdk\examples; Flags: ignoreversion uninsrestartdelete restartreplace
Source: ..\sdk\invert_example.c; DestDir: {app}\sdk\examples; Flags: ignoreversion uninsrestartdelete restartreplace
;bundled filters
Source: filters\avisource.dll; DestDir: {app}\filters; Flags: ignoreversion uninsrestartdelete restartreplace
Source: filters\vivtc.dll; DestDir: {app}\filters; Flags: ignoreversion uninsrestartdelete restartreplace
Source: filters\eedi3.dll; DestDir: {app}\filters; Flags: ignoreversion uninsrestartdelete restartreplace
Source: filters\temporalsoften.dll; DestDir: {app}\filters; Flags: ignoreversion uninsrestartdelete restartreplace
Source: filters\histogram.dll; DestDir: {app}\filters; Flags: ignoreversion uninsrestartdelete restartreplace


[Icons]
Name: {group}\VapourSynth Website; Filename: http://www.vapoursynth.com/
Name: {group}\Documentation; Filename: http://www.vapoursynth.com/doc/
Name: {group}\Bundled Filters; Filename: {app}\filters
Name: {group}\VapourSynth SDK; Filename: {app}\sdk

[Registry]
Root: HKLM; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}; ValueType: string; ValueName: ""; ValueData: "VapourSynth"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: ""; ValueData: "vsvfw.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: ""; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: "Source Filter"; ValueData: "{{D3588AB0-0781-11CE-B03A-0020AF0BA770}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\.vpy; ValueType: string; ValueName: ""; ValueData: "vsfile"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\vsfile; ValueType: string; ValueName: ""; ValueData: "VapourSynth Python Script"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\vsfile\DefaultIcon; ValueType: string; ValueName: ""; ValueData: "vsvfw.dll,0"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\AVIFile\Extensions\VPY; ValueType: string; ValueName: ""; ValueData: "{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\.vpy\ShellNew; ValueType: string; ValueName: "FileName"; ValueData: "{app}\template.vpy"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: newvpyfile

[Code]

var PythonPath: string;

function InitializeSetup(): Boolean;
begin
  Result := RegQueryStringValue(HKLM, 'SOFTWARE\Python\PythonCore\3.3\InstallPath', '', PythonPath);
  if not Result then
    MsgBox('Python 3.3 installation not found.', mbCriticalError, MB_OK);
end;

function GetPythonPath(Param: String): String;
begin
  Result := PythonPath + '\Lib\site-packages';
end;

// copied from the internets

/////////////////////////////////////////////////////////////////////
function GetUninstallString(): String;
var
  sUnInstPath: String;
  sUnInstallString: String;
begin
  sUnInstPath := ExpandConstant('Software\Microsoft\Windows\CurrentVersion\Uninstall\{#emit SetupSetting("AppId")}_is1');
  sUnInstallString := '';
  if not RegQueryStringValue(HKLM, sUnInstPath, 'UninstallString', sUnInstallString) then
    RegQueryStringValue(HKCU, sUnInstPath, 'UninstallString', sUnInstallString);
  Result := sUnInstallString;
end;


/////////////////////////////////////////////////////////////////////
function IsUpgrade(): Boolean;
begin
  Result := (GetUninstallString() <> '');
end;


/////////////////////////////////////////////////////////////////////
function UnInstallOldVersion(): Integer;
var
  sUnInstallString: String;
  iResultCode: Integer;
begin
// Return Values:
// 1 - uninstall string is empty
// 2 - error executing the UnInstallString
// 3 - successfully executed the UnInstallString

  // default return value
  Result := 0;

  // get the uninstall string of the old app
  sUnInstallString := GetUninstallString();
  if sUnInstallString <> '' then begin
    sUnInstallString := RemoveQuotes(sUnInstallString);
    if Exec(sUnInstallString, '/SILENT /NORESTART /SUPPRESSMSGBOXES','', SW_HIDE, ewWaitUntilTerminated, iResultCode) then
      Result := 3
    else
      Result := 2;
  end else
    Result := 1;
end;

/////////////////////////////////////////////////////////////////////
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep=ssInstall) then
  begin
    if (IsUpgrade()) then
    begin
      UnInstallOldVersion();
    end;
  end;
end;
