@echo on
pushd %SRC_DIR%\installer
call setmvscvars.bat
popd

pushd %SRC_DIR%
rmdir /s /q build
del vapoursynth.*.pyd
del /q dist\*.whl
cd 

pushd %SRC_DIR%\msvc_project

SET VSPYTHON_PATH=%PREFIX%
cd %SRC_DIR%\msvc_project

MSBuild.exe ./../libp2p/_msvc/libp2p_simd/libp2p_simd.vcxproj /p:configuration=release /p:platform=x64 /maxCpuCount:%CPU_COUNT% /p:SolutionDir=%cd%\
if %ERRORLEVEL% neq 0 exit 1
MSBuild.exe ./Core/Core.vcxproj /p:configuration=release /p:platform=x64 /maxCpuCount:%CPU_COUNT% /p:SolutionDir=%cd%\
if %ERRORLEVEL% neq 0 exit 1
MSBuild.exe ./VSScript/VSScript.vcxproj /p:configuration=release /p:platform=x64 /maxCpuCount:%CPU_COUNT% /p:SolutionDir=%cd%\
if %ERRORLEVEL% neq 0 exit 1
MSBuild.exe ./VSPipe/VSPipe.vcxproj /p:configuration=release /p:platform=x64 /maxCpuCount:%CPU_COUNT% /p:SolutionDir=%cd%\
if %ERRORLEVEL% neq 0 exit 1
MSBuild.exe ./AvsCompat/AvsCompat.vcxproj /p:configuration=release /p:platform=x64 /maxCpuCount:%CPU_COUNT% /p:SolutionDir=%cd%\
if %ERRORLEVEL% neq 0 exit 1
MSBuild.exe ./AVFS/AVFS.vcxproj /p:configuration=release /p:platform=x64 /maxCpuCount:%CPU_COUNT% /p:SolutionDir=%cd%\
if %ERRORLEVEL% neq 0 exit 1
MSBuild.exe ./VSVFW/VSVFW.vcxproj /p:configuration=release /p:platform=x64 /maxCpuCount:%CPU_COUNT% /p:SolutionDir=%cd%\
if %ERRORLEVEL% neq 0 exit 1


@echo %VERSION_STRING%

cd x64\release
:: 复制文件
copy VapourSynth.dll %PREFIX%\VapourSynth.dll
copy VSScript.dll %PREFIX%\VSScript.dll
copy VSPipe.exe %PREFIX%\VSPipe.exe
copy AVFS.exe %PREFIX%\AVFS.exe
mkdir %PREFIX%\vs-coreplugins
copy AvsCompat.dll %PREFIX%\vs-coreplugins\AvsCompat.dll
copy VSVFW.dll %PREFIX%\VSVFW.dll
mkdir %PREFIX%\vs-plugins
cd. > %PREFIX%\portable.vs
cd. > %PREFIX%\vs-plugins\.keep

copy %SRC_DIR%\vsrepo\vsrepo.py %PREFIX% /Y
copy %SRC_DIR%\vsrepo\vsgenstubs.py %PREFIX% /Y
xcopy %SRC_DIR%\vsrepo\vsgenstubs4 %PREFIX%\vsgenstubs4 /E /I /Y

copy "%SOURCE_DIR%\pfm-192-vapoursynth-win.exe" "%PREFIX%\pfm-192-vapoursynth-win.exe" /Y
copy "%SOURCE_DIR%\vs-detect-python.bat" "%PREFIX%\vs-detect-python.bat" /Y

mkdir %LIBRARY_LIB%
copy VapourSynth.lib %LIBRARY_LIB%\VapourSynth.lib
copy VSScript.lib %LIBRARY_LIB%\VSScript.lib

mkdir %LIBRARY_INC%\vapoursynth
xcopy /E /F /I /Y %SRC_DIR%\include %LIBRARY_INC%\vapoursynth

(
echo prefix=%PREFIX%/Library
echo exec_prefix=${prefix}
echo libdir=${exec_prefix}/lib
echo includedir=${prefix}/include/vapoursynth
echo,
echo Name: vapoursynth
echo Description: A frameserver for the 21st century
echo Version: %PKG_VERSION%
echo,
echo Libs: -L${libdir} -lVapourSynth
echo Cflags: -I${includedir}
)>%LIBRARY_LIB%\pkgconfig\vapoursynth.pc

(
echo prefix=%PREFIX%/Library
echo exec_prefix=${prefix}
echo libdir=${exec_prefix}/lib
echo includedir=${prefix}/include/vapoursynth
echo,
echo Name: vapoursynth-script
echo Description: Library for interfacing VapourSynth with Python
echo Version: %PKG_VERSION%
echo,
echo Requires: vapoursynth
echo Libs: -L${libdir} -lVSScript
echo Cflags: -I${includedir}
)>%LIBRARY_LIB%\pkgconfig\vapoursynth-script.pc

cd %SRC_DIR%
"%PYTHON%" -m pip install -vv .

:end
endlocal