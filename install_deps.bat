@echo off

IF NOT EXIST AviSynthPlus (
    git clone https://github.com/AviSynth/AviSynthPlus
) ELSE (
    echo AviSynthPlus: & pushd AviSynthPlus & git pull & popd
)

IF NOT EXIST libp2p (
    git clone https://github.com/sekrit-twc/libp2p
) ELSE (
    echo libp2p: & pushd libp2p & git pull &popd
)

IF NOT EXIST vsrepo (
    git clone https://github.com/vapoursynth/vsrepo
) ELSE (
    echo vsrepo: & pushd vsrepo & git pull &popd
)

IF NOT EXIST zimg (
    git clone https://github.com/sekrit-twc/zimg --shallow-submodules --recurse-submodules
) ELSE (
    echo zimg: & pushd zimg & git pull & popd
)

py -3.14 -m ensurepip
py -3.14 -m pip install --upgrade -r python-requirements.txt

SET ZFOLDER=%ProgramFiles%\7-Zip
IF EXIST "%ZFOLDER%\7z.exe" GOTO copym

SET ZFOLDER=C:\Program Files\7-Zip
IF EXIST "%ZFOLDER%\7z.exe" GOTO copym

SET ZFOLDER=%ProgramFiles(x86)%\7-Zip
IF EXIST "%ZFOLDER%\7z.exe" GOTO copym

SET ZFOLDER=C:\Program Files (x86)\7-Zip
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