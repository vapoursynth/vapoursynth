#define Version 'R47'

#ifndef InstallerBits
  #define InstallerBits '64'
#endif
#define InstallerBitsInt = Int(InstallerBits)

#if InstallerBitsInt == 64
  #define AppName 'VapourSynth (64-bits)'
  #define AppId 'VapourSynth'
  #define RegistryPath 'SOFTWARE\VapourSynth'
  #define SourceBinaryPath '..\msvc_project\x64\Release'
  #define WheelFilename 'VapourSynth-46-cp37-cp37m-win_amd64.whl'
#else
  #define AppName 'VapourSynth (32-bits)'
  #define AppId 'VapourSynth-32'
  #define RegistryPath 'SOFTWARE\VapourSynth-32'
  #define SourceBinaryPath '..\msvc_project\Release'
  #define WheelFilename 'VapourSynth-46-cp37-cp37m-win32.whl'
#endif

[Setup]
OutputDir=Compiled
OutputBaseFilename=VapourSynth{#= InstallerBits}-{#= Version}
Compression=lzma2/max
InternalCompressLevel=max
SolidCompression=yes
LZMAUseSeparateProcess=yes
VersionInfoDescription={#= AppName} {#= Version} Installer
AppId={#= AppId}
AppName={#= AppName} {#= Version}
AppVersion={#= Version}
AppVerName={#= AppName} {#= Version}
AppPublisher=Fredrik Mellbin
AppPublisherURL=http://www.vapoursynth.com/
AppSupportURL=http://www.vapoursynth.com/
AppUpdatesURL=http://www.vapoursynth.com/
VersionInfoVersion=1.46.0.0
DefaultDirName={autopf}\{#= AppId}
DefaultGroupName={#= AppName}
AllowCancelDuringInstall=no
AllowNoIcons=yes
AllowUNCPath=no
MinVersion=6.0
UsePreviousPrivileges=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
FlatComponentsList=yes
#if InstallerBitsInt == 64
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
#endif

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: Full; Description: Full installation; Flags: iscustom

[Components]
Name: "vscore"; Description: "VapourSynth {#= InstallerBits}-bit {#= Version}"; Types: Full; Check: HasPython; Flags: fixed disablenouninstallwarning
Name: "vsrepo"; Description: "VSRepo Package Manager"; Types: Full; Flags: disablenouninstallwarning
Name: "docs"; Description: "VapourSynth Documentation"; Types: Full; Flags: disablenouninstallwarning
Name: "sdk"; Description: "VapourSynth SDK"; Flags: disablenouninstallwarning; Types: Full
Name: "pismo"; Description: "Pismo PFM Runtime (required for AVFS)"; Types: Full; Flags: disablenouninstallwarning
Name: "vsruntimes"; Description: "Visual Studio 2019 Runtime"; Types: Full; Check: IsAdminInstallMode; Flags: disablenouninstallwarning

[Tasks]
Name: newvpyfile; Description: "Add 'New VapourSynth Python Script' option to shell context menu"; GroupDescription: "New File Shortcuts:"; Components: vscore
Name: vsrepoupdate; Description: "Update VSRepo package list"; GroupDescription: "VSRepo:"; Components: vsrepo

[Run]
Filename: {code:GetPythonExecutable}; Parameters: "-m pip install ""{app}\python\{#= WheelFilename}"""; Flags: runhidden; Components: vscore
Filename: "{app}\pismo\pfm-192-vapoursynth-win.exe"; Parameters: "install"; Check: IsAdminInstallMode; Flags: runhidden; Components: pismo
Filename: {code:GetPythonExecutable}; Parameters: """{app}\vsrepo\vsrepo.py"" update"; Flags: runhidden runasoriginaluser; Components: vsrepo

[UninstallRun]
Filename: {code:GetPythonExecutable}; Parameters: "-m pip uninstall -y VapourSynth"; Flags: runhidden; Components: vscore

[Files]
;core binaries
Source: template.vpy; DestDir: {app}; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: ..\dist\{#= WheelFilename}; DestDir: {app}\python; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore

Source: {#= SourceBinaryPath}\vapoursynth.dll; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\vapoursynth.pdb; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\avfs.exe; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\vspipe.exe; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\vsvfw.dll; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\vsscript.dll; DestDir: {app}\core; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore

;vsrepo
Source: ..\vsrepo\vsrepo.py; DestDir: {app}\vsrepo; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vsrepo

;docsSource: ..\doc\_build\html\*; DestDir: {app}\docs; Flags: ignoreversion uninsrestartdelete restartreplace recursesubdirs; Components: docs

;sdk
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
Source: Plugins{#= InstallerBits}\*; DestDir: {app}\core\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\AvsCompat.dll; DestDir: {app}\core\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\EEDI3.dll; DestDir: {app}\core\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\MiscFilters.dll; DestDir: {app}\core\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\Morpho.dll; DestDir: {app}\core\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\RemoveGrainVS.dll; DestDir: {app}\core\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components:vscore
Source: {#= SourceBinaryPath}\Vinverse.dll; DestDir: {app}\core\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: {#= SourceBinaryPath}\VIVTC.dll; DestDir: {app}\core\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore

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
Root: HKA; Subkey: {#= RegistryPath}; ValueType: string; ValueName: "PythonPath"; ValueData: "{code:GetPythonPath}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore

; Write compatibility values, deprecated since R46 when the installers were split
#if InstallerBitsInt == 32
Root: HKLM; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Version"; ValueData: {#= Version}; Check: IsAdminInstallMode; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKLM; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Path"; ValueData: "{app}"; Check: IsAdminInstallMode; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKLM; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "CorePlugins"; ValueData: "{app}\core\plugins"; Check: IsAdminInstallMode; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKLM; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Plugins"; ValueData: "{app}\plugins"; Check: IsAdminInstallMode; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKLM; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "VapourSynthDLL"; ValueData: "{app}\core\vapoursynth.dll"; Check: IsAdminInstallMode; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKLM; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "VSScriptDLL"; ValueData: "{app}\core\vsscript.dll"; Check: IsAdminInstallMode; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
Root: HKLM; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "PythonPath"; ValueData: "{code:GetPythonPath}"; Check: IsAdminInstallMode; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vscore
#endif

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

#include "scripts\products.iss"
#include "scripts\products\stringversion.iss"
#include "scripts\products\msiproduct.iss"
#include "scripts\products\vcredist2017.iss"

[Code]

const VSRuntimeVersion = '14.22.27821';

type
  TPythonPath = record
    DisplayName: string;
    InstallPath: string;
    ExecutablePath: string;
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
    
function HasPython: Boolean;
var
  Counter: Integer;
begin
  Result := False;
  for Counter := 0 to GetArrayLength(PythonInstallations) - 1 do
    if PythonInstallations[Counter].Bitness = {#InstallerBitsInt} then
      Result := True;
end;

procedure GetPythonInstallations2(var DestArray: array of TPythonPath; RegRoot: Integer; RegPath: string; AssumeBitness: Integer);
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
          if (not RegQueryStringValue(RegRoot, RegPathTemp, 'SysVersion', Temp)) or (Temp <> '3.7') then
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
  GetPythonInstallations2(UserPythonInstallations, HKCU, 'SOFTWARE\Python', 0);
  GetPythonInstallations2(GlobalPythonInstallations, HKLM, 'SOFTWARE\Python', {#InstallerBitsInt}); 
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
      MsgBox('No suitable Python 3.7 ({#InstallerBits}-bit) installation found. The installer will now exit.', mbCriticalError, MB_OK)
  else if not Result and IsAdminInstallMode then
      MsgBox('Python 3.7 ({#InstallerBits}-bit) is installed for the current user only. Run the installer again and select "Install for me only" or install Python for all users.', mbCriticalError, MB_OK)
  else if not Result and not IsAdminInstallMode then
      MsgBox('Python 3.7 ({#InstallerBits}-bit) is installed for all users. Run the installer again and select "Install for all users" or install Python for the current user only.', mbCriticalError, MB_OK);
      
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
  PythonPage := CreateCustomPage(wpSelectComponents, 'Select Python Installations', 'Select one or more Python installations to use');
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
