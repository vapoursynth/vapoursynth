# Preparing the Build Environment on Windows

Default install paths are assumed in some projects and scripts, be prepared to adjust many things if you changed them.

## Required languages and applications

* Needs [Visual Studio 2019](https://visualstudio.microsoft.com/de/vs/)
* It also needs both [32bit](https://www.python.org/ftp/python/3.7.4/python-3.7.4-webinstall.exe) and [64bit](https://www.python.org/ftp/python/3.7.4/python-3.7.4-amd64-webinstall.exe) Python 3.7 series
* [InnoSetup 6.x](http://www.jrsoftware.org/isdl.php#stable) is needed to create the installer (default installation path assumed)
* [7-zip](https://www.7-zip.org/) is needed to compress the portable version (default installation path assumed)

## 2. Preparing the C++ Project

* Clone VapourSynth
* Clone VSRepo into the VapourSynth dir (`git clone https://github.com/vapoursynth/vsrepo`)
* Clone zimg v2.9 branch into the VapourSynth dir (`git clone https://github.com/sekrit-twc/zimg --branch v2.9`)
* Clone avs+ pinterf mt branch into the VapourSynth dir (`git clone https://github.com/pinterf/AviSynthPlus --branch MT`)
* Set environment variables `PYTHON32` and `PYTHON64` for the msvc project and cython build
* Compile 32 and 64 bit releases using the VapourSynth solution

## 3. Preparing the Python Project

* Run `py -3.7 -m pip install -r python-requirements.txt` for 64bit.
* Run `py -3.7-32 -m pip install -r python-requirements.txt` for 32bit.
* Run `cython_build.bat` to compile the Python modules
* Run `docs_build.bat` to compile the documentation

## 4. Distribution

All the above steps are necessary to create the installer

In order to build the installer you need to download
and place isxdl.dll in "installer\scripts\isxdl".
It can be downloaded from: [NET-Framework Installer for InnoSetup](http://www.codeproject.com/Articles/20868/NET-Framework-Installer-for-InnoSetup)

There are also a few plugins that aren't included
which are easier to simply retrieve from an existing
VapourSynth installation/portable release.

Run `make_portable.bat` and `make_installers.bat` to package things.

## Additional notes
Note that the Avisynth side of AVFS won't work properly in debug builds (memory allocation and exceptions across module boundaries trolololol)
Likewise AviSource can only be compiled as a release build (or debug versions of functions that aren't present are referenced)
