rem @echo | call install_deps.bat

pushd installer
call setmvscvars.bat
popd

pushd msvc_project

FOR /F "tokens=*" %%g IN ('py -3.14 -c "import sys; import os; print(os.path.dirname(sys.executable))"') DO SET VSPYTHON_PATH=%%g

"%MSBuildPTH%\Msbuild\Current\Bin\MSBuild.exe" -m /t:Clean;Build /p:Configuration=Release /p:Platform=x64 /p:CurrentVersion=%CURRENT_VERSION% VapourSynth.sln

popd

if %ERRORLEVEL% NEQ 0 goto builderror

@echo | call docs_build.bat
@echo | call cython_build.bat

pushd installer
@echo | call make_portable.bat
@echo | call make_installers.bat
popd
@echo BUILD COMPLETE

goto endc

:builderror
@echo BUILD FAILED

:endc

pause
