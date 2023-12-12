@echo off

IF EXIST "Python312.dll" (
    ECHO Python 3.12 detected
    GOTO end
)

IF EXIST "Python38.dll" (
    ECHO Python 3.8 detected
    ECHO Switching to 3.8 support...
    COPY /Y "VSScriptPython38.dll" "VSScript.dll"
    ECHO Done
    GOTO end
)

ECHO Neither Python 3.12 nor Python 3.8 is supported
GOTO end

:end
pause