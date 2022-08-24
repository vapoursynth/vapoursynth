@echo off

git clone https://github.com/AviSynth/AviSynthPlus
git clone https://github.com/sekrit-twc/libp2p
git clone https://github.com/vapoursynth/vsrepo
git clone https://github.com/sekrit-twc/zimg --branch v3.0
py -3.10 -m pip install -r python-requirements.txt
py -3.10-32 -m pip install -r python-requirements.txt

SET ZFOLDER=C:\Program Files\7-Zip
IF EXIST "%ZFOLDER%\7z.exe" GOTO copym

SET ZFOLDER=D:\Program Files\7-Zip
IF EXIST "%ZFOLDER%\7z.exe" GOTO copym

SET ZFOLDER=C:\Program Files (x86)\7-Zip
IF EXIST "%ZFOLDER%\7z.exe" GOTO copym

SET ZFOLDER=D:\Program Files (x86)\7-Zip
IF EXIST "%ZFOLDER%\7z.exe" GOTO copym

GOTO end
:copym
copy "%ZFOLDER%\7z.exe" installer
copy "%ZFOLDER%\7z.dll" installer
GOTO end

ECHO 7-zip not installed!

:end

IF NOT EXIST installer/pfm-192-vapoursynth-win.exe (
    ECHO You need to grab pfm-192-vapoursynth-win.exe from a portable release!
)