@echo | call install_deps.bat

pushd installer
call setmvscvars.bat
popd

pushd msvc_project

FOR /F "tokens=*" %%g IN ('py -3.12 -c "import sys; import os; print(os.path.dirname(sys.executable))"') DO SET VSPYTHON_PATH=%%g
FOR /F "tokens=*" %%g IN ('py -3.8 -c "import sys; import os; print(os.path.dirname(sys.executable))"') DO SET VSPYTHON38_PATH=%%g

"%MSBuildPTH%\Msbuild\Current\Bin\MSBuild.exe" VapourSynth.sln -m /t:Clean;Build /p:Configuration=Release /p:Platform=x64 /p:CurrentVersion=%CURRENT_VERSION%

popd

@echo | call docs_build.bat
@echo | call cython_build.bat

pushd installer
@echo | call make_portable.bat
@echo | call make_installers.bat
popd

pause

:endc