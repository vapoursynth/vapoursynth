#define AppName = 'VapourSynth'
#define Version = 'R35'

[Setup]
OutputDir=Compiled
OutputBaseFilename=VapourSynth-{#= Version}
Compression=lzma2/max
InternalCompressLevel=max
SolidCompression=yes
LZMAUseSeparateProcess=yes
VersionInfoDescription={#= AppName} {#= Version} Installer
AppId={#= AppName}
AppName={#= AppName} {#= Version}
AppVersion={#= Version}
AppVerName={#= AppName} {#= Version}
AppPublisher=Fredrik Mellbin
AppPublisherURL=http://www.vapoursynth.com/
AppSupportURL=http://www.vapoursynth.com/
AppUpdatesURL=http://www.vapoursynth.com/
VersionInfoVersion=1.35.0.0
DefaultDirName={pf32}\VapourSynth
DefaultGroupName=VapourSynth
AllowCancelDuringInstall=no
AllowNoIcons=yes
AllowUNCPath=no
MinVersion=6.0
PrivilegesRequired=admin
FlatComponentsList=yes
ArchitecturesAllowed=x86 x64
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: Full; Description: Full installation; Flags: iscustom

[Components]
Name: "vs64"; Description: "VapourSynth 64-bit"; Types: Full; Check: HasPython64; Flags: disablenouninstallwarning
Name: "vs32"; Description: "VapourSynth 32-bit"; Types: Full; Check: HasPython32; Flags: disablenouninstallwarning
Name: "sdk"; Description: "VapourSynth SDK"; Flags: disablenouninstallwarning; Types: Full
Name: "pismo"; Description: "Pismo PFM Runtime (required for AVFS)"; Types: Full; Flags: disablenouninstallwarning
Name: "vsruntimes"; Description: "Visual Studio Runtimes (2013 & 2015)"; Types: Full; Flags: disablenouninstallwarning

[Tasks]
Name: newvpyfile; Description: "Add 'New VapourSynth Python Script' option to shell context menu"; GroupDescription: "New File Shortcuts:"; Components: vs32 vs64

[Run]
Filename: "{app}\pismo\pfm-183-vapoursynth-win.exe"; Parameters: "install"; Flags: runhidden; Components: pismo

[Files]
;core binaries
Source: template.vpy; DestDir: {app}; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32 vs64
Source: vapoursynth.pth; DestDir: {code:GetPythonPath32}\Lib\site-packages; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: vapoursynth.pth; DestDir: {code:GetPythonPath64}\Lib\site-packages; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: ..\vapoursynth.cp35-win32.pyd; DestDir: {code:GetPythonPath32}\Lib\site-packages\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\vapoursynth.cp35-win_amd64.pyd; DestDir: {code:GetPythonPath64}\Lib\site-packages\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: ..\msvc_project\Release\vapoursynth.dll; DestDir: {code:GetPythonPath32}\Lib\site-packages\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\x64\Release\vapoursynth.dll; DestDir: {code:GetPythonPath64}\Lib\site-packages\vapoursynth; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64
Source: ..\msvc_project\Release\vapoursynth.dll; DestDir: {app}\core32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\Release\vapoursynth.pdb; DestDir: {app}\core32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\x64\Release\vapoursynth.dll; DestDir: {app}\core64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64
Source: ..\msvc_project\x64\Release\vapoursynth.pdb; DestDir: {app}\core64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: ..\msvc_project\Release\avfs.exe; DestDir: {app}\core32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\x64\Release\avfs.exe; DestDir: {app}\core64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: ..\msvc_project\Release\vspipe.exe; DestDir: {app}\core32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\x64\Release\vspipe.exe; DestDir: {app}\core64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: ..\msvc_project\Release\vsvfw.dll; DestDir: {app}\core32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\x64\Release\vsvfw.dll; DestDir: {app}\core64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

Source: ..\msvc_project\Release\vsscript.dll; DestDir: {app}\core32; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\x64\Release\vsscript.dll; DestDir: {app}\core64; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64
Source: ..\msvc_project\Release\vsscript.dll; DestDir: {sys}; Flags: uninsrestartdelete restartreplace 32bit; Components: vs32
Source: ..\msvc_project\x64\Release\vsscript.dll; DestDir: {sys}; Flags: uninsrestartdelete restartreplace 64bit; Components: vs64


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
Source: x86\plugins\*; DestDir: {app}\core32\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\Release\AvsCompat.dll; DestDir: {app}\core32\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\Release\EEDI3.dll; DestDir: {app}\core32\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\Release\RemoveGrainVS.dll; DestDir: {app}\core32\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\Release\Vinverse.dll; DestDir: {app}\core32\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32
Source: ..\msvc_project\Release\VIVTC.dll; DestDir: {app}\core32\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs32

Source: x64\plugins\*; DestDir: {app}\core64\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64
Source: ..\msvc_project\x64\Release\AvsCompat.dll; DestDir: {app}\core64\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64
Source: ..\msvc_project\x64\Release\EEDI3.dll; DestDir: {app}\core64\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64
Source: ..\msvc_project\x64\Release\RemoveGrainVS.dll; DestDir: {app}\core64\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64
Source: ..\msvc_project\x64\Release\Vinverse.dll; DestDir: {app}\core64\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64
Source: ..\msvc_project\x64\Release\VIVTC.dll; DestDir: {app}\core64\plugins; Flags: ignoreversion uninsrestartdelete restartreplace; Components: vs64

;pismo installer
Source: "pfm-183-vapoursynth-win.exe"; DestDir: {app}\pismo; Flags: ignoreversion uninsrestartdelete restartreplace; Components: pismo

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
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Version"; ValueData: {#= Version}; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM64; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Version"; ValueData: {#= Version}; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64

Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Path"; ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM64; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Path"; ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64

Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "CorePlugins"; ValueData: "{app}\core32\plugins"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM64; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "CorePlugins"; ValueData: "{app}\core64\plugins"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64

Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Plugins"; ValueData: "{app}\plugins32"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM64; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Plugins"; ValueData: "{app}\plugins64"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64

Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "VapourSynthDLL"; ValueData: "{app}\core32\vapoursynth.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM64; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "VapourSynthDLL"; ValueData: "{app}\core64\vapoursynth.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64

Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "VSScriptDLL"; ValueData: "{app}\core32\vsscript.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "PythonPath"; ValueData: "{code:GetPythonPath32}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM64; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "VSScriptDLL"; ValueData: "{app}\core64\vsscript.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM64; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "PythonPath"; ValueData: "{code:GetPythonPath64}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64

; legacy entries, will one day be removed
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "CorePlugins32"; ValueData: "{app}\core32\plugins"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "CorePlugins64"; ValueData: "{app}\core64\plugins"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Plugins32"; ValueData: "{app}\plugins32"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\VapourSynth; ValueType: string; ValueName: "Plugins64"; ValueData: "{app}\plugins64"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64

; new vpy file shortcut task
Root: HKLM; Subkey: SOFTWARE\Classes\.vpy\ShellNew; ValueType: string; ValueName: "FileName"; ValueData: "{app}\template.vpy"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: newvpyfile

; 32bit vfw
Root: HKLM32; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}; ValueType: string; ValueName: ""; ValueData: "VapourSynth"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: ""; ValueData: "{app}\core32\vsvfw.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: ""; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: "Source Filter"; ValueData: "{{D3588AB0-0781-11CE-B03A-0020AF0BA770}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\.vpy; ValueType: string; ValueName: ""; ValueData: "vpyfile"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\vpyfile; ValueType: string; ValueName: ""; ValueData: "VapourSynth Python Script"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\vpyfile\DefaultIcon; ValueType: string; ValueName: ""; ValueData: "{app}\core32\vsvfw.dll,0"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32
Root: HKLM32; Subkey: SOFTWARE\Classes\AVIFile\Extensions\VPY; ValueType: string; ValueName: ""; ValueData: "{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs32

; 64bit vfw
Root: HKLM64; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}; ValueType: string; ValueName: ""; ValueData: "VapourSynth"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM64; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: ""; ValueData: "{app}\core64\vsvfw.dll"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM64; Subkey: SOFTWARE\Classes\CLSID\{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM64; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: ""; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM64; Subkey: SOFTWARE\Classes\Media Type\Extensions\.vpy; ValueType: string; ValueName: "Source Filter"; ValueData: "{{D3588AB0-0781-11CE-B03A-0020AF0BA770}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM64; Subkey: SOFTWARE\Classes\.vpy; ValueType: string; ValueName: ""; ValueData: "vpyfile"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM64; Subkey: SOFTWARE\Classes\vpyfile; ValueType: string; ValueName: ""; ValueData: "VapourSynth Python Script"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM64; Subkey: SOFTWARE\Classes\vpyfile\DefaultIcon; ValueType: string; ValueName: ""; ValueData: "{app}\core64\vsvfw.dll,0"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64
Root: HKLM64; Subkey: SOFTWARE\Classes\AVIFile\Extensions\VPY; ValueType: string; ValueName: ""; ValueData: "{{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: vs64

[Code]
#include "scripts\products.iss"
#include "scripts\products\msiproduct.iss"
#include "scripts\products\vcredist2013.iss"
#include "scripts\products\vcredist2015.iss"

type
  TPythonPath = record
    DisplayName: string;
    InstallPath: string;
    Bitness: Integer;
  end;

var
  Runtimes32Added: Boolean;
  Runtimes64Added: Boolean;
  PythonInstallations: array of TPythonPath;
  PythonPage: TWizardPage;
  PythonList: TNewCheckListBox;
  Python32Path: string;
  Python64Path: string;

function HasPython32: Boolean;
var
  Counter: Integer;
begin
  Result := False;
  for Counter := 0 to GetArrayLength(PythonInstallations) - 1 do
    if PythonInstallations[Counter].Bitness = 32 then
      Result := True;
end;

function HasPython64: Boolean;
var
  Counter: Integer;
begin
  Result := False;
  for Counter := 0 to GetArrayLength(PythonInstallations) - 1 do
    if PythonInstallations[Counter].Bitness = 64 then
      Result := True;
end;

procedure GetPythonInstallations2(RegRoot: Integer; RegPath: string; AssumeBitness: Integer);
var
  Names, Tags: TArrayOfString;
  Nc, Tc: Integer;
  RegPathTemp: string;
  Temp: string;
  DisplayName, InstallPath: string;
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
          if (not RegQueryStringValue(RegRoot, RegPathTemp, 'SysVersion', Temp)) or (Temp <> '3.5') then
            continue;
          if RegQueryStringValue(RegRoot, RegPathTemp, 'SysArchitecture', Temp) then
          begin
            if Temp = '32bit' then
              Bitness := 32
            else if Temp = '64bit' then
              Bitness := 64;              
          end;

          if RegQueryStringValue(RegRoot, RegPathTemp, 'DisplayName', DisplayName) and RegQueryStringValue(RegRoot, RegPathTemp + '\InstallPath', '', InstallPath) then
          begin
             SetArrayLEngth(PythonInstallations, GetArrayLength(PythonInstallations) + 1);
             PythonInstallations[GetArrayLength(PythonInstallations) - 1].DisplayName := DisplayName;
             PythonInstallations[GetArrayLength(PythonInstallations) - 1].InstallPath := InstallPath;
             PythonInstallations[GetArrayLength(PythonInstallations) - 1].Bitness := Bitness;
          end;
        end;
      end;
    end;
  end;  
end;

procedure GetPythonInstallations;
var
  Success: Boolean;
  PythonPath32: string;
  PythonPath64: string;
begin
  Success := RegQueryStringValue(HKCU32, 'SOFTWARE\Python\PythonCore\3.5-32\InstallPath', '', PythonPath32);
  if not Success then
    RegQueryStringValue(HKLM32, 'SOFTWARE\Python\PythonCore\3.5-32\InstallPath', '', PythonPath32);

  if Is64BitInstallMode then
  begin
    Success := RegQueryStringValue(HKCU, 'SOFTWARE\Python\PythonCore\3.5\InstallPath', '', PythonPath64);
    if not Success then
      RegQueryStringValue(HKLM, 'SOFTWARE\Python\PythonCore\3.5\InstallPath', '', PythonPath64);
  end;

  if PythonPath32 <> '' then
  begin
    SetArrayLEngth(PythonInstallations, GetArrayLength(PythonInstallations) + 1);
    PythonInstallations[GetArrayLength(PythonInstallations) - 1].DisplayName := 'Python 3.5 (32-bit)';
    PythonInstallations[GetArrayLength(PythonInstallations) - 1].InstallPath := PythonPath32;
    PythonInstallations[GetArrayLength(PythonInstallations) - 1].Bitness := 32;
  end;

  if PythonPath64 <> '' then
  begin
    SetArrayLEngth(PythonInstallations, GetArrayLength(PythonInstallations) + 1);
    PythonInstallations[GetArrayLength(PythonInstallations) - 1].DisplayName := 'Python 3.5 (64-bit)';
    PythonInstallations[GetArrayLength(PythonInstallations) - 1].InstallPath := PythonPath64;
    PythonInstallations[GetArrayLength(PythonInstallations) - 1].Bitness := 64;
  end;
  
  GetPythonInstallations2(HKCU, 'SOFTWARE\Python', 0);
  GetPythonInstallations2(HKLM32, 'SOFTWARE\Python', 32);
  if Is64BitInstallMode then
    GetPythonInstallations2(HKLM, 'SOFTWARE\Python', 64);      
end;

procedure PopulatePythonInstallations(List: TNewCheckListBox);
var
  Counter: Integer;
  First: Boolean;
begin
  List.Items.Clear;

  if IsComponentSelected('vs32') then
  begin
    First := True;
    List.AddGroup('Python Environments (32-bit)', '', 0, nil);
    for Counter := 0 to GetArrayLength(PythonInstallations) - 1 do
      if PythonInstallations[Counter].Bitness = 32 then
        with PythonInstallations[Counter] do
        begin
          List.AddRadioButton(DisplayName, '(' + InstallPath + ')', 1, First, True, TObject(Counter));
          First := False;
        end;
  end;

  if IsComponentSelected('vs64') then
  begin
    First := True;
    List.AddGroup('Python Environments (64-bit)', '', 0, nil);
    for Counter := 0 to GetArrayLength(PythonInstallations) - 1 do
      if PythonInstallations[Counter].Bitness = 64 then
        with PythonInstallations[Counter] do
        begin
          List.AddRadioButton(DisplayName, '(' + InstallPath + ')', 1, First, True, TObject(Counter));
          First := False;
        end;
  end;        
end;

function InitializeSetup: Boolean;
begin
  Runtimes32Added := False;
  Runtimes64Added := False;
  PythonList := nil;
  GetPythonInstallations;
  Result := True;
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
end;

function GetPythonPath32(Param: string): String;
begin
  Result := Python32Path;
end;

function GetPythonPath64(Param: String): String;
begin
  Result := Python64Path;
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
var
  Counter: Integer;
  Idx: Integer;
begin
  Result := True;
  if CurPageID = PythonPage.ID then
  begin    
    for Counter := 0 to PythonList.Items.Count - 1 do
    begin
      if (PythonList.Checked[Counter]) and (PythonList.ItemLevel[Counter] = 1) then
      begin
        Idx := Integer(PythonList.ItemObject[Counter]);
        with PythonInstallations[Idx] do
        begin
          if Bitness = 64 then
            Python64Path := InstallPath
          else if Bitness = 32 then
            Python32Path := InstallPath;
        end;
      end;
    end;
  end
  else if CurPageID = wpSelectComponents then
  begin
    if not IsComponentSelected('vs32 or vs64') then
    begin
      Result := False;
      MsgBox('At least one version of the core library has to be installed.', mbCriticalError, MB_OK)
    end;
    
    PopulatePythonInstallations(PythonList); 
  end
  else if CurPageID = wpReady then
  begin
    if IsComponentSelected('vsruntimes') and IsComponentSelected('vs32') and not Runtimes32Added then
    begin
      SetForceX86(True);
      vcredist2013();
      vcredist2015();
      SetForceX86(False);
      Runtimes32Added := True;
    end;
    if IsComponentSelected('vsruntimes') and IsComponentSelected('vs64') and not Runtimes64Added then
    begin
      vcredist2013();
      vcredist2015();
      Runtimes64Added := True;
    end;
    Result := NextButtonClick2(CurPageID);
  end;
end;
