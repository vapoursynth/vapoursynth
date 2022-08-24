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
pushd msvc_project
"%MSBuildPTH%\Msbuild\Current\Bin\MSBuild.exe" VapourSynth.sln /t:Clean;Build /p:Configuration=Release /p:Platform=x64
"%MSBuildPTH%\Msbuild\Current\Bin\MSBuild.exe" VapourSynth.sln /t:Clean;Build /p:Configuration=Release /p:Platform=Win32
popd

@echo | call docs_build.bat
@echo | call cython_build.bat
pushd installer
@echo | call make_portable.bat
@echo | call make_installers.bat
popd
pause

:endc