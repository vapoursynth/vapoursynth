if exist C:\Program Files (x86)\Inno Setup 6\ (
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DInstallerBits=64 vsinstaller.iss
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DInstallerBits=32 vsinstaller.iss
) else (
    "D:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DInstallerBits=64 vsinstaller.iss
    "D:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DInstallerBits=32 vsinstaller.iss
)