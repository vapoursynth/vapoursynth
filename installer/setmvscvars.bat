@REM @echo off

IF NOT DEFINED MSBuildPTH GOTO setmvscpath
IF NOT EXIST MSBuildPTH GOTO setmvscpath

goto foundmvspath

:setmvscpath
SET MSBuildPTH=%ProgramFiles%\Microsoft Visual Studio\2022\Community
IF EXIST "%MSBuildPTH%" GOTO foundmvspath

SET MSBuildPTH=C:\Program Files\Microsoft Visual Studio\2022\Community
IF EXIST "%MSBuildPTH%" GOTO foundmvspath

SET MSBuildPTH=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise
IF EXIST "%MSBuildPTH%" GOTO foundmvspath

SET MSBuildPTH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise
IF EXIST "%MSBuildPTH%" GOTO foundmvspath

@echo on
ECHO MSVC couldn't be found!
@echo off
GOTO endc

:foundmvspath
IF NOT DEFINED RedistVersion GOTO setredistvars
IF NOT DEFINED RedistShortVersion GOTO setredistvars

GOTO endc

:setredistvars
SET MVSCRedistPath=%MSBuildPTH%\VC\Redist\MSVC
SET RedistVersion=
SET RedistShortVersion=

for /F "delims=" %%A in ('dir "%MVSCRedistPath%" /o-n /ad /b') do (
    IF NOT DEFINED RedistShortVersion (
        SET RedistShortVersion=%%A
    ) ELSE (
        IF NOT DEFINED RedistVersion SET RedistVersion=%%A
    )
)
SET RedistShortVersion=%RedistShortVersion:~1%

:endc

@echo on
