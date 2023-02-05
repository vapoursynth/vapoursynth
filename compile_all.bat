@echo | call install_deps.bat

pushd installer
call setmvscvars.bat
popd

pushd msvc_project

FOR /F "tokens=*" %%g IN ('py -3.11 -c "import sys; import os; print(os.path.dirname(sys.executable))"') DO SET VSPYTHON_PATH=%%g
FOR /F "tokens=*" %%g IN ('py -3.11-32 -c "import sys; import os; print(os.path.dirname(sys.executable))"') DO SET VSPYTHON32_PATH=%%g

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