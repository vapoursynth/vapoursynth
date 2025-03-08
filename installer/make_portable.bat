@echo off

call setmvscvars.bat

@echo %VERSION_STRING%
mkdir Compiled
echo param(> Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.ps1
echo     [int]$VSVersion = %VERSION_STRING%,>> Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.ps1
type install-portable-vapoursynth.ps1 >> Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.ps1
echo powershell.exe -executionpolicy bypass -file Install-Portable-VapourSynth-R%VERSION_STRING%.ps1 %%* > Compiled\Install-Portable-VapourSynth-R%VERSION_STRING%.bat
mkdir buildp64\vs-coreplugins
mkdir buildp64\vs-plugins
mkdir buildp64\sdk\include\vapoursynth
mkdir buildp64\sdk\examples
mkdir buildp64\sdk\lib64
mkdir buildp64\doc
mkdir buildp64\vsgenstubs4
mkdir buildp64\wheel
copy ..\vsrepo\vsrepo.py buildp64
copy ..\vsrepo\vsgenstubs.py buildp64
copy ..\vsrepo\vsgenstubs4 buildp64\vsgenstubs4
copy 7z.exe buildp64
copy 7z.dll buildp64
copy ..\dist\VapourSynth-%VERSION_STRING%-cp313-cp313-win_amd64.whl buildp64\wheel
copy ..\dist\VapourSynth-%VERSION_STRING%-cp38-cp38-win_amd64.whl buildp64\wheel
copy ..\msvc_project\x64\Release\vsscript.dll buildp64
copy ..\msvc_project\x64\Release\vsscriptpython38.dll buildp64
copy ..\msvc_project\x64\Release\avfs.exe buildp64
copy ..\msvc_project\x64\Release\vsvfw.dll buildp64
copy ..\msvc_project\x64\Release\vspipe.exe buildp64
copy ..\msvc_project\x64\Release\AvsCompat.dll buildp64\vs-coreplugins
copy ..\include\VapourSynth.h buildp64\sdk\include\vapoursynth
copy ..\include\VSHelper.h buildp64\sdk\include\vapoursynth
copy ..\include\VSScript.h buildp64\sdk\include\vapoursynth
copy ..\include\VapourSynth4.h buildp64\sdk\include\vapoursynth
copy ..\include\VSHelper4.h buildp64\sdk\include\vapoursynth
copy ..\include\VSScript4.h buildp64\sdk\include\vapoursynth
copy ..\include\VSConstants4.h buildp64\sdk\include\vapoursynth
copy ..\msvc_project\x64\Release\vapoursynth.lib buildp64\sdk\lib64
copy ..\msvc_project\x64\Release\vsscript.lib buildp64\sdk\lib64
copy ..\sdk\filter_skeleton.c buildp64\sdk\examples
copy ..\sdk\invert_example.c buildp64\sdk\examples
copy ..\sdk\vsscript_example.c buildp64\sdk\examples
copy "%MVSCRedistPath%\%RedistVersion%\x64\Microsoft.VC%RedistShortVersion%.CRT\*" buildp64
copy pfm-192-vapoursynth-win.exe buildp64
copy .\VAPOURSYNTH_VERSION buildp64
copy .\MANIFEST.in buildp64
xcopy /E ..\doc\_build\html\* buildp64\doc
type nul >buildp64\portable.vs
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
