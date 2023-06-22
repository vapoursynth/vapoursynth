call setmvscvars.bat

IF EXIST "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" (
    "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" /DInstallerBits=64 /DVSRuntimeVersion=%RedistVersion% /DVersion=%CURRENT_VERSION% /DVersionExtra=%CURRENT_VERSION_EXTRA% vsinstaller.iss
    "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" /DInstallerBits=32 /DVSRuntimeVersion=%RedistVersion% /DVersion=%CURRENT_VERSION% /DVersionExtra=%CURRENT_VERSION_EXTRA% vsinstaller.iss
    GOTO end
) ELSE (
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DInstallerBits=64 /DVSRuntimeVersion=%RedistVersion% /DVersion=%CURRENT_VERSION% /DVersionExtra=%CURRENT_VERSION_EXTRA% vsinstaller.iss
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DInstallerBits=32 /DVSRuntimeVersion=%RedistVersion% /DVersion=%CURRENT_VERSION% /DVersionExtra=%CURRENT_VERSION_EXTRA% vsinstaller.iss
    GOTO end
)

:end