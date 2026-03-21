@echo off

call setmvscvars.bat

SET VERSION_STRING=%CURRENT_VERSION%%CURRENT_VERSION_EXTRA%

@echo %VERSION_STRING%

mkdir Compiled
echo param(> Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.ps1
echo     [int]$VSVersion = %CURRENT_VERSION%,>> Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.ps1
echo     [string]$VSVersionExtra = "%CURRENT_VERSION_EXTRA%",>> Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.ps1
type install-portable-vapoursynth.ps1 >> Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.ps1
echo powershell.exe -executionpolicy bypass -file Install-Portable-VapourSynth-R%VERSION_STRING%.ps1 %%* > Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.bat
mkdir buildp64\doc
mkdir buildp64\vsgenstubs4
mkdir buildp64\wheel
copy ..\vsrepo\vsrepo.py buildp64
copy ..\vsrepo\vsgenstubs.py buildp64
copy ..\vsrepo\vsgenstubs4 buildp64\vsgenstubs4
copy 7z.exe buildp64
copy 7z.dll buildp64
copy ..\dist\VapourSynth-%CURRENT_VERSION%-cp312-abi3-win_amd64.whl buildp64\wheel
copy .\VAPOURSYNTH_VERSION buildp64
xcopy /E ..\doc\_build\html\* buildp64\doc
if "%SKIP_COMPRESS%" EQU "" (
  IF EXIST "Compiled\vapoursynth64-portable-R%VERSION_STRING%.zip" (
    del Compiled\vapoursynth64-portable-R%VERSION_STRING%.zip
  )
  cd buildp64
  7z.exe a ..\Compiled\VapourSynth64-Portable-R%VERSION_STRING%.zip *
  cd ..
  rmdir /s /q buildp64
)

:endc

if "%SKIP_WAIT%" EQU "" (
  pause
)
