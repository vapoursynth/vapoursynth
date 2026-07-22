call setmsvcvars.bat

rem Locate the Inno Setup compiler: prefer one on PATH, then the standard install
rem locations for version 7 and 6.
set "ISCCEXE="
for /f "delims=" %%i in ('where ISCC 2^>nul') do if not defined ISCCEXE set "ISCCEXE=%%i"
if not defined ISCCEXE if exist "%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe" set "ISCCEXE=%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe"
if not defined ISCCEXE if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCCEXE=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCCEXE (
    echo Inno Setup ISCC.exe not found
    exit /b 1
)

"%ISCCEXE%" /DVSRuntimeVersion=%RedistVersion% /DVersion=%CURRENT_VERSION% /DVersionExtra=%CURRENT_VERSION_EXTRA% vsinstaller.iss

:end