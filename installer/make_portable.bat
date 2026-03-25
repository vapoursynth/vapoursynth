@echo off

call setmsvcvars.bat

SET VERSION_STRING=%CURRENT_VERSION%%CURRENT_VERSION_EXTRA%

@echo %VERSION_STRING%

mkdir Compiled
mkdir buildpscript
echo param(> buildpscript\Install-Portable-VapourSynth-R%VERSION_STRING%.ps1
echo     [int]$VSVersion = %CURRENT_VERSION%,>> buildpscript\Install-Portable-VapourSynth-R%VERSION_STRING%.ps1
echo     [string]$VSVersionExtra = "%CURRENT_VERSION_EXTRA%",>> buildpscript\Install-Portable-VapourSynth-R%VERSION_STRING%.ps1
type install-portable-vapoursynth.ps1 >> buildpscript\Install-Portable-VapourSynth-R%VERSION_STRING%.ps1
echo powershell.exe -executionpolicy bypass -file Install-Portable-VapourSynth-R%VERSION_STRING%.ps1 %%* > buildpscript\Install-Portable-VapourSynth-R%VERSION_STRING%.bat
mkdir buildp64\doc
mkdir buildp64\wheel
copy ..\dist\VapourSynth-%CURRENT_VERSION%%CURRENT_VERSION_EXTRA%-cp312-abi3-win_amd64.whl buildp64\wheel
copy .\VAPOURSYNTH_VERSION buildp64
xcopy /E ..\doc\_build\html\* buildp64\doc
if "%SKIP_COMPRESS%" EQU "" (
  IF EXIST "Compiled\vapoursynth64-portable-R%VERSION_STRING%.zip" (
    del Compiled\vapoursynth64-portable-R%VERSION_STRING%.zip
  )
  cd buildp64
  ..\7z.exe a ..\Compiled\VapourSynth64-Portable-R%VERSION_STRING%.zip *
  cd ..
  rmdir /s /q buildp64
  
  IF EXIST "Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.zip" (
    del Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.zip
  )
  cd buildpscript
  ..\7z.exe a ..\Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.zip Install-Portable-VapourSynth-R%VERSION_STRING%.ps1 Install-Portable-VapourSynth-R%VERSION_STRING%.bat
  cd ..
  rmdir /s /q buildpscript
)

:endc

if "%SKIP_WAIT%" EQU "" (
  pause
)
