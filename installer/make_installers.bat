call setmvscvars.bat

IF EXIST "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" (
    "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" /DVSRuntimeVersion=%RedistVersion% /DVersion=%CURRENT_VERSION% /DVersionExtra=%CURRENT_VERSION_EXTRA% vsinstaller.iss
    GOTO end
) ELSE (
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DVSRuntimeVersion=%RedistVersion% /DVersion=%CURRENT_VERSION% /DVersionExtra=%CURRENT_VERSION_EXTRA% vsinstaller.iss
    GOTO end
)

:end