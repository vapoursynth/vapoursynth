@echo off

IF EXIST "Python38.dll" (
ECHO Python 3.8 detected
ECHO Switching to 3.8 support...
COPY /Y "VSScriptPython38.dll" "VSScript.dll"
ECHO Done
GOTO end )

IF EXIST "Python310.dll" (
ECHO Python 3.10 detected
GOTO end )

ECHO Python 3.10 or Python 3.8 is supported
GOTO end

:end
pause