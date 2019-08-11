:: Extract version string
for /F "tokens=2 delims='" %%t in ('findstr /C:"#define Version " vsinstaller.iss') do set VERSION=%%t
@echo Version: %VERSION%

:: Platform-specific execution
if "%PLATFORM%" == "Win32" (
    set MSVC_RELEASE=msvc_project\Release
    set "MSVC_CRT=%MSVC_CRT32%"
    set CYTHON_PLATFORM=win32
    set ARCH_BITS=32
    set BUILD_DIR=build%ARCH_BITS%
    call :Build
) else (
    set MSVC_RELEASE=msvc_project\x64\Release
    set "MSVC_CRT=%MSVC_CRT64%"
    set CYTHON_PLATFORM=win_amd64
    set ARCH_BITS=64
    set BUILD_DIR=build%ARCH_BITS%
    call :Build
)
:: End program here (don't continue with function definition)
exit /b %ERRORLEVEL%

:: Build function
:Build
::  Create build directories
mkdir %BUILD_DIR%\vapoursynth%ARCH_BITS%\coreplugins
mkdir %BUILD_DIR%\vapoursynth%ARCH_BITS%\plugins
mkdir %BUILD_DIR%\sdk\include
mkdir %BUILD_DIR%\sdk\examples
mkdir %BUILD_DIR%\sdk\lib%ARCH_BITS%
::  Copy repository configurations
copy ..\vsrepo\vsrepo.py %BUILD_DIR%
copy ..\vsrepo\vspackages.json %BUILD_DIR%
::  Copy Cython output
copy ..\vapoursynth.cp37-%CYTHON_PLATFORM%.pyd %BUILD_DIR%
::  Copy plugins
copy ..\%MSVC_RELEASE%\AvsCompat.dll %BUILD_DIR%\vapoursynth%ARCH_BITS%\coreplugins
copy ..\%MSVC_RELEASE%\EEDI3.dll %BUILD_DIR%\vapoursynth%ARCH_BITS%\coreplugins
copy ..\%MSVC_RELEASE%\MiscFilters.dll %BUILD_DIR%\vapoursynth%ARCH_BITS%\coreplugins
copy ..\%MSVC_RELEASE%\Morpho.dll %BUILD_DIR%\vapoursynth%ARCH_BITS%\coreplugins
copy ..\%MSVC_RELEASE%\RemoveGrainVS.dll %BUILD_DIR%\vapoursynth%ARCH_BITS%\coreplugins
copy ..\%MSVC_RELEASE%\Vinverse.dll %BUILD_DIR%\vapoursynth%ARCH_BITS%\coreplugins
copy ..\%MSVC_RELEASE%\VIVTC.dll %BUILD_DIR%\vapoursynth%ARCH_BITS%\coreplugins
::  Copy C binaries
copy ..\%MSVC_RELEASE%\VapourSynth.dll %BUILD_DIR%
copy ..\%MSVC_RELEASE%\vsscript.dll %BUILD_DIR%
copy ..\%MSVC_RELEASE%\vsvfw.dll %BUILD_DIR%
copy ..\%MSVC_RELEASE%\avfs.exe %BUILD_DIR%
copy ..\%MSVC_RELEASE%\vspipe.exe %BUILD_DIR%
::  Copy C headers
copy ..\include\VapourSynth.h %BUILD_DIR%\sdk\include
copy ..\include\VSHelper.h %BUILD_DIR%\sdk\include
copy ..\include\VSScript.h %BUILD_DIR%\sdk\include
::  Copy C libraries
copy ..\%MSVC_RELEASE%\vapoursynth.lib %BUILD_DIR%\sdk\lib%ARCH_BITS%
copy ..\%MSVC_RELEASE%\vsscript.lib %BUILD_DIR%\sdk\lib%ARCH_BITS%
::  Copy SDK examples
copy ..\sdk\filter_skeleton.c %BUILD_DIR%\sdk\examples
copy ..\sdk\invert_example.c %BUILD_DIR%\sdk\examples
copy ..\sdk\vsscript_example.c %BUILD_DIR%\sdk\examples
::  Copy MSVC runtime
copy "%MSVC_CRT%\*" %BUILD_DIR%
::  Copy setup files
copy .\setup.py %BUILD_DIR%
copy .\MANIFEST.in %BUILD_DIR%
::  Create meta files (creation time & environment hint)
type nul >%BUILD_DIR%\portable.vs
type nul >%BUILD_DIR%\vapoursynth%ARCH_BITS%\plugins\.keep
::  Package!
del VapourSynth%ARCH_BITS%-Portable-%VERSION%.7z
pushd %BUILD_DIR%
7z a ..\VapourSynth%ARCH_BITS%-Portable-%VERSION%.7z *
popd
rmdir /s /q %BUILD_DIR%
::  Return from function call
exit /b 0