#define Version '55'
#define VersionExtra '-API4-RC5'
#define PythonVersion '3.9'
#define PythonCompactVersion '39'

#ifndef InstallerBits
  #define InstallerBits '64'
#endif
#define InstallerBitsInt = Int(InstallerBits)

#if InstallerBitsInt == 64
  #define AppName 'VapourSynth (64-bits)'
  #define AppId 'VapourSynth'
  #define RegistryPath 'SOFTWARE\VapourSynth'
  #define SourceBinaryPath '..\msvc_project\x64\Release'
  #define MimallocSourceBinaryPath '..\mimalloc\out\msvc-x64\Release'
  #define MimallocRedirectName 'mimalloc-redirect.dll'
  #define WheelFilename(Version) 'VapourSynth-' + Version + '-cp' + PythonCompactVersion + '-cp' + PythonCompactVersion + '-win_amd64.whl'
  #define WheelFilenamePython38(Version) 'VapourSynth-' + Version + '-cp38-cp38-win_amd64.whl'
#else
  #define AppName 'VapourSynth (32-bits)'
  #define AppId 'VapourSynth-32'
  #define RegistryPath 'SOFTWARE\VapourSynth-32'
  #define SourceBinaryPath '..\msvc_project\Release'
  #define MimallocSourceBinaryPath '..\mimalloc\out\msvc-Win32\Release'
  #define MimallocRedirectName 'mimalloc-redirect32.dll'
  #define WheelFilename(Version) 'VapourSynth-' + Version + '-cp' + PythonCompactVersion + '-cp' + PythonCompactVersion + '-win32.whl'
  #define WheelFilenamePython38(Version) 'VapourSynth-' + Version + '-cp38-cp38-win32.whl'
#endif

[Setup]
OutputDir=Compiled
OutputBaseFilename=VapourSynth{#= InstallerBits}-R{#= Version}{#= VersionExtra}
Compression=lzma2/max
SolidCompression=yes
VersionInfoDescription={#= AppName} R{#= Version}{#= VersionExtra} Installer
AppId={#= AppId}
AppName={#= AppName} R{#= Version}{#= VersionExtra}
AppVersion=R{#= Version}{#= VersionExtra}
AppVerName={#= AppName} R{#= Version}{#= VersionExtra}
AppPublisher=Fredrik Mellbin
AppPublisherURL=http://www.vapoursynth.com/
AppSupportURL=http://www.vapoursynth.com/
AppUpdatesURL=http://www.vapoursynth.com/
VersionInfoVersion=1.{#= Version}.0.0
DefaultDirName={autopf}\{#= AppId}
DefaultGroupName={#= AppName}
AllowCancelDuringInstall=no
AllowNoIcons=yes
AllowUNCPath=no
MinVersion=6.1
UsePreviousPrivileges=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
FlatComponentsList=yes
ChangesEnvironment=yes
#if InstallerBitsInt == 64
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
#endif

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: Full; Description: Full installation; Flags: iscustom

[Components]
Name: "vscore"; Description: "VapourSynth {#= InstallerBits}-bit R{#= Version}{#= VersionExtra}"; Types: Full; Flags: fixed disablenouninstallwarning
Name: "vsrepo"; Description: "VSRepo Package Manager"; Types: Full; Flags: disablenouninstallwarning
Name: "docs"; Description: "VapourSynth Documentation"; Types: Full; Flags: disablenouninstallwarning
Name: "sdk"; Description: "VapourSynth SDK"; Flags: disablenouninstallwarning; Types: Full
Name: "pismo"; Description: "Pismo PFM Runtime (required for AVFS)"; Types: Full; Flags: disablenouninstallwarning
Name: "vsruntimes"; Description: "Visual Studio 2019 Runtime"; Types: Full; Check: IsAdminInstallMode; Flags: disablenouninstallwarning

[Tasks]
Name: newvpyfile; Description: "Add 'New VapourSynth Python Script' option to shell context menu"; GroupDescription: "VapourSynth:"; Components: vscore
Name: vscorepath; Description: "Add VSPipe and AVFS to PATH"; GroupDescription: "VapourSynth:"; Components: vscore
Name: vsrepoupdate; Description: "Update VSRepo package list"; GroupDescription: "VSRepo:"; Components: vsrepo
Name: vsrepopath; Description: "Add VSRepo to PATH"; GroupDescription: "VSRepo:"; Components: vsrepo

[Run]
Filename: {code:GetPythonExecutable}; Parameters: "-m pip install ""{app}\python\{#= WheelFilename(Version)}"""; Check: IsPython3; Flags: runhidden; Components: vscore
Filename: {code:GetPythonExecutable}; Parameters: "-m pip install ""{app}\python\{#= WheelFilenamePython38(Version)}"""; Check: IsPython38; Flags: runhidden; Components: vscore
Filename: "{app}\pismo\pfm-192-vapoursynth-win.exe"; Parameters: "install"; Check: IsAdminInstallMode; Flags: runhidden; Components: pismo
Filename: {code:GetPythonExecutable}; Parameters: """{app}\vsrepo\vsrepo.py"" update"; Flags: runhidden runasoriginaluser; Components: vsrepo

[UninstallRun]
Filename: {code:GetPythonExecutable}; Parameters: "-m pip uninstall -y VapourSynth"; Flags: runhidden; Components: vscore

[Files]
;core binaries
Source: template.vpy; DestDir: {app}; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: ..\dist\{#= WheelFilename(Version)}; DestDir: {app}\python; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: ..\dist\{#= WheelFilenamePython38(Version)}; DestDir: {app}\python; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore

Source: {#= SourceBinaryPath}\vapoursynth.dll; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\vapoursynth.pdb; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\avfs.exe; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\vspipe.exe; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\vsvfw.dll; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\vsscript.dll; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore; Check: IsPython3
Source: {#= SourceBinaryPath}\vsscriptpython38.dll; DestName: "vsscript.dll"; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore; Check: IsPython38

Source: {#= MimallocSourceBinaryPath}\mimalloc-override.dll; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= MimallocSourceBinaryPath}\{#= MimallocRedirectName}; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore

;vsrepo
Source: ..\vsrepo\vsrepo.py; DestDir: {app}\vsrepo; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vsrepo
Source: ..\vsrepo\vsgenstubs\__init__.py; DestDir: {app}\vsrepo\vsgenstubs; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vsrepo
Source: ..\vsrepo\vsgenstubs\_vapoursynth.part.pyi; DestDir: {app}\vsrepo\vsgenstubs; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vsrepo
Source: 7z.exe; DestDir: {app}\vsrepo; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vsrepo
Source: 7z.dll; DestDir: {app}\vsrepo; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vsrepo

;docs
Source: ..\doc\_build\html\*; DestDir: {app}\docs; Flags: ignoreversion uninsrestartdelete restartreplace recursesubdirs; Components: docs

;sdk
Source: ..\include\VapourSynth4.h; DestDir: {app}\sdk\include\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\include\VSHelper4.h; DestDir: {app}\sdk\include\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\include\VSScript4.h; DestDir: {app}\sdk\include\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\include\VSConstants4.h; DestDir: {app}\sdk\include\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\include\VapourSynth.h; DestDir: {app}\sdk\include\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\include\VSHelper.h; DestDir: {app}\sdk\include\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\include\VSScript.h; DestDir: {app}\sdk\include\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\msvc_project\Release\vsscript.lib; DestDir: {app}\sdk\lib32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\msvc_project\x64\Release\vsscript.lib; DestDir: {app}\sdk\lib64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\msvc_project\Release\vapoursynth.lib; DestDir: {app}\sdk\lib32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\msvc_project\x64\Release\vapoursynth.lib; DestDir: {app}\sdk\lib64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\sdk\filter_skeleton.c; DestDir: {app}\sdk\examples; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\sdk\invert_example.c; DestDir: {app}\sdk\examples; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\sdk\vsscript_example.c; DestDir: {app}\sdk\examples; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk

;bundled plugins
Source: {#= SourceBinaryPath}\AvsCompat.dll; DestDir: {app}\core\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore

;pismo installer
Source: "pfm-192-vapoursynth-win.exe"; DestDir: {app}\pismo; Flags: ignoreversion uninsrestartdelete restartreplace; Components: pismo

; Create the general autoload directory
[Dirs]
Name: "{app}\plugins"; Flags: uninsalwaysuninstall; Components: vscore

[Icons]
Name: {group}\VapourSynth Website; Filename: http://www.vapoursynth.com/; Components: vscore
Name: {group}\Documentation (Local); Filename: {app}\docs\index.html; Components: docs
Name: {group}\Documentation (Online); Filename: http://www.vapoursynth.com/doc/
Name: {group}\Global Autoload Directory; Filename: {app}\plugins; Check: IsAdminInstallMode; Components: vscore
Name: {group}\User Autoload Directory; Filename: %APPDATA%\VapourSynth\plugins{#= InstallerBits}; Components: vscore
Name: {group}\VapourSynth SDK; Filename: {app}\sdk; Components: sdk

[Registry]
Root: HKA; Subkey: {#= RegistryPath}; ValueType: string; ValueName: "Version"; ValueData: {#= Version}; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: {#= RegistryPath}; ValueType: string; ValueName: "Path"; ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: {#= RegistryPath}; ValueType: string; ValueName: "CorePlugins"; ValueData: "{app}\core\plugins"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: {#= RegistryPath}; ValueType: string; ValueName: "Plugins"; ValueData: "{app}\plugins"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: {#= RegistryPath}; ValueType: string; ValueName: "VapourSynthDLL"; ValueData: "{app}\core\vapoursynth.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: {#= RegistryPath}; ValueType: string; ValueName: "VSScriptDLL"; ValueData: "{app}\core\vsscript.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: {#= RegistryPath}; ValueType: string; ValueName: "VSPipeEXE"; ValueData: "{app}\core\vspipe.exe"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: {#= RegistryPath}; ValueType: string; ValueName: "VSRepoPY"; ValueData: "{app}\vsrepo\vsrepo.py"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vsrepo
Root: HKA; Subkey: {#= RegistryPath}; ValueType: string; ValueName: "PythonPath"; ValueData: "{code:GetPythonPath}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore

; new vpy file shortcut task
Root: HKA; Subkey: SOFTWARE\Classes\.vpy\ShellNew; ValueType: string; ValueName: "FileName"; ValueData: "{app}\template.vpy"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: newvpyfile

; vfw
Root: HKA; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}; ValueType: string; ValueName: ""; ValueData: "VapourSynth"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: ""; ValueData: "{app}\core\vsvfw.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: ""; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: "Source Filter"; ValueData: "{{D3588AB0-0781-11CE-B03A-0020AF0BA770}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: SOFTWARE\Classes\.vpy; ValueType: string; ValueName: ""; ValueData: "vpyfile"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: SOFTWARE\Classes\vpyfile; ValueType: string; ValueName: ""; ValueData: "VapourSynth Python Script"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: SOFTWARE\Classes\vpyfile\DefaultIcon; ValueType: string; ValueName: ""; ValueData: "{app}\core\vsvfw.dll,0"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKA; Subkey: SOFTWARE\Classes\AVIFile\Extensions\VPY; ValueType: string; ValueName: ""; ValueData: "{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore

; PATH
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "PATH"; ValueData: "{olddata};{app}\core"; Check: IsAdminInstallMode; Tasks: vscorepath
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "PATH"; ValueData: "{olddata};{app}\core"; Check: not IsAdminInstallMode; Tasks: vscorepath
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "PATH"; ValueData: "{olddata};{app}\vsrepo"; Check: IsAdminInstallMode; Tasks: vsrepopath
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "PATH"; ValueData: "{olddata};{app}\vsrepo"; Check: not IsAdminInstallMode; Tasks: vsrepopath

#include "scripts\products.iss"
#include "scripts\products\stringversion.iss"
#include "scripts\products\msiproduct.iss"
#include "scripts\products\vcredist2017.iss"

[Code]

const VSRuntimeVersion = '14.29.30133';

type
  TPythonPath = record
    DisplayName: string;
    InstallPath: string;
    ExecutablePath: string;
    Version: string;
    Bitness: Integer;
  end;

var
  RuntimesAdded: Boolean;
  PythonInstallations: array of TPythonPath;
  GlobalPythonInstallations: array of TPythonPath;
  UserPythonInstallations: array of TPythonPath;
  PythonPage: TWizardPage;
  PythonList: TNewCheckListBox;
  PythonPath: string;
  PythonExecutable: string;
  PythonVersion: string;

    
function IsPython3: Boolean;
begin
  Result := PythonVersion = '{#PythonVersion}';
end;

function IsPython38: Boolean;
begin
  Result := PythonVersion = '3.8';
end;

procedure GetPythonInstallations2(var DestArray: array of TPythonPath; RegRoot: Integer; RegPath: string; PythonVer: string; AssumeBitness: Integer);
var
  Names, Tags: TArrayOfString;
  Nc, Tc: Integer;
  RegPathTemp: string;
  Temp: string;
  DisplayName, InstallPath, ExecutablePath: string;
  Bitness: Integer;
begin
  if RegGetSubkeyNames(RegRoot, RegPath, Names) then
  begin
    for Nc := 0 to GetArrayLength(Names) - 1 do
    begin
      if RegGetSubkeyNames(RegRoot, RegPath + '\' + Names[Nc], Tags) then
      begin
        for Tc := 0 to GetArrayLength(Tags) - 1 do
        begin
          RegPathTemp := RegPath + '\' + Names[Nc] + '\' + Tags[Tc];
          Bitness := AssumeBitness;
          if (not RegQueryStringValue(RegRoot, RegPathTemp, 'SysVersion', Temp)) or (Temp <> PythonVer) then
            continue;
          if RegQueryStringValue(RegRoot, RegPathTemp, 'SysArchitecture', Temp) then
          begin
            if Temp = '32bit' then
              Bitness := 32
            else if Temp = '64bit' then
              Bitness := 64;              
          end;

          if Bitness = {#InstallerBitsInt} then
          begin
            if RegQueryStringValue(RegRoot, RegPathTemp, 'DisplayName', DisplayName)
              and RegQueryStringValue(RegRoot, RegPathTemp + '\InstallPath', '', InstallPath)
              and RegQueryStringValue(RegRoot, RegPathTemp + '\InstallPath', 'ExecutablePath', ExecutablePath)
              and (DisplayName <> '') and (InstallPath <> '') and (ExecutablePath <> '') then
            begin
               SetArrayLength(DestArray, GetArrayLength(DestArray) + 1);
               DestArray[GetArrayLength(DestArray) - 1].DisplayName := DisplayName;
               DestArray[GetArrayLength(DestArray) - 1].InstallPath := InstallPath;
               DestArray[GetArrayLength(DestArray) - 1].ExecutablePath := ExecutablePath;
               DestArray[GetArrayLength(DestArray) - 1].Version := PythonVer;
               DestArray[GetArrayLength(DestArray) - 1].Bitness := Bitness;
            end;
          end;
        end;
      end;
    end;
  end;  
end;

procedure GetPythonInstallations;
begin
  GetPythonInstallations2(UserPythonInstallations, HKCU, 'SOFTWARE\Python', '{#PythonVersion}', 0);
  GetPythonInstallations2(GlobalPythonInstallations, HKLM, 'SOFTWARE\Python', '{#PythonVersion}', {#InstallerBitsInt}); 
  GetPythonInstallations2(UserPythonInstallations, HKCU, 'SOFTWARE\Python', '3.8', 0);
  GetPythonInstallations2(GlobalPythonInstallations, HKLM, 'SOFTWARE\Python', '3.8', {#InstallerBitsInt}); 
end;

procedure PopulatePythonInstallations(List: TNewCheckListBox);
var
  Counter: Integer;
  First: Boolean;
begin
  List.Items.Clear;
  First := True;
  for Counter := 0 to GetArrayLength(PythonInstallations) - 1 do
    if PythonInstallations[Counter].Bitness = {#InstallerBitsInt} then
      with PythonInstallations[Counter] do
      begin
        List.AddRadioButton(DisplayName, '(' + InstallPath + ')', 0, First, True, TObject(Counter));
        First := False;
      end;
end;

/////////////////////////////////////////////////////////////////////

procedure RemovePath(Path: string);
var
  Paths: string;
  P: Integer;
  RegPath: string;
begin
  if IsAdminInstallMode then
    RegPath := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    RegPath := 'Environment';
  
  if not RegQueryStringValue(HKA, RegPath, 'Path', Paths) then
  begin
    Log('PATH not found');
  end
    else
  begin
    Log(Format('PATH is [%s]', [Paths]));

    P := Pos(';' + Uppercase(Path) + ';', ';' + Uppercase(Paths) + ';');
    if P = 0 then
    begin
      Log(Format('Path [%s] not found in PATH', [Path]));
    end
      else
    begin
      if P > 1 then P := P - 1;
      Delete(Paths, P, Length(Path) + 1);
      Log(Format('Path [%s] removed from PATH => [%s]', [Path, Paths]));

      if RegWriteStringValue(HKA, RegPath, 'Path', Paths) then
      begin
        Log('PATH written');
      end
        else
      begin
        Log('Error writing PATH');
      end;
    end;
  end;
end;

/////////////////////////////////////////////////////////////////////

function InitializeSetup: Boolean;
var
  HasOtherPython: Boolean;
  ErrCode: Integer;
begin
  RuntimesAdded := False;
  PythonList := nil;
  GetPythonInstallations;
  if IsAdminInstallMode then
  begin
    PythonInstallations := GlobalPythonInstallations;
    HasOtherPython := GetArrayLength(UserPythonInstallations) > 0;
  end
  else
  begin
    PythonInstallations := UserPythonInstallations;
    HasOtherPython := GetArrayLength(GlobalPythonInstallations) > 0;
  end;

  Result := GetArrayLength(PythonInstallations) > 0; 

  if not Result and not HasOtherPython then
      MsgBox('No suitable Python {#PythonVersion} or 3.8 ({#InstallerBits}-bit) installation found. The installer will now exit.', mbCriticalError, MB_OK)
  else if not Result and IsAdminInstallMode then
      MsgBox('Python {#PythonVersion} or 3.8 ({#InstallerBits}-bit) is installed for the current user only. Run the installer again and select "Install for me only" or install Python for all users.', mbCriticalError, MB_OK)
  else if not Result and not IsAdminInstallMode then
      MsgBox('Python {#PythonVersion} or 3.8 ({#InstallerBits}-bit) is installed for all users. Run the installer again and select "Install for all users" or install Python for the current user only.', mbCriticalError, MB_OK);
      
  if not IsAdminInstallMode and not vcredist2017installed(VSRuntimeVersion) then
      if MsgBox('No recent Visual Studio 2019 Runtime installed.If you proceed with the install it is very likely the installation won''t work.'#13#10#13#10'Go to the download website now?', mbError, MB_YESNO) = IDYES then
          ShellExec('open', 'https://visualstudio.microsoft.com/downloads/?q=redistributable', '', '', SW_SHOW, ewNoWait, ErrCode);
end;

procedure WizardFormOnResize(Sender: TObject);
begin
  with PythonList do
  begin
    Parent := PythonPage.Surface;
    Left := ScaleX(0);
    Top := ScaleY(0);
    Width := PythonPage.Surface.Width - ScaleX(0) * 2;
    Height := PythonPage.Surface.Height;
  end;
end;

procedure InitializeWizard();
begin
  PythonPage := CreateCustomPage(wpSelectComponents, 'Select Python Installation', 'Select which Python installation to use');
  PythonList := TNewCheckListBox.Create(PythonPage);
  
  with PythonList do
  begin
    Parent := PythonPage.Surface;
    Left := ScaleX(0);
    Top := ScaleY(0);
    Width := PythonPage.Surface.Width - ScaleX(0) * 2;
    Height := PythonPage.Surface.Height;     
  end; 

  WizardForm.OnResize := @WizardFormOnResize;
end;

function GetPythonPath(Param: string): String;
begin
  Result := PythonPath;
end;

function GetPythonExecutable(Param: string): String;
begin
    Result := PythonExecutable;
end;

/////////////////////////////////////////////////////////////////////
function GetUninstallString: String;
var
  sUnInstPath: String;
  sUnInstallString: String;
begin
  sUnInstPath := ExpandConstant('Software\Microsoft\Windows\CurrentVersion\Uninstall\{#emit SetupSetting("AppId")}_is1');
  sUnInstallString := '';
  RegQueryStringValue(HKA, sUnInstPath, 'UninstallString', sUnInstallString)
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
var
  Counter: Integer;
  Idx: Integer;
begin
  Result := True;
  if CurPageID = PythonPage.ID then
  begin    
    for Counter := 0 to PythonList.Items.Count - 1 do
    begin
      if PythonList.Checked[Counter] then
      begin
        Idx := Integer(PythonList.ItemObject[Counter]);
        with PythonInstallations[Idx] do
        begin
          PythonPath := InstallPath;
          PythonExecutable := ExecutablePath;
          PythonVersion := Version;
        end;
      end;
    end;
  end
  else if CurPageID = wpSelectComponents then
  begin    
    PopulatePythonInstallations(PythonList); 
  end
  else if CurPageID = wpReady then
  begin
    if WizardIsComponentSelected('vsruntimes') and not RuntimesAdded then
    begin
      vcredist2017(VSRuntimeVersion);
      RuntimesAdded := True;
    end;
    Result := NextButtonClick2(CurPageID);
  end;
end;

/////////////////////////////////////////////////////////////////////

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RemovePath(ExpandConstant('{app}\core'));
    RemovePath(ExpandConstant('{app}\vsrepo'));
  end;
end;
