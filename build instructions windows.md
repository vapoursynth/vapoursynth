# Preparing the Build Environment on Windows

Default install paths are assumed in all projects and scripts, be prepared to adjust many things if you changed them

## Required languages and applications

* Needs [Visual Studio 2019](https://visualstudio.microsoft.com/de/vs/)
* It also needs both [32bit](https://www.python.org/) and [64bit](https://www.python.org/) Python 3.8.x and 3.9.x (the msvc project assumes that you installed python for all users.)
* [InnoSetup](http://www.jrsoftware.org/isdl.php) is needed to create the installer (default installation path assumed)
* [7-zip](https://www.7-zip.org/) is needed to compress the portable version (default installation path assumed)

## 2. Preparing the C++ Project

* Clone VapourSynth
* Clone VSRepo into the VapourSynth dir (`git clone https://github.com/vapoursynth/vsrepo`)
* Clone zimg into the VapourSynth dir (`git clone https://github.com/sekrit-twc/zimg --branch v3.0`)
* Clone avs+ into the VapourSynth dir (`git clone https://github.com/AviSynth/AviSynthPlus.git`)
* Clone libp2p into the VapourSynth dir (`git clone https://github.com/sekrit-twc/libp2p.git`)
* Compile 32 and 64 bit releases using the VapourSynth solution
* Alternatively, you can use `c++_build.bat` which will clone the repositories and compile the Vapoursynth solution.
  You have to have `msbuild` in your path or launch it from `x64 Native Tools Command Prompt for VS 2019`.

## 3. Preparing the Python Project

* Run `py -3.9 -m pip install -r python-requirements.txt` for 64bit.
* Run `py -3.9-32 -m pip install -r python-requirements.txt` for 32bit.
* Run `py -3.8 -m pip install -r python-requirements.txt` for 64bit.
* Run `py -3.8-32 -m pip install -r python-requirements.txt` for 32bit.
* Alternatively, you can use `install_python_requirements.bat` to launch all the python commands above. 
* Run `cython_build.bat` to compile the Python modules
* Run `docs_build.bat` to compile the documentation

## 4. Distribution

All the above steps are necessary to create the installer

You also need 7z.exe and 7z.dll from
the 32 bit version of [7-zip](https://www.7-zip.org/)
Both need to be placed in the "installer" dir.
(if you only plan to make 64 bit builds then the 64 bit version is ok to use instead)

You'll also have to grab the file `pfm-192-vapoursynth-win.exe`
which is only available from installations/portable releases.

Run `make_portable.bat` and `make_installers.bat` to package things.

## Additional notes
Note that the Avisynth side of AVFS won't work properly in debug builds (memory allocation and exceptions across module boundaries trolololol)