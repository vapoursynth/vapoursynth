IF EXIST "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" (
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DInstallerBits=64 vsinstaller.iss
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DInstallerBits=32 vsinstaller.iss
    GOTO end
) ELSE (
    "D:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DInstallerBits=64 vsinstaller.iss
    "D:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DInstallerBits=32 vsinstaller.iss
    GOTO end
)

:end