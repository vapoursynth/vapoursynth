rem extract version string
for /F "tokens=2 delims='" %%a in ('findstr /C:"#define Version" vsinstaller.iss') do set v=%%a
@echo %v%

rem 64bit build
mkdir buildp64\vapoursynth64\coreplugins
mkdir buildp64\vapoursynth64\plugins
copy ..\vapoursynth.cp36-win_amd64.pyd buildp64
copy ..\msvc_project\x64\Release\VapourSynth.dll buildp64
copy ..\msvc_project\x64\Release\vsscript.dll buildp64
copy ..\msvc_project\x64\Release\avfs.exe buildp64
copy ..\msvc_project\x64\Release\vsvfw.dll buildp64
copy ..\msvc_project\x64\Release\vspipe.exe buildp64
copy ..\msvc_project\x64\Release\AvsCompat.dll buildp64\vapoursynth64\coreplugins
copy ..\msvc_project\x64\Release\EEDI3.dll buildp64\vapoursynth64\coreplugins
copy ..\msvc_project\x64\Release\Morpho.dll buildp64\vapoursynth64\coreplugins
copy ..\msvc_project\x64\Release\RemoveGrainVS.dll buildp64\vapoursynth64\coreplugins
copy ..\msvc_project\x64\Release\Vinverse.dll buildp64\vapoursynth64\coreplugins
copy ..\msvc_project\x64\Release\VIVTC.dll buildp64\vapoursynth64\coreplugins
copy "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Redist\MSVC\14.10.25008\x64\Microsoft.VC150.CRT\*" buildp64
copy x64\plugins\* buildp64\vapoursynth64\coreplugins
type nul >buildp64\portable.vs
rm Compiled\vapoursynth64-portable-%v%.7z
cd buildp64
"C:\Program Files\7-Zip\7z.exe" a ..\Compiled\VapourSynth64-Portable-%v%.7z *
cd ..
rmdir /s /q buildp64

rem 32bit build
mkdir buildp32\vapoursynth32\coreplugins
mkdir buildp32\vapoursynth32\plugins
copy ..\vapoursynth.cp36-win32.pyd buildp32
copy ..\msvc_project\Release\VapourSynth.dll buildp32
copy ..\msvc_project\Release\vsscript.dll buildp32
copy ..\msvc_project\Release\avfs.exe buildp32
copy ..\msvc_project\Release\vsvfw.dll buildp32
copy ..\msvc_project\Release\vspipe.exe buildp32
copy ..\msvc_project\Release\AvsCompat.dll buildp32\vapoursynth32\coreplugins
copy ..\msvc_project\Release\EEDI3.dll buildp32\vapoursynth32\coreplugins
copy ..\msvc_project\Release\Morpho.dll buildp32\vapoursynth32\coreplugins
copy ..\msvc_project\Release\RemoveGrainVS.dll buildp32\vapoursynth32\coreplugins
copy ..\msvc_project\Release\Vinverse.dll buildp32\vapoursynth32\coreplugins
copy ..\msvc_project\Release\VIVTC.dll buildp32\vapoursynth32\coreplugins
copy "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Redist\MSVC\14.10.25008\x86\Microsoft.VC150.CRT\*" buildp32
copy x86\plugins\* buildp32\vapoursynth32\coreplugins
type nul >buildp32\portable.vs
rm Compiled\vapoursynth32-portable-%v%.7z
cd buildp32
"C:\Program Files\7-Zip\7z.exe" a ..\Compiled\VapourSynth32-Portable-%v%.7z *
cd ..
rmdir /s /q buildp32

