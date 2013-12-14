#define AppName = 'VapourSynth'
#define Version = 'R22'

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
VersionInfoVersion=1.22.0.0
DefaultDirName={pf32}\VapourSynth
DefaultGroupName=VapourSynth
AllowCancelDuringInstall=no
AllowNoIcons=yes
AllowUNCPath=no
MinVersion=5.1sp3
PrivilegesRequired=admin
FlatComponentsList=yes
ArchitecturesAllowed=x86 x64
ArchitecturesInstallIn64BitMode=x64

[Types]
Name: Full; Description: Full installation; Flags: iscustom

[Components]
Name: "vs64"; Description: "VapourSynth 64bit"; Types: Full; Check: HasPython64; Flags: disablenouninstallwarning
Name: "vs32"; Description: "VapourSynth 32bit"; Types: Full; Check: HasPython32; Flags: disablenouninstallwarning
Name: "sdk"; Description: "VapourSynth SDK"; Flags: fixed; Types: Full

[Tasks]
Name: newvpyfile; Description: "Add 'New VapourSynth Python Script' option to shell context menu"; GroupDescription: "New File Shortcuts:"; Components: vs32 vs64
Name: registervsfs; Description: "Register the VSFS handler"; GroupDescription: "Pismo File Mount:";
Name: registervsfs\registervsfs32; Description: "32-bit VSFS handler"; GroupDescription: "Pismo File Mount:"; Flags: Exclusive; Components: vs32
Name: registervsfs\registervsfs64; Description: "64-bit VSFS handler"; GroupDescription: "Pismo File Mount:"; Flags: Exclusive; Components: vs64

[Files]
;core binaries
Source: template.vpy; DestDir: {app}; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32 vs64
Source: vapoursynth.pth; DestDir: {code:GetPythonPath32}; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: vapoursynth.pth; DestDir: {code:GetPythonPath64}; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: x86\vapoursynth.pyd; DestDir: {code:GetPythonPath32}\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: x64\vapoursynth.pyd; DestDir: {code:GetPythonPath64}\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: x86\vapoursynth.dll; DestDir: {code:GetPythonPath32}\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: x64\vapoursynth.dll; DestDir: {code:GetPythonPath64}\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64
Source: x86\vapoursynth.dll; DestDir: {app}\core32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: x64\vapoursynth.dll; DestDir: {app}\core64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: x86\vsfs.dll; DestDir: {app}\core32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: x64\vsfs.dll; DestDir: {app}\core64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: x86\vspipe.exe; DestDir: {app}\core32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: x64\vspipe.exe; DestDir: {app}\core64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: x86\vspipe.exe; DestDir: {app}\core32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: x64\vspipe.exe; DestDir: {app}\core64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: x86\vsvfw.dll; DestDir: {sys}; Flags: uninsrestartdelete restartreplace 32bit; Components: vs32
Source: x64\vsvfw.dll; DestDir: {sys}; Flags: uninsrestartdelete restartreplace 64bit; Components: vs64

Source: x86\vsscript.dll; DestDir: {sys}; Flags: uninsrestartdelete restartreplace 32bit; Components: vs32
Source: x64\vsscript.dll; DestDir: {sys}; Flags: uninsrestartdelete restartreplace 64bit; Components: vs64
;vs2010 and vs2013 runtime installers
Source: x86\rtx86.msi; DestDir: {app}\runtimes; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: x64\rtx64.msi; DestDir: {app}\runtimes; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64
;sdk
Source: ..\include\VapourSynth.h; DestDir: {app}\sdk\include; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\include\VSHelper.h; DestDir: {app}\sdk\include; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\include\VSScript.h; DestDir: {app}\sdk\include; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: x86\vsscript.lib; DestDir: {app}\sdk\lib32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: x64\vsscript.lib; DestDir: {app}\sdk\lib64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: x86\vapoursynth.lib; DestDir: {app}\sdk\lib32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: x64\vapoursynth.lib; DestDir: {app}\sdk\lib64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\sdk\filter_skeleton.c; DestDir: {app}\sdk\examples; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\sdk\invert_example.c; DestDir: {app}\sdk\examples; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\sdk\vsscript_example.c; DestDir: {app}\sdk\examples; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
;bundled plugins
Source: x86\plugins\*; DestDir: {app}\core32\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: x64\plugins\*; DestDir: {app}\core64\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

; Create the general autoload directory
[Dirs]
Name: "{app}\plugins32"; Flags: uninsalwaysuninstall; Components: vs32
Name: "{app}\plugins64"; Flags: uninsalwaysuninstall; Components: vs64

[Icons]
Name: {group}\VapourSynth Website; Filename: http://www.vapoursynth.com/
Name: {group}\Documentation; Filename: http://www.vapoursynth.com/doc/
Name: {group}\Global Autoload Directory (32bit); Filename: {app}\plugins32; Components: vs32
Name: {group}\Global Autoload Directory (64bit); Filename: {app}\plugins64; Components: vs64
Name: {group}\VapourSynth SDK; Filename: {app}\sdk; Components: sdk

[Registry]
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Version"; ValueData: {#= Version}; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Path"; ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "CorePlugins32"; ValueData: "{app}\core32\plugins"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "CorePlugins64"; ValueData: "{app}\core64\plugins"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Plugins32"; ValueData: "{app}\plugins32"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Plugins64"; ValueData: "{app}\plugins64"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64

; new vpy file shortcut task
Root: HKLM; Subkey: SOFTWARE\Classes\.vpy\ShellNew; ValueType: string; ValueName: "FileName"; ValueData: "{app}\template.vpy"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: newvpyfile

; 32bit vfw
Root: HKLM32; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}; ValueType: string; ValueName: ""; ValueData: "VapourSynth"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: ""; ValueData: "vsvfw.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: ""; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: "Source Filter"; ValueData: "{{D3588AB0-0781-11CE-B03A-0020AF0BA770}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\.vpy; ValueType: string; ValueName: ""; ValueData: "vsfile"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\vsfile; ValueType: string; ValueName: ""; ValueData: "VapourSynth Python Script"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\vsfile\DefaultIcon; ValueType: string; ValueName: ""; ValueData: "vsvfw.dll,0"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\AVIFile\Extensions\VPY; ValueType: string; ValueName: ""; ValueData: "{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32

; 64bit vfw
Root: HKLM; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}; ValueType: string; ValueName: ""; ValueData: "VapourSynth"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: ""; ValueData: "vsvfw.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: ""; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: "Source Filter"; ValueData: "{{D3588AB0-0781-11CE-B03A-0020AF0BA770}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM; Subkey: SOFTWARE\Classes\.vpy; ValueType: string; ValueName: ""; ValueData: "vsfile"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM; Subkey: SOFTWARE\Classes\vsfile; ValueType: string; ValueName: ""; ValueData: "VapourSynth Python Script"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM; Subkey: SOFTWARE\Classes\vsfile\DefaultIcon; ValueType: string; ValueName: ""; ValueData: "vsvfw.dll,0"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM; Subkey: SOFTWARE\Classes\AVIFile\Extensions\VPY; ValueType: string; ValueName: ""; ValueData: "{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64

[Run]
Filename: "msiexec.exe"; Parameters: "/package ""{app}\runtimes\rtx86.msi"" /quiet /norestart ARPSYSTEMCOMPONENT=1"; Components: vs32
Filename: "msiexec.exe"; Parameters: "/package ""{app}\runtimes\rtx64.msi"" /quiet /norestart ARPSYSTEMCOMPONENT=1"; Components: vs64

Filename: "{win}\pfm.exe"; Parameters: "register ""{app}\core64\vsfs.dll"""; Tasks: registervsfs\registervsfs64; Flags: skipifdoesntexist
Filename: "{win}\pfm.exe"; Parameters: "register ""{app}\core32\vsfs.dll"""; Tasks: registervsfs\registervsfs32; Flags: skipifdoesntexist

[UninstallRun]
Filename: "{win}\pfm.exe"; Parameters: "unregister ""{app}\core64\vsfs.dll"""; Tasks: registervsfs\registervsfs64; Flags: skipifdoesntexist
Filename: "{win}\pfm.exe"; Parameters: "unregister ""{app}\core32\vsfs.dll"""; Tasks: registervsfs\registervsfs32; Flags: skipifdoesntexist

[Code]

var
  PythonPath32: string;
  PythonPath64: string;

function HasPython32: Boolean;
begin
  Result := PythonPath32 <> '';
end;

function HasPython64: Boolean;
begin
  Result := PythonPath64 <> '';
end;

function InitializeSetup: Boolean;
var Success: Boolean;
begin
  Success := RegQueryStringValue(HKCU32, 'SOFTWARE\Python\PythonCore\3.3\InstallPath', '', PythonPath32);
  if not Success then
    RegQueryStringValue(HKLM32, 'SOFTWARE\Python\PythonCore\3.3\InstallPath', '', PythonPath32);

  if Is64BitInstallMode then
  begin
    Success := RegQueryStringValue(HKCU, 'SOFTWARE\Python\PythonCore\3.3\InstallPath', '', PythonPath64);
    if not Success then
      RegQueryStringValue(HKLM, 'SOFTWARE\Python\PythonCore\3.3\InstallPath', '', PythonPath64);
  end;

  Result := HasPython32 or HasPython64;
  if not Result then
  begin
    MsgBox('No Python 3.3 installations found.', mbCriticalError, MB_OK);
  end
  else if PythonPath32 = PythonPath64 then
  begin
    Result := False;
    MsgBox('Corrupt Python installation detected. 32 and 64 bit install path is the same. Uninstall and re-install Python for all users.', mbCriticalError, MB_OK)
  end;
end;

function GetPythonPath32(Param: string): String;
begin
  Result := PythonPath32 + '\Lib\site-packages';
end;

function GetPythonPath64(Param: String): String;
begin
  Result := PythonPath64 + '\Lib\site-packages';
end;

// copied from the internets

/////////////////////////////////////////////////////////////////////
function GetUninstallString: String;
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
function IsUpgrade: Boolean;
begin
  Result := (GetUninstallString() <> '');
end;


/////////////////////////////////////////////////////////////////////
function UnInstallOldVersion: Integer;
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
  end
  else
    Result := 1;
end;

/////////////////////////////////////////////////////////////////////
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep=ssInstall then
  begin
    if IsUpgrade() then
      UnInstallOldVersion();
  end;
end;

/////////////////////////////////////////////////////////////////////
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectComponents then
  begin
    if not IsComponentSelected('vs32 or vs64') then
    begin
      Result := False;
      MsgBox('At least one version of the core library has to be installed.', mbCriticalError, MB_OK)
    end;
  end;
end;
