rem extract version string
for /F "tokens=2 delims='" %%a in ('findstr /C:"#define Version" vsinstaller.iss') do set v=%%a
@echo %v%

rem 64bit build
mkdir buildp64\vapoursynth64\coreplugins
mkdir buildp64\vapoursynth64\plugins
mkdir buildp64\sdk\include
mkdir buildp64\sdk\examples
mkdir buildp64\sdk\lib32
mkdir buildp64\sdk\lib64
copy ..\vsrepo\vsrepo.py buildp64
copy ..\vsrepo\vspackages.json buildp64
copy ..\vapoursynth.cp37-win_amd64.pyd buildp64
copy ..\msvc_project\x64\Release\VapourSynth.dll buildp64
copy ..\msvc_project\x64\Release\vsscript.dll buildp64
copy ..\msvc_project\x64\Release\avfs.exe buildp64
copy ..\msvc_project\x64\Release\vsvfw.dll buildp64
copy ..\msvc_project\x64\Release\vspipe.exe buildp64
copy ..\msvc_project\x64\Release\AvsCompat.dll buildp64\vapoursynth64\coreplugins
copy ..\msvc_project\x64\Release\EEDI3.dll buildp64\vapoursynth64\coreplugins
copy ..\msvc_project\x64\Release\MiscFilters.dll buildp64\vapoursynth64\coreplugins
copy ..\msvc_project\x64\Release\Morpho.dll buildp64\vapoursynth64\coreplugins
copy ..\msvc_project\x64\Release\RemoveGrainVS.dll buildp64\vapoursynth64\coreplugins
copy ..\msvc_project\x64\Release\Vinverse.dll buildp64\vapoursynth64\coreplugins
copy ..\msvc_project\x64\Release\VIVTC.dll buildp64\vapoursynth64\coreplugins
copy ..\include\VapourSynth.h buildp64\sdk\include
copy ..\include\VSHelper.h buildp64\sdk\include
copy ..\include\VSScript.h buildp64\sdk\include
copy ..\msvc_project\Release\vapoursynth.lib buildp64\sdk\lib32
copy ..\msvc_project\Release\vsscript.lib buildp64\sdk\lib32
copy ..\msvc_project\x64\Release\vapoursynth.lib buildp64\sdk\lib64
copy ..\msvc_project\x64\Release\vsscript.lib buildp64\sdk\lib64
copy ..\sdk\filter_skeleton.c buildp64\sdk\examples
copy ..\sdk\invert_example.c buildp64\sdk\examples
copy ..\sdk\vsscript_example.c buildp64\sdk\examples
copy "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Redist\MSVC\14.22.27821\x64\Microsoft.VC142.CRT\*" buildp64
copy x64\plugins\* buildp64\vapoursynth64\coreplugins
copy pfm-192-vapoursynth-win.exe buildp64
copy .\setup.py buildp64
copy .\MANIFEST.in buildp64
type nul >buildp64\portable.vs
type nul >buildp64\vapoursynth64\plugins\.keep
del Compiled\vapoursynth64-portable-%v%.7z
cd buildp64
"C:\Program Files\7-Zip\7z.exe" a ..\Compiled\VapourSynth64-Portable-%v%.7z *
cd ..
rmdir /s /q buildp64

rem 32bit build
mkdir buildp32\vapoursynth32\coreplugins
mkdir buildp32\vapoursynth32\plugins
mkdir buildp32\sdk\include
mkdir buildp32\sdk\examples
mkdir buildp32\sdk\lib32
mkdir buildp32\sdk\lib64
copy ..\vsrepo\vsrepo.py buildp32
copy ..\vsrepo\vspackages.json buildp32
copy ..\vapoursynth.cp37-win32.pyd buildp32
copy ..\msvc_project\Release\VapourSynth.dll buildp32
copy ..\msvc_project\Release\vsscript.dll buildp32
copy ..\msvc_project\Release\avfs.exe buildp32
copy ..\msvc_project\Release\vsvfw.dll buildp32
copy ..\msvc_project\Release\vspipe.exe buildp32
copy ..\msvc_project\Release\AvsCompat.dll buildp32\vapoursynth32\coreplugins
copy ..\msvc_project\Release\EEDI3.dll buildp32\vapoursynth32\coreplugins
copy ..\msvc_project\Release\MiscFilters.dll buildp32\vapoursynth32\coreplugins
copy ..\msvc_project\Release\Morpho.dll buildp32\vapoursynth32\coreplugins
copy ..\msvc_project\Release\RemoveGrainVS.dll buildp32\vapoursynth32\coreplugins
copy ..\msvc_project\Release\Vinverse.dll buildp32\vapoursynth32\coreplugins
copy ..\msvc_project\Release\VIVTC.dll buildp32\vapoursynth32\coreplugins
copy ..\include\VapourSynth.h buildp32\sdk\include
copy ..\include\VSHelper.h buildp32\sdk\include
copy ..\include\VSScript.h buildp32\sdk\include
copy ..\msvc_project\Release\vapoursynth.lib buildp32\sdk\lib32
copy ..\msvc_project\Release\vsscript.lib buildp32\sdk\lib32
copy ..\msvc_project\x64\Release\vapoursynth.lib buildp32\sdk\lib64
copy ..\msvc_project\x64\Release\vsscript.lib buildp32\sdk\lib64
copy ..\sdk\filter_skeleton.c buildp32\sdk\examples
copy ..\sdk\invert_example.c buildp32\sdk\examples
copy ..\sdk\vsscript_example.c buildp32\sdk\examples
copy "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Redist\MSVC\14.22.27821\x86\Microsoft.VC142.CRT\*" buildp32
copy x86\plugins\* buildp32\vapoursynth32\coreplugins
copy pfm-192-vapoursynth-win.exe buildp32
copy .\setup.py buildp32
copy .\MANIFEST.in buildp32
type nul >buildp32\portable.vs
type nul >buildp32\vapoursynth32\plugins\.keep
del Compiled\vapoursynth32-portable-%v%.7z
cd buildp32
"C:\Program Files\7-Zip\7z.exe" a ..\Compiled\VapourSynth32-Portable-%v%.7z *
cd ..
rmdir /s /q buildp32
