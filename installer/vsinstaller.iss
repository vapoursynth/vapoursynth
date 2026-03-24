#define PythonVersionMinorLow 12
#define PythonVersionMinorHigh 30

#define AppName 'VapourSynth'
#define AppId 'VapourSynth'
#define RegistryPath 'SOFTWARE\VapourSynth'
#define SourceBinaryPath '..\msvc_project\x64\Release'
#define WheelFilename(Version) 'VapourSynth-' + Version + VersionExtra + '-cp312-abi3-win_amd64.whl'
#define VSRepoVersion '51'
#define VSRepoWheelFilename(VSRepoVer) 'vsrepo-' + VSRepoVer + '-py3-none-any.whl'

#define Dependency_NoExampleSetup
#include "CodeDependencies.iss"

[Setup]
OutputDir=Compiled
OutputBaseFilename=VapourSynth-x64-R{#= Version}{#= VersionExtra}
Compression=lzma2/max
SolidCompression=yes
VersionInfoDescription={#= AppName} x64 R{#= Version}{#= VersionExtra} Installer
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
MinVersion=10.0.14393
PrivilegesRequired=lowest
WizardStyle=modern dynamic
FlatComponentsList=yes
ChangesEnvironment=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: Full; Description: Full installation; Flags: iscustom

[Components]
Name: "vscore"; Description: "VapourSynth x64 R{#= Version}{#= VersionExtra}"; Types: Full; Flags: fixed disablenouninstallwarning
Name: "vsrepo"; Description: "VSRepo Package Manager"; Types: Full; Flags: disablenouninstallwarning
Name: "docs"; Description: "VapourSynth Documentation"; Types: Full; Flags: disablenouninstallwarning
Name: "sdk"; Description: "VapourSynth SDK"; Flags: disablenouninstallwarning; Types: Full
Name: "vsruntimes"; Description: "Visual Studio 2015-2026 Runtime"; Types: Full; Flags: disablenouninstallwarning

[Tasks]
Name: registerinstall; Description: "Set VSSCRIPT_PATH environment variable"; GroupDescription: "VapourSynth:"; Components: vscore
Name: legacyinstall; Description: "Write legacy installation information to the registry"; GroupDescription: "VapourSynth:"; Components: vscore
Name: registervfw; Description: "Register VFW component"; GroupDescription: "VapourSynth:"; Components: vscore
Name: newvpyfile; Description: "Add 'New VapourSynth Python Script' option to shell context menu"; GroupDescription: "VapourSynth:"; Components: vscore
Name: vsrepoupdate; Description: "Update VSRepo package list"; GroupDescription: "VSRepo:"; Components: vsrepo

[Run]
Filename: {code:GetPythonExecutable}; Parameters: "-m pip install --user ""{app}\python\{#= WheelFilename(Version)}"""; Check: IsPython3; Flags: runhidden; Components: vscore
Filename: {code:GetPythonExecutable}; Parameters: "-m pip install --user ""{app}\python\{#= VSRepoWheelFilename(VSRepoVersion)}"""; Check: IsPython3; Flags: runhidden; Components: vsrepo
Filename: {code:GetPythonExecutable}; Parameters: "-m vapoursynth vapoursynth-config"; Check: IsPython3; Flags: runhidden; Components: vscore
Filename: {code:GetPythonExecutable}; Parameters: "-m vapoursynth register-install"; Check: IsPython3; Flags: runhidden; Components: vscore; Tasks: registerinstall
Filename: {code:GetPythonExecutable}; Parameters: "-m vapoursynth register-vfw"; Check: IsPython3; Flags: runhidden; Components: vscore; Tasks: registervfw
Filename: {code:GetPythonExecutable}; Parameters: "-m vapoursynth register-install legacy"; Check: IsPython3; Flags: runhidden; Components: vscore; Tasks: legacyinstall
Filename: {code:GetPythonExecutable}; Parameters: "-m vsrepo update"; Flags: runhidden; Components: vsrepo

[UninstallRun]
Filename: {code:GetPythonExecutable}; Parameters: "-m pip uninstall -y VapourSynth"; Flags: runhidden; RunOnceId: "VSUninstallPyModule"; Components: vscore

[Files]
;core binaries
Source: template.vpy; DestDir: {app}; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore
Source: ..\dist\{#= WheelFilename(Version)}; DestDir: {app}\python; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore

;vsrepo
Source: ..\vsrepo\dist\{#= VSRepoWheelFilename(VSRepoVersion)}; DestDir: {app}\python; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vscore

;docs
Source: ..\doc\_build\html\*; DestDir: {app}\docs; Flags: ignoreversion uninsrestartdelete restartreplace recursesubdirs; Components: docs

;sdk
Source: ..\include\VapourSynth4.h; DestDir: {app}\sdk\include\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\include\VSHelper4.h; DestDir: {app}\sdk\include\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\include\VSScript4.h; DestDir: {app}\sdk\include\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\include\VSConstants4.h; DestDir: {app}\sdk\include\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\sdk\filter_skeleton.c; DestDir: {app}\sdk\examples; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\sdk\invert_example.c; DestDir: {app}\sdk\examples; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk
Source: ..\sdk\vsscript_example.c; DestDir: {app}\sdk\examples; Flags: ignoreversion uninsrestartdelete restartreplace; Components: sdk

[Icons]
Name: {group}\VapourSynth Website; Filename: http://www.vapoursynth.com/; Components: vscore
Name: {group}\Documentation (Local); Filename: {app}\docs\index.html; Components: docs
Name: {group}\Documentation (Online); Filename: http://www.vapoursynth.com/doc/
Name: {group}\Autoload Directory; Filename: {code:GetPythonExecutable}; Parameters: "-c ""import vapoursynth as vs; from subprocess import run; run(['explorer.exe', vs.get_plugin_dir()])"""; Flags: runminimized; Components: vscore
Name: {group}\VapourSynth SDK; Filename: {app}\sdk; Components: sdk

[Registry]
; new vpy file shortcut task
Root: HKCU; Subkey: SOFTWARE\Classes\.vpy\ShellNew; ValueType: string; ValueName: "FileName"; ValueData: "{app}\template.vpy"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: newvpyfile

[Code]

type
  TPythonPath = record
    DisplayName: string;
    InstallPath: string;
    ExecutablePath: string;
    Version: string;
    Bitness: Integer;
    User: Boolean;
  end;

var
  RuntimesAdded: Boolean;
  PythonInstallations: array of TPythonPath;
  PythonPage: TWizardPage;
  PythonList: TNewCheckListBox;
  PythonPath: string;
  PythonExecutable: string;
  PythonVersion: string;

    
function IsPython3: Boolean;
var Counter : Integer;
begin
  Result := False;
  for Counter := {#PythonVersionMinorLow} to {#PythonVersionMinorHigh} do
  begin
    if PythonVersion = '3.' + IntToStr(Counter) then
    begin
      Result := True;
      break;
    end;
  end;
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

          if Bitness = 64 then
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
               DestArray[GetArrayLength(DestArray) - 1].User := (RegRoot = HKCU);
            end;
          end;
        end;
      end;
    end;
  end;  
end;

procedure GetPythonInstallations;
var Counter: Integer;
begin
  for Counter := {#PythonVersionMinorHigh} downto {#PythonVersionMinorLow} do
  begin
    GetPythonInstallations2(PythonInstallations, HKCU, 'SOFTWARE\Python', '3.' + IntToStr(Counter), 0);
    GetPythonInstallations2(PythonInstallations, HKLM, 'SOFTWARE\Python', '3.' + IntToStr(Counter), 64); 
  end;
end;

procedure PopulatePythonInstallations(List: TNewCheckListBox);
var
  Counter: Integer;
  First: Boolean;
begin
  List.Items.Clear;
  First := True;
  for Counter := 0 to GetArrayLength(PythonInstallations) - 1 do
    if PythonInstallations[Counter].Bitness = 64 then
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
  RegPath := 'Environment';
  
  if not RegQueryStringValue(HKCU, RegPath, 'Path', Paths) then
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

      if RegWriteStringValue(HKCU, RegPath, 'Path', Paths) then
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
begin
  RuntimesAdded := False;
  PythonList := nil;
  GetPythonInstallations;
  Result := GetArrayLength(PythonInstallations) > 0; 
  if not Result then
      MsgBox('No suitable Python installations found.', mbCriticalError, MB_OK);
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
    if WizardIsComponentSelected('vsruntimes') and not RuntimesAdded then
    begin
      Dependency_AddVC2015To2022;
      RuntimesAdded := True;
    end;
  end;
end;

/////////////////////////////////////////////////////////////////////

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RemovePath(ExpandConstant('{app}\vsrepo'));
  end;
end;
