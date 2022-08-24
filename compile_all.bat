@echo | call install_deps.bat

SET MSBuildPTH=C:\Program Files\Microsoft Visual Studio\2022\Community
IF EXIST "%MSBuildPTH%\Msbuild\Current\Bin\MSBuild.exe" GOTO buildm

SET MSBuildPTH=D:\Program Files\Microsoft Visual Studio\2022\Community
IF EXIST "%MSBuildPTH%\Msbuild\Current\Bin\MSBuild.exe" GOTO buildm

SET MSBuildPTH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise
IF EXIST "%MSBuildPTH%\Msbuild\Current\Bin\MSBuild.exe" GOTO buildm

SET MSBuildPTH=D:\Program Files\Microsoft Visual Studio\2022\Enterprise
IF EXIST "%MSBuildPTH%\Msbuild\Current\Bin\MSBuild.exe" GOTO buildm


echo MSVC's MSBuild executable not found!
GOTO endc

:buildm
"%MSBuildPTH%\Msbuild\Current\Bin\MSBuild.exe" msvc_project\VapourSynth.sln /p:Configuration=Release /p:Platform=Win32
"%MSBuildPTH%\Msbuild\Current\Bin\MSBuild.exe" msvc_project\VapourSynth.sln /p:Configuration=Release /p:Platform=x64


@echo | call docs_build.bat
@echo | call cython_build.bat
pushd installer
@echo | call make_portable.bat
@echo | call make_installers.bat
popd
pause

:endc