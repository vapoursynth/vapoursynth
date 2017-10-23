#!/usr/bin/env python

from platform import architecture
from shutil import which
from os import curdir
from os.path import join, exists, dirname
from setuptools import setup, Extension

is_win = (architecture()[1] == "WindowsPE")
is_64 = (architecture()[0] == "64bit")

extra_data = {}

library_dirs = [curdir, "build"]
if is_win:
    if is_64:
        library_dirs.append(join("msvc_project", "x64", "Release"))
        lib_suffix = "lib64"
    else:
        library_dirs.append(join("msvc_project", "Release"))
        lib_suffix = "lib32"

    #
    # This code detects the library directory by querying the Windows Registry
    # for the current VapourSynth directory location.
    #

    import winreg
    REGISTRY_PATH = r"SOFTWARE\VapourSynth"
    REGISTRY_KEY = "VapourSynthDLL"
    REGISTRY_KEY_PATH = "Path"

    def query(hkey, path, key):
        reg_key = None
        try:
            reg_key = winreg.OpenKey(hkey, path, 0, winreg.KEY_READ)
            value, _ = winreg.QueryValueEx(reg_key, key)
        finally:
            if reg_key is not None:
                winreg.CloseKey(reg_key)

        return value

    # Locate the vapoursynth dll inside the library directories first
    # should we find it, it is a clear indicator that VapourSynth
    # has been compiled by the user.
    for path in library_dirs:
        dll_path = join(path, "vapoursynth.dll")
        if exists(dll_path):
            break
    else:
        # In case the user did not compile vapoursynth by themself, we will then
        # hit the path.
        #
        # This is an indicator for portable installations.
        dll_path = which("vapoursynth.dll")
        if dll_path is None:
            # If the vapoursynth.dll is not located in PATH, we then hit the registry
            try:
                dll_path = query(winreg.HKEY_LOCAL_MACHINE, REGISTRY_PATH, REGISTRY_KEY)
            except:
                # Give up.
                raise OSError("Couldn't detect vapoursynth installation path")
            else:
                # Since the SDK is on a different directory than the DLL insert the SDK to library_dirs
                sdkpath = join(query(winreg.HKEY_LOCAL_MACHINE, REGISTRY_PATH, REGISTRY_KEY_PATH), "sdk", lib_suffix)
                if not exists(sdkpath):
                    raise OSError("It appears you don't have the sdk installed. Please make sure you installed the sdk before running setup.py")
                library_dirs.append(sdkpath)

        # Insert the DLL Path to the library dirs
        if dll_path:
            library_dirs.append(dirname(dll_path))

    # Make sure the setup process copies the VapourSynth.dll into the site-package folder
    print("Found VapourSynth.dll at:", dll_path)
    extra_data["data_files"] = [(r"Lib\site-packages", [dll_path])]
        
        
setup(
    name = "VapourSynth",
    description = "A frameserver for the 21st century",
    url = "http://www.vapoursynth.com/",
    download_url = "https://github.com/vapoursynth/vapoursynth",
    author = "Fredrik Mellbin",
    author_email = "fredrik.mellbin@gmail.com",
    license = "LGPL 2.1 or later",
    version = "40",
    long_description = "A portable replacement for Avisynth",
    platforms = "All",
    ext_modules = [Extension("vapoursynth", [join("src", "cython", "vapoursynth.pyx")],
                             libraries = ["vapoursynth"],
                             library_dirs = library_dirs,
                             include_dirs = [curdir, join("src", "cython")],
                             cython_c_in_temp = 1)],
    setup_requires=[
        'setuptools>=18.0',
        "Cython",
    ],
    
    **extra_data
)
