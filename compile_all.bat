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
