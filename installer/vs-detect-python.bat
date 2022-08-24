@echo off

IF EXIST "Python310.dll" (
ECHO Python 3.10 detected
GOTO end )

ECHO Python 3.10 is supported
GOTO end

:end
pause