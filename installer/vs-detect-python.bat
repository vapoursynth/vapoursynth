@echo off

IF EXIST "Python311.dll" (
    ECHO Python 3.11 detected
    GOTO end
)

IF EXIST "Python38.dll" (
    ECHO Python 3.8 detected
    ECHO Switching to 3.8 support...
    COPY /Y "VSScriptPython38.dll" "VSScript.dll"
    ECHO Done
    GOTO end
)

ECHO Neither Python 3.11 nor Python 3.8 is supported
GOTO end

:end
pause