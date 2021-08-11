echo off
IF NOT EXIST "Python38.dll" GOTO pcurrent
ECHO Python 3.8 detected
COPY /Y "VSScriptPython38.dll" "VSScript.dll"
GOTO end
:pcurrent
ECHO Python 3.9 detected
:end
ECHO Done
pause