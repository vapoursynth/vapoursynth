#define AppName = 'VapourSynth'
#define Version = 'R10'

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
VersionInfoVersion=0.9.0.0
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

[Files]
Source: vapoursynth.dll; DestDir: {code:GetPythonPath}; Flags: ignoreversion uninsrestartdelete restartreplace
Source: vapoursynth.pyd; DestDir: {code:GetPythonPath}; Flags: ignoreversion uninsrestartdelete restartreplace
Source: QtCore4.dll; DestDir: {code:GetPythonPath}; Flags: ignoreversion uninsrestartdelete restartreplace
Source: vsvfw.dll; DestDir: {sys}; Flags: ignoreversion uninsrestartdelete restartreplace
Source: msvcr100.dll; DestDir: {sys}; Flags: restartreplace uninsneveruninstall sharedfile
Source: msvcp100.dll; DestDir: {sys}; Flags: restartreplace uninsneveruninstall sharedfile

[Icons]
Name: {group}\VapourSynth Website; Filename: http://www.vapoursynth.com/
Name: {group}\VapourSynth Documentation; Filename: http://www.vapoursynth.com/doc/

[Registry]
Root: HKLM; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}; ValueType: string; ValueName: ""; ValueData: "VapourSynth"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32'; ValueType: string; ValueName: ""; ValueData: "vsvfw.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: ""; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: "Source Filter"; ValueData: "{{D3588AB0-0781-11CE-B03A-0020AF0BA770}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\.vpy; ValueType: string; ValueName: ""; ValueData: "vsfile"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\vsfile; ValueType: string; ValueName: ""; ValueData: "VapourSynth Python Script"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\vsfile\DefaultIcon; ValueType: string; ValueName: ""; ValueData: "vsvfw.dll,0"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: SOFTWARE\Classes\AVIFile\Extensions\VPY; ValueType: string; ValueName: ""; ValueData: "{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}"; Flags: uninsdeletevalue uninsdeletekeyifempty

[Code]

var PythonPath: string;

function InitializeSetup(): Boolean;
begin
  Result := RegQueryStringValue(HKLM, 'SOFTWARE\Python\PythonCore\3.2\InstallPath', '', PythonPath);
  if not Result then
    MsgBox('Python 3.2 installation not found.', mbCriticalError, MB_OK);
end;

function GetPythonPath(Param: String): String;
begin
  Result := PythonPath + '\Lib\site-packages';
end;
