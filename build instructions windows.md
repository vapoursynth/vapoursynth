# Preparing the Build Environment on Windows

Default install paths are assumed in all projects and scripts, be prepared to adjust many things if you changed them

## Required languages and applications

* Needs [Visual Studio 2019](https://visualstudio.microsoft.com/de/vs/)
* It also needs both [32bit](https://www.python.org/) and [64bit](https://www.python.org/) Python 3.8.x and 3.9.x (the msvc project assumes that you installed python for all users.)
* [InnoSetup 6.x](http://www.jrsoftware.org/isdl.php) is needed to create the installer (default installation path assumed)
* [7-zip](https://www.7-zip.org/) is needed to compress the portable version (default installation path assumed)

## 2. Preparing the C++ Project

* Clone VapourSynth
* Clone VSRepo into the VapourSynth dir (`git clone https://github.com/vapoursynth/vsrepo`)
* Clone zimg into the VapourSynth dir (`git clone https://github.com/sekrit-twc/zimg --branch v3.0`)
* Clone avs+ into the VapourSynth dir (`git clone https://github.com/AviSynth/AviSynthPlus.git`)
* Clone libp2p into the VapourSynth dir (`git clone https://github.com/sekrit-twc/libp2p.git`)
* Clone mimalloc into the VapourSynth dir (`git clone https://github.com/microsoft/mimalloc`)
* Compile 32 and 64 bit releases of mimalloc using the solution in mimalloc\ide\vs2019
* Compile 32 and 64 bit releases using the VapourSynth solution

## 3. Preparing the Python Project

* Run `py -3.9 -m pip install -r python-requirements.txt` for 64bit.
* Run `py -3.9-32 -m pip install -r python-requirements.txt` for 32bit.
* Run `py -3.8 -m pip install -r python-requirements.txt` for 64bit.
* Run `py -3.8-32 -m pip install -r python-requirements.txt` for 32bit.
* Run `cython_build.bat` to compile the Python modules
* Run `docs_build.bat` to compile the documentation

## 4. Distribution

All the above steps are necessary to create the installer

In order to build the installer you need to download
and place isxdl.dll in "installer\scripts\isxdl".
It can be downloaded from: [NET-Framework Installer for InnoSetup](http://www.codeproject.com/Articles/20868/NET-Framework-Installer-for-InnoSetup)

You also need 7z.exe and 7z.dll from
the 32 bit version of [7-zip](https://www.7-zip.org/)
Both need to be placed in the "installer" dir.

You'll also have to grab the file `pfm-192-vapoursynth-win.exe`
which is only available from installations/portable releases.

Run `make_portable.bat` and `make_installers.bat` to package things.

## Additional notes
Note that the Avisynth side of AVFS won't work properly in debug builds (memory allocation and exceptions across module boundaries trolololol)